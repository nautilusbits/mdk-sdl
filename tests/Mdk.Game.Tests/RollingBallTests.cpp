#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <doctest.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Inventory.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Shared/GameData.h"

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
using Mdk::Game::Kurt::Difficulty;

// LEVEL6's rolling boulders (OLYM_6's XBO, spawned when Kurt enters the floor area).
namespace
{
    constexpr int Level = 6;
    constexpr const char* Room = "OLYM_6";
    constexpr const char* Ball = "XBO";
    // Its shots (hurt_kurt 6) would blur the ball's damage.
    constexpr const char* Grunt = "XC";
    constexpr float Tick = 1.0f / 30.0f;
    constexpr int RollTicks = 150;
    constexpr int HitTicks = 300;
    constexpr float FloorTolerance = 0.5f;
    constexpr float RayReach = 50.0f;

    // Inside the spawn box (if_kurt_in_box at 0xcc34).
    const Vector3 Entry{-1900.0f, 3100.0f, -2160.0f};

    // On the floor in the first ball's path.
    const Vector3 InPath{-1903.0f, 3010.0f, -2202.0f};

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ScriptRuntime> CreateRuntime(std::shared_ptr<ArenaSpace> space, Difficulty difficulty)
    {
        auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, Level);
        auto number = std::to_string(Level);
        auto cmi = Formats::Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".CMI"));
        auto sprites = Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
        kurt->Inventory->Difficulty = difficulty;
        auto runtime = std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
        runtime->TeleportKurt(Room, Entry, 0.0f);
        return runtime;
    }
}

// The ball rests on its origin; it's drawn lifted by its height offset (set_height_offset 5 vs a
// radius of 5.15), so only a sliver sinks into the floor (0x43b65c: z + obj+0x5c).
DATA_TEST_CASE("RollingBallTests.BallsRollOnTheFloor")
{
    auto space = std::make_shared<ArenaSpace>();
    auto runtime = CreateRuntime(space, Difficulty::Normal);
    for (int i = 0; i < RollTicks; i++)
    {
        runtime->Update(Tick);
    }

    std::vector<std::shared_ptr<MdkObject>> balls;
    std::copy_if(runtime->Objects.begin(), runtime->Objects.end(), std::back_inserter(balls),
        [](const auto& o) { return o->TypeName == Ball && !o->Dead; });
    CHECK_FALSE(balls.empty());
    for (const auto& ball : balls)
    {
        // The model's lowest vertex as drawn.
        REQUIRE(ball->Model != nullptr);
        auto transform = ball->Transform();
        float lowest = std::numeric_limits<float>::infinity();
        for (const auto& part : ball->Model->PartList)
        {
            for (const auto& v : part->Vertices)
            {
                lowest = std::min(lowest, Vector3::Transform(v, transform).Z);
            }
        }

        Vector3 up(0.0f, 0.0f, RayReach);
        Vector3 floor;
        CHECK(space->Floor(ball->Position + up, ball->Position - up, floor));
        CHECK(lowest >= floor.Z - FloorTolerance);
        CHECK(lowest <= floor.Z + FloorTolerance);
    }
}

// A ball touching Kurt runs hurt_kurt 4 (0xcdb4): 2/3 on easy, doubled on hard, then knocks him
// down (set_hurt_flash 1), which makes him invulnerable.
DATA_TEST_CASE("RollingBallTests.BallHurtsKurt")
{
    const std::pair<Difficulty, int> cases[] = {{Difficulty::Easy, 2}, {Difficulty::Normal, 4}, {Difficulty::Hard, 8}};
    for (const auto& [difficulty, damage] : cases)
    {
        CAPTURE(damage);
        auto runtime = CreateRuntime(std::make_shared<ArenaSpace>(), difficulty);
        runtime->Update(Tick);
        auto grunt = std::find_if(runtime->Objects.begin(), runtime->Objects.end(),
            [](const auto& o) { return o->TypeName == Grunt && o->Arena == Room; });
        REQUIRE(grunt != runtime->Objects.end());
        runtime->Kill(**grunt);
        runtime->Kurt->Teleport(InPath, 0.0f);

        int start = runtime->Kurt->Health;
        for (int i = 0; i < HitTicks && runtime->Kurt->Health == start; i++)
        {
            Input idle;
            runtime->Kurt->Update(idle, Tick);
            runtime->Update(Tick);
        }

        CHECK(runtime->Kurt->Health == start - damage);
    }
}
