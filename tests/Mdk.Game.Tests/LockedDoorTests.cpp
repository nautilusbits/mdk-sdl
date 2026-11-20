#include <doctest.h>

#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Inventory.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/Items.h"
#include "Mdk.Game/Scripts/ObjectBehaviors.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Shared/GameData.h"

#include <memory>
#include <stdexcept>
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
using Mdk::Game::Kurt::Inventory;
using Mdk::Game::Kurt::Mortality;
using Mdk::Tests::GameData;

// Doors locked until nuked: their scripts wait on flag word 5 bit 6 (their own door state,
// locked), then set or clear an arena flag (the key stops respawning). While word 5 read the
// linked object's flags they went on at once.
namespace
{
    constexpr int SecondTicks = 30;
    constexpr int SettleSeconds = 2;
    constexpr int NukeSeconds = 10;
    constexpr float DoorReach = 2.f;
    constexpr float Halfway = 0.5f;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ScriptRuntime> CreateRuntime(int number)
    {
        auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, number);
        auto name = std::to_string(number);
        auto cmi = Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + name + "/LEVEL" + name + ".CMI"));
        auto sprites = Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        auto space = std::make_shared<ArenaSpace>();
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

    void StandAt(ScriptRuntime& runtime, Vector3 feet, float yaw, int ticks)
    {
        for (int i = 0; i < ticks; i++)
        {
            runtime.Kurt->Teleport(feet, yaw);
            runtime.Update(ScriptRuntime::Tick);
        }
    }

    struct DoorCase
    {
        int Level;
        const char* Arena;
        float X, Y, Z, KurtX, KurtY, Yaw;
    };

    const DoorCase Doors[] = {
        {6, "OLYM_1", -1142.f, -931.f, -73.f, -1142.f, -946.f, 90.f},
        {7, "DANT_1", 0.f, 481.f, 15.f, 0.f, 466.f, 90.f},
        {7, "DANT_5", 252.f, 3633.f, -71.f, 252.f, 3618.f, 90.f},
        {7, "CDANT_5", 252.f, 3633.f, -71.f, 252.f, 3648.f, 270.f},
        {7, "DANT_9", 276.f, 4782.f, -37.f, 261.f, 4782.f, 0.f},
        {8, "GUNT_1", -68.f, 93.f, 12.f, -53.f, 93.f, 180.f},
        {8, "GUNT_6", 75.f, 2571.f, -269.f, 90.f, 2571.f, 180.f},
        {8, "CGUNT_6", 75.f, 2571.f, -269.f, 60.f, 2571.f, 0.f},
    };
}

// Kurt stands 15 units before the door, facing it: it stays locked and its script waits. He
// throws the key; the nuke unlocks the door, its script goes on, and the door opens as he steps
// closer. (CDANT_9's door tests word 5 bit 64 & 31 = 0, "open": it goes on at once, in the
// original too.)
DATA_TEST_CASE("LockedDoorTests.NukeUnlocksDoor")
{
    for (const auto& c : Doors)
    {
        CAPTURE(c.Arena);
        auto runtime = CreateRuntime(c.Level);
        runtime->Kurt->Mortality = Mortality::God;
        Vector3 at(c.X, c.Y, c.Z);
        Vector3 feet(c.KurtX, c.KurtY, c.Z);
        runtime->TeleportKurt(c.Arena, feet, c.Yaw);
        StandAt(*runtime, feet, c.Yaw, SettleSeconds * SecondTicks);

        // Locked, its script waiting.
        std::shared_ptr<MdkObject> door;
        for (auto& o : runtime->Objects)
        {
            if (!o->Dead && (o->Flags & MdkObject::FlagDoor) != 0 && Vector3::Distance(o->Position, at) < DoorReach)
            {
                door = o;
                break;
            }
        }

        REQUIRE(door != nullptr);
        auto waiting = door->Restart;
        StandAt(*runtime, feet, c.Yaw, SettleSeconds * SecondTicks);
        CHECK(door->Restart == waiting);
        CHECK((door->DoorState & ObjectBehaviors::DoorLocked) != 0);

        // The nuke.
        auto health = Inventory::MaxHealth;
        runtime->Kurt->Inventory->Collect("SW_KEY", health);
        runtime->Items->UseItem();
        StandAt(*runtime, feet, c.Yaw, NukeSeconds * SecondTicks);
        CHECK(door->Restart != waiting);
        CHECK((door->DoorState & ObjectBehaviors::DoorLocked) == 0);

        // Closer: it opens.
        auto closer = Vector3::Lerp(feet, at, Halfway);
        StandAt(*runtime, closer, c.Yaw, SettleSeconds * SecondTicks);
        CHECK((door->DoorState & (ObjectBehaviors::DoorOpen | ObjectBehaviors::DoorOpening)) != 0);
    }
}
