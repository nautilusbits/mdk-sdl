#include <memory>
#include <optional>
#include <string>

#include <doctest.h>

#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "formats/Arena.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/collision/Bsp.h"
#include "game/kurt/Kurt.h"
#include "game/level/LevelData.h"
#include "game/level/TriangleGroups.h"
#include "game/scripts/ScriptRuntime.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// Found by playtest: Kurt's arena came from the arena boxes, so on level 4's MEAT_7 floor,
// just before the doorway to CMEAT_7 (connection 1012, the plane y = 14822), he was already in the
// corridor; the scripts then dropped MEAT_7 from the solid arenas and he fell through its floor.
// The original switches only when his move crosses the doorway (0x41c550).

namespace
{
    constexpr int Level = 4;
    constexpr const char* Room = "MEAT_7";
    constexpr const char* Corridor = "CMEAT_7";
    constexpr float Doorway = 14822.f;
    constexpr float Floor = 21.f;

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
        auto cmi = Formats::Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".CMI"));
        auto sprites = Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        for (const auto& arena : level->Arenas)
        {
            if (level->IsReachable(arena->Name))
            {
                space->Add(arena);
            }
        }

        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
        return std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    }

    std::shared_ptr<ScriptRuntime> CreateRuntime() { return CreateRuntime(std::make_shared<ArenaSpace>()); }

    // Puts Kurt at a y on the floor by the doorway and runs a tick.
    void StepTo(ScriptRuntime& runtime, float y)
    {
        runtime.Kurt->Teleport(Vector3(0.f, y, Floor), runtime.Kurt->Yaw);
        runtime.Update(ScriptRuntime::Tick);
    }

    bool Contains(const std::vector<Bsp*>& arenas, const std::string& name)
    {
        for (auto* b : arenas)
        {
            if (b->Arena->Name == name)
            {
                return true;
            }
        }

        return false;
    }
}

DATA_TEST_CASE("ArenaSwitchTests.ArenaSwitchesAtDoorway")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Room, Vector3(0.f, Doorway - 100.f, Floor), 90.f);
    runtime->Update(ScriptRuntime::Tick);

    // Inside the corridor's box, but not through the doorway yet.
    StepTo(*runtime, Doorway - 1.f);
    CHECK(Room == runtime->CurrentArena);

    // Through: the room stays as the active second arena.
    StepTo(*runtime, Doorway + 3.f);
    CHECK(Corridor == runtime->CurrentArena);
    CHECK(Room == runtime->SecondArena);
    CHECK(runtime->SecondActive);

    // And back.
    StepTo(*runtime, Doorway - 1.f);
    CHECK(Room == runtime->CurrentArena);
    CHECK(Corridor == runtime->SecondArena);
}

// The first tick has no move (script_runtime.gd): no velocity from the origin.
DATA_TEST_CASE("ArenaSwitchTests.FirstTickHasNoMove")
{
    auto runtime = CreateRuntime();
    runtime->Kurt->Teleport(Vector3(0.f, Doorway - 100.f, Floor), 90.f);

    runtime->Update(ScriptRuntime::Tick);

    CHECK(Vector3::Zero() == runtime->KurtVelocity);
}

// Found by the soak tour: teleported onto a doorway before the first tick, Kurt was
// pushed through it, but the first tick forgot the teleport and took his new place as the
// previous one; he stayed in the room while in the corridor and fell out when it went.
DATA_TEST_CASE("ArenaSwitchTests.FirstTickKeepsTeleport")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Room, Vector3(0.f, Doorway, Floor), 90.f);

    StepTo(*runtime, Doorway + 3.f);

    CHECK(Corridor == runtime->CurrentArena);
}

// An arena no connection leads to isn't solid until a script teleports Kurt into it
// (level.gd enter_arena): then it's in Kurt's space.
DATA_TEST_CASE("ArenaSwitchTests.TeleportMakesUnreachableArenaSolid")
{
    auto space = std::make_shared<ArenaSpace>();
    auto runtime = CreateRuntime(space);
    std::shared_ptr<Formats::Arena> arena;
    for (const auto& a : runtime->Level->Arenas)
    {
        if (!runtime->Level->IsReachable(a->Name) && !a->Vertices.empty())
        {
            arena = a;
            break;
        }
    }

    REQUIRE(arena != nullptr);
    auto min = arena->Vertices[0];
    auto max = arena->Vertices[0];
    for (const auto& v : arena->Vertices)
    {
        min = Vector3::Min(min, v);
        max = Vector3::Max(max, v);
    }

    auto center = (min + max) / 2.f;
    CHECK_FALSE(Contains(space->At(center), arena->Name));

    runtime->TeleportKurt(arena->Name, center, 0.f);

    CHECK(Contains(space->At(center), arena->Name));
}
