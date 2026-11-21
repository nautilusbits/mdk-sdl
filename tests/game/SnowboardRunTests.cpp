#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "engine/platform/Input.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/kurt/Kurt.h"
#include "game/level/LevelData.h"
#include "game/level/TriangleGroups.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/Rides.h"
#include "game/scripts/ScriptRuntime.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;
using Mdk::Game::Kurt::Mortality;

// Found by playtest: LEVEL4's first board run (MEAT_1 → CMEAT_1 → MEAT_3) never ended. Its
// script waits in MEAT_1 for Kurt in a box of MEAT_3; the board stayed in MEAT_1, frozen once Kurt
// left CMEAT_1. The original moves objects with flag 0x80000 (the ridden board) into the arena whose
// connection they cross (0x45e810, 0x43ca00).
namespace
{
    constexpr int Level = 4;
    constexpr float Step = 1.f / 60.f;
    constexpr int StepsPerSecond = 60;
    constexpr const char* Start = "MEAT_1";
    constexpr const char* End = "MEAT_3";
    // Kurt lands on group 1 of MEAT_1 (its hit script spawns the board and XS); XS's death
    // makes the board rideable.
    constexpr int LandingGroup = 1;
    constexpr const char* BoardType = "XSNOWB";
    const Vector3 Landing(0.f, 0.f, 80.f);
    const Vector3 AboveBoard(0.f, 0.f, 3.f);
    constexpr float Yaw = 90.f;
    // The run takes about 45 s; the script lets him off in MEAT_3 (y −2674…3176).
    constexpr int RunSeconds = 60;

    // The second run's corridor, near its board (first key 420, 4285).
    constexpr const char* SecondRun = "CMEAT_3";
    const Vector3 SecondBoard(420.f, 4280.f, -578.f);
    constexpr int RideSeconds = 5;
    constexpr const char* SecondEnd = "MEAT_5";
    // Without keys the ride reaches MEAT_5 after about 145 s.
    constexpr int SecondRunSeconds = 180;
    // The third run, from MEAT_7 (its board at 0, 14803): its script lets Kurt off in MEAT_10, y ≥ 23970.
    constexpr const char* ThirdRun = "MEAT_7";
    const Vector3 ThirdStart(0.f, 14700.f, 40.f);
    constexpr int ThirdRunSeconds = 240;
    constexpr const char* ThirdEnd = "MEAT_10";
    constexpr float ThirdEndY = 23970.f;
    // The scripts' random numbers: with some, MEAT_4 stops a rider without keys.
    constexpr int Seed = 4;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    struct Created
    {
        std::shared_ptr<ScriptRuntime> Runtime;
        std::shared_ptr<ArenaSpace> Space;
        std::shared_ptr<TriangleGroups> Groups;
    };

    Created CreateRuntime()
    {
        auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, Level);
        auto number = std::to_string(Level);
        auto cmi = Formats::Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".CMI"));
        auto sprites = Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        auto space = std::make_shared<ArenaSpace>();
        auto groups = std::make_shared<TriangleGroups>();
        for (const auto& arena : level->Arenas)
        {
            if (!level->IsReachable(arena->Name))
            {
                continue;
            }

            space->Add(arena);
            groups->Add(arena);
        }

        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
        auto runtime = std::make_shared<ScriptRuntime>(level, cmi, sprites, space, groups, mixer, kurt, Seed);
        return {runtime, space, groups};
    }

    // Kurt and the scripts for up to that many steps, as the game runs them.
    void Run(ScriptRuntime& runtime, ArenaSpace& space, int steps, const std::function<bool()>& until = nullptr)
    {
        Input input;
        for (int i = 0; i < steps && !(until && until()); i++)
        {
            runtime.Kurt->Update(input, Step);
            runtime.Update(Step);
            space.SetSolid(runtime.SolidArenas());
        }
    }

    // Onto the second run's board.
    std::shared_ptr<MdkObject> BoardSecondRun(ScriptRuntime& runtime, ArenaSpace& space)
    {
        runtime.TeleportKurt(SecondRun, SecondBoard, Yaw);
        Run(runtime, space, StepsPerSecond);
        auto board = runtime.FindObjectNamed(BoardType);
        REQUIRE(board != nullptr);
        runtime.TeleportKurt(SecondRun, board->Position + AboveBoard, Yaw);
        Run(runtime, space, StepsPerSecond, [&] { return runtime.Rides->OnBoard(); });
        CHECK(runtime.Rides->OnBoard());
        return board;
    }
}

DATA_TEST_CASE("SnowboardRunTests.FirstRunEndsInMeat3")
{
    auto [runtimePtr, spacePtr, groups] = CreateRuntime();
    auto& runtime = *runtimePtr;
    auto& space = *spacePtr;
    runtime.TeleportKurt(Start, Landing, Yaw);
    Run(runtime, space, StepsPerSecond / 2);
    runtime.HitGroup(Start, LandingGroup, 0, ScriptRuntime::HitKurt, ScriptRuntime::HitTypeKurt);
    Run(runtime, space, StepsPerSecond);
    auto xs = runtime.FindObjectNamed("XS");
    REQUIRE(xs != nullptr);
    runtime.Kill(*xs);
    Run(runtime, space, StepsPerSecond);

    // Onto the board.
    auto board = runtime.FindObjectNamed(BoardType);
    REQUIRE(board != nullptr);
    runtime.TeleportKurt(Start, board->Position + AboveBoard, Yaw);
    Run(runtime, space, StepsPerSecond, [&] { return runtime.Rides->OnBoard(); });
    CHECK(runtime.Rides->OnBoard());

    // Down the run, without keys: the script lets him off in MEAT_3.
    Run(runtime, space, RunSeconds * StepsPerSecond, [&] { return !runtime.Rides->OnBoard(); });

    CHECK_FALSE(runtime.Rides->OnBoard());
    CHECK(runtime.CurrentArena == End);
    CHECK(board->Arena == End);
}

// Found by playtest: on the second run (CMEAT_3) the slope vanished. Its board runs
// group_state_near_player 2, 10, 31, 1, 2: the original (0x453a1e) gives the groups near Kurt
// the opposite op (shown) and the others the op (hidden), not the reverse.
DATA_TEST_CASE("SnowboardRunTests.SecondRunShowsSlopeUnderKurt")
{
    auto [runtimePtr, spacePtr, groups] = CreateRuntime();
    auto& runtime = *runtimePtr;
    auto& space = *spacePtr;
    BoardSecondRun(runtime, space);

    Run(runtime, space, RideSeconds * StepsPerSecond);

    int floor = runtime.GetKurtFloorGroup();
    CHECK(floor != 0);
    CHECK(runtime.CurrentArena == SecondRun);
    auto group = groups->Get(SecondRun, floor);
    REQUIRE(group != nullptr);
    CHECK_FALSE(HasFlag(group->State, TriangleGroups::State::Hidden));
}

// Found by playtest: the second run didn't end in MEAT_5. Its board's script lets Kurt off
// in a box there, but the board stayed in CMEAT_4: MEAT_5's arena_show NONE dropped CMEAT_4
// before the board's tick. The original runs the arenas' scripts after the objects.
// Fails in the C# too since mdk-sdl e1fde55 ("Stand Kurt on platforms' triangles, not boxes"):
// both end in MEAT_4 with identical traces. Expected to fail until fixed upstream.
TEST_CASE("SnowboardRunTests.SecondRunEndsInMeat5" * doctest::skip(!::Mdk::Tests::GameData::Present()) * doctest::should_fail())
{
    auto [runtimePtr, spacePtr, groups] = CreateRuntime();
    auto& runtime = *runtimePtr;
    auto& space = *spacePtr;
    auto board = BoardSecondRun(runtime, space);

    // Down CMEAT_3, MEAT_4 and CMEAT_4, without keys: the script lets him off in MEAT_5.
    Run(runtime, space, SecondRunSeconds * StepsPerSecond, [&] { return !runtime.Rides->OnBoard(); });

    CHECK_FALSE(runtime.Rides->OnBoard());
    CHECK(runtime.CurrentArena == SecondEnd);
    CHECK(board->Arena == SecondEnd);
}

// Issue #38: MEAT_4, on the second run, has no red bones nor EWJ. Its script spawns 6
// BONEFLC and 3 SW_EWJ with spawn_flagged; they have no model: the original (0x451b88) spawns
// them as sprite boxes (spawn_box 0x45cca8), BONEFLC 9 × 9 × 6, SW_EWJ 6 × 6 × 6.
DATA_TEST_CASE("SnowboardRunTests.SecondRunHasBones")
{
    const std::string Arena = "MEAT_4";
    constexpr int Bones = 6;
    constexpr int Cows = 3;
    auto [runtimePtr, spacePtr, groups] = CreateRuntime();
    auto& runtime = *runtimePtr;
    auto& space = *spacePtr;
    runtime.TeleportKurt(Arena, Vector3(301.f, 8300.f, -1200.f), Yaw);
    Run(runtime, space, StepsPerSecond);

    std::vector<std::shared_ptr<MdkObject>> pickups;
    for (const auto& o : runtime.Objects)
    {
        if (!o->Dead && o->Arena == Arena && (o->Flags & MdkObject::FlagPickup) != 0)
        {
            pickups.push_back(o);
        }
    }

    auto ofType = [&](const char* type) { return std::count_if(pickups.begin(), pickups.end(), [&](const auto& o) { return o->TypeName == type; }); };
    CHECK(ofType("BONEFLC") == Bones);
    CHECK(ofType("SW_EWJ") == Cows);

    // Kurt takes the first bone he rides through.
    auto found = std::find_if(pickups.begin(), pickups.end(), [](const auto& o) { return o->TypeName == "BONEFLC"; });
    REQUIRE(found != pickups.end());
    auto bone = *found;
    runtime.TeleportKurt(Arena, bone->Position, Yaw);
    Run(runtime, space, 2);
    CHECK((bone->Flags & MdkObject::FlagCollected) != 0);

    // It shrinks away.
    Run(runtime, space, StepsPerSecond);
    CHECK(bone->Dead);
}

DATA_TEST_CASE("SnowboardRunTests.ThirdRunEnds")
{
    auto [runtimePtr, spacePtr, groups] = CreateRuntime();
    auto& runtime = *runtimePtr;
    auto& space = *spacePtr;
    runtime.TeleportKurt(ThirdRun, ThirdStart, Yaw);
    Run(runtime, space, StepsPerSecond);
    auto found = std::find_if(runtime.Objects.begin(), runtime.Objects.end(),
        [](const auto& o) { return !o->Dead && o->TypeName == BoardType && o->Arena == ThirdRun; });
    REQUIRE(found != runtime.Objects.end());
    auto board = *found;
    runtime.TeleportKurt(ThirdRun, board->Position + AboveBoard, Yaw);
    Run(runtime, space, StepsPerSecond, [&] { return runtime.Rides->OnBoard(); });
    CHECK(runtime.Rides->OnBoard());

    // Down CMEAT_7, MEAT_8 and CMEAT_8 (god: its aliens shoot): the script lets him off at the end.
    runtime.Kurt->Mortality = Mortality::God;
    Run(runtime, space, ThirdRunSeconds * StepsPerSecond, [&] { return !runtime.Rides->OnBoard(); });

    CHECK_FALSE(runtime.Rides->OnBoard());
    CHECK(runtime.CurrentArena == ThirdEnd);
    CHECK(runtime.Kurt->Feet.Y >= ThirdEndY);
    CHECK(runtime.Kurt->Feet.Y <= std::numeric_limits<float>::max());
}
