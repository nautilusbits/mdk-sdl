#include <doctest.h>

#include "engine/audio/AudioDevice.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/kurt/Kurt.h"
#include "game/level/LevelData.h"
#include "game/level/TriangleGroups.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/ScriptRuntime.h"
#include "shared/GameData.h"

#include <memory>
#include <string>

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using Mdk::Formats::Bni;
using Mdk::Formats::Cmi;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;

// Found by playtest: in LEVEL8's GUNT_10 a winged alien XG walked into the arena's centre hole
// (x -61...-25 at y 3327; the X10_CAP platform over it stops Kurt only) and slid out of sight
// below the -400 floor, so the script waiting for the last XG never went on. The original's
// if_no_floor_at (0x45694b, probe 0x46046c) tests a point dx ahead the way add_vel_local does
// (-dx along the yaw: the scripts' -20 is 20 units ahead), from z + 3 down to z - 3 (or
// z + depth), up-facing faces only; the XG turns away at the edge.
namespace
{
    constexpr int Level = 8;
    const std::string Arena = "GUNT_10";
    const std::string Alien = "XG";
    constexpr float Tick = 1.f / 30.f;
    constexpr int WalkTicks = 15 * 30;
    // The ring around the hole slopes down to about -410 at its rim.
    constexpr float LowestFloor = -410.f;
    // How far the feet may be from the floor (walking bumps).
    constexpr float FeetReach = 5.f;
    // The hole's west edge at y 3327.
    constexpr float HoleWest = -61.f;
    // The XG's longest edge check (if_no_floor_at -20).
    constexpr float EdgeReach = 20.f;

    // The XG's walk loop (0x22134): edge and wall checks, turns.
    constexpr int WalkScript = 0x22134;

    // West of the hole, facing it (yaw 0 is +x).
    const Vector3 Start(-85.f, 3327.f, -399.f);
    // Behind the XG, out of its sight.
    const Vector3 KurtSpot(-150.f, 3327.f, -399.f);

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ScriptRuntime> CreateRuntime(const std::shared_ptr<ArenaSpace>& space)
    {
        auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, Level);
        auto number = std::to_string(Level);
        auto cmi = Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".CMI"));
        auto sprites = Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        for (auto& arena : level->Arenas)
        {
            if (!level->IsReachable(arena->Name))
            {
                continue;
            }

            space->Add(arena);
        }

        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<Mdk::Game::Kurt::Kurt>(*space, *mixer, [](auto) { return 1; });
        return std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    }

    // An up-facing face close above or below the feet.
    bool OnFloor(ArenaSpace& space, Vector3 feet)
    {
        Vector3 reach(0.f, 0.f, FeetReach);
        Vector3 floor;
        return space.Floor(feet + reach, feet - reach, floor);
    }
}

DATA_TEST_CASE("NoFloorTests.WalkerKeepsToTheFloor")
{
    auto space = std::make_shared<ArenaSpace>();
    auto runtime = CreateRuntime(space);
    runtime->TeleportKurt(Arena, KurtSpot, 0.f);
    runtime->Update(Tick);
    auto alien = runtime->Spawn(*runtime->GetArenaState(Arena)->Controller, Alien, Start, 0.f, -1, WalkScript, ScriptRuntime::Spawning::Plain);
    REQUIRE(alien != nullptr);
    alien->Flags |= MdkObject::FlagGravity | MdkObject::FlagCollides;

    // The pace 0x223a3 sets before the walk.
    alien->MaxSpeed = 40.f;
    alien->Acceleration = 25.f;
    alien->Deceleration = 30.f;

    // Walk for a while: it reaches the edge, never sinks below the ring's floor and ends on it.
    auto farthest = alien->Position.X;
    auto lowest = alien->Position.Z;
    for (int i = 0; i < WalkTicks; i++)
    {
        runtime->Update(Tick);
        farthest = MathF::Max(farthest, alien->Position.X);
        lowest = MathF::Min(lowest, alien->Position.Z);
    }

    CHECK_MESSAGE(farthest > HoleWest - EdgeReach, "the XG didn't reach the edge: ", farthest);
    CHECK_MESSAGE(lowest > LowestFloor - FeetReach, "the XG fell off the floor: z ", lowest);
    CHECK_MESSAGE(OnFloor(*space, alien->Position), "the XG ended off the floor");
}
