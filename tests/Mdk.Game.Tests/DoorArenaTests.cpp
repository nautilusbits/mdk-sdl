#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "Asserts.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Formats/MdkData.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/ObjectBehaviors.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Shared/GameData.h"

using namespace Mdk;
using Mdk::Engine::Audio::AudioDevice;
using Mdk::Engine::Audio::Output;
using Mdk::Formats::Bni;
using Mdk::Formats::Cmi;
using Mdk::Game::Audio::SoundMixer;
using Mdk::Game::Collision::ArenaSpace;
using Mdk::Game::Level::LevelData;
using Mdk::Game::Level::TriangleGroups;
using Mdk::Game::Objects::MdkObject;
using Mdk::Game::Scripts::ObjectBehaviors;
using Mdk::Game::Scripts::ScriptRuntime;
using Mdk::Tests::GameData;

// Found by playtest: on level 7, back from DANT_2 to the start tunnel CDANT_1, the doorway
// showed the sky. The tunnel's door stayed in CDANT_1; once that arena was put away the door froze
// shut, undrawn and not solid, so it never opened to show the tunnel. The original moves a door into
// Kurt's arena when he is on its other side (0x43cc68), and into an arena being loaded from its
// neighbours (arena_load 0x419d00), turned around (0x43ca00).

namespace
{
    constexpr int Level = 7;
    constexpr const char* Tunnel = "CDANT_1";
    constexpr const char* Room = "DANT_2";
    constexpr const char* Corridor = "CDANT_2";
    // The tunnel's door into DANT_2 (spawn_connector of CDANT_1), at yaw 90.
    constexpr Vector3 TunnelDoor{0.f, 673.f, -3.f};
    constexpr float DoorYaw = 90.f;
    constexpr float TurnedYaw = 270.f;
    constexpr float Floor = -3.f;
    // The room's floor a little in, and far enough for the door to close.
    constexpr Vector3 NearDoor{0.f, 688.f, -5.f};
    constexpr Vector3 FarFromDoor{0.f, 750.f, -8.f};
    constexpr Vector3 CorridorSpot{100.f, 990.f, -6.f};
    constexpr int SecondTicks = 30;
    constexpr const char* Far = "DANT_10";
    constexpr Vector3 FarSpot{495.f, 5005.f, 24.f};
    constexpr Vector3 TunnelSpot{0.f, 485.f, 19.f};
    constexpr Vector3 BeforeDoor{0.f, 660.f, Floor};

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ScriptRuntime> CreateRuntime()
    {
        const auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, Level);
        auto cmi = Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + std::to_string(Level) + "/LEVEL" + std::to_string(Level) + ".CMI"));
        auto sprites = Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        auto space = std::make_shared<ArenaSpace>();
        for (const auto& arena : level->Arenas)
        {
            if (!level->IsReachable(arena->Name))
            {
                continue;
            }

            space->Add(arena);
        }

        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<Game::Kurt::Kurt>(*space, *mixer, [](Game::Kurt::Kurt::State) { return 1; });
        return std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    }

    // Puts Kurt somewhere and runs that many ticks.
    void StandAt(ScriptRuntime& runtime, Vector3 feet, int ticks)
    {
        for (auto i = 0; i < ticks; i++)
        {
            runtime.Kurt->Teleport(feet, DoorYaw);
            runtime.Update(ScriptRuntime::Tick);
        }
    }

    // C#'s First(): throws when there's none.
    std::shared_ptr<MdkObject> DoorAt(ScriptRuntime& runtime, Vector3 position)
    {
        for (const auto& o : runtime.Objects)
        {
            if (!o->Dead && (o->Flags & MdkObject::FlagDoor) != 0 && Vector3::Distance(o->Position, position) < 1.f)
            {
                return o;
            }
        }

        FAIL("No door at the position");
        return nullptr;
    }

    bool Contains(const std::vector<std::string>& names, const std::string& name)
    {
        return std::find(names.begin(), names.end(), name) != names.end();
    }
}

// Kurt goes through the tunnel's door into DANT_2, away until it shuts, and back: it
// opens again and shows the tunnel.
DATA_TEST_CASE("DoorArenaTests.DoorShowsTunnelOnTheWayBack")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Tunnel, Vector3(0.f, 655.f, Floor), DoorYaw);
    for (auto y = 655.f; y < NearDoor.Y; y += 1.f)
    {
        StandAt(*runtime, Vector3(0.f, y, Floor), 1);
    }

    CHECK(std::string(Room) == runtime->CurrentArena);

    // Into the room, out of the door's reach: it closes and the tunnel goes.
    StandAt(*runtime, FarFromDoor, 3 * SecondTicks);
    CHECK_FALSE(Contains(runtime->DrawnArenas(), Tunnel));

    // Back to the door.
    StandAt(*runtime, NearDoor, 2 * SecondTicks);

    auto door = DoorAt(*runtime, TunnelDoor);
    CHECK(std::string(Room) == door->Arena);
    CHECK(std::string(Tunnel) == door->Connects);
    CHECK(Contains(runtime->DrawnArenas(), Tunnel));
}

// Loading DANT_2 ahead (from CDANT_2) pulls the tunnel's door into it, turned around.
DATA_TEST_CASE("DoorArenaTests.LoadedArenaPullsItsDoors")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Tunnel, Vector3(0.f, 655.f, Floor), DoorYaw);
    StandAt(*runtime, Vector3(0.f, 655.f, Floor), 1);
    runtime->TeleportKurt(Corridor, CorridorSpot, 0.f);
    StandAt(*runtime, CorridorSpot, 1);

    // DANT_2 stays loaded from the tunnel; a trigger clears it.
    runtime->ShowArena("");
    runtime->PreloadArena(Room);

    auto door = DoorAt(*runtime, TunnelDoor);
    CHECK(std::string(Room) == door->Arena);
    CHECK(std::string(Tunnel) == door->Connects);
    CHECK_NEAR(TurnedYaw, door->Yaw, 0.01f);
}

// A teleport into a corridor not loaded loads the last arena (DTI order) leading to it,
// which becomes the active second (0x41bce4): from DANT_10 into CDANT_1, DANT_2 behind its door.
DATA_TEST_CASE("DoorArenaTests.TeleportLoadsCorridorArena")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Far, FarSpot, 0.f);
    StandAt(*runtime, FarSpot, 1);

    runtime->TeleportKurt(Tunnel, TunnelSpot, DoorYaw);
    StandAt(*runtime, TunnelSpot, 1);

    CHECK(std::string(Room) == runtime->SecondArena);
    CHECK(Contains(runtime->DrawnArenas(), Room));
}

// A teleport into a corridor of the loaded arena keeps the second arena (0x41bce4):
// from DANT_2 into CDANT_2, none.
DATA_TEST_CASE("DoorArenaTests.TeleportKeepsLoadedCorridor")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Room, FarFromDoor, DoorYaw);
    StandAt(*runtime, FarFromDoor, 1);

    runtime->TeleportKurt(Corridor, CorridorSpot, 0.f);
    StandAt(*runtime, CorridorSpot, 1);

    CHECK("" == runtime->SecondArena);
    CHECK(std::vector<std::string>{Corridor} == runtime->DrawnArenas());
}

// The soak tour shuts the doors before it teleports: a door left open freezes with its
// arena and, back, closes onto an arena no longer drawn.
DATA_TEST_CASE("DoorArenaTests.ShutDoorsLeavesNoneOpen")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Tunnel, BeforeDoor, DoorYaw);
    StandAt(*runtime, BeforeDoor, SecondTicks);
    auto door = DoorAt(*runtime, TunnelDoor);
    CHECK(0 == (door->DoorState & ObjectBehaviors::DoorClosed));

    runtime->ShutDoors();

    CHECK(0 != (door->DoorState & ObjectBehaviors::DoorClosed));
    CHECK(door->IsAnimationDone());
}
