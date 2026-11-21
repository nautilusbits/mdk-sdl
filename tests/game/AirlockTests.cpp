#include <memory>
#include <optional>
#include <string>

#include <doctest.h>

#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/kurt/Kurt.h"
#include "game/level/LevelData.h"
#include "game/level/TriangleGroups.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/Items.h"
#include "game/scripts/ObjectBehaviors.h"
#include "game/scripts/ScriptRuntime.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;
using Mdk::Game::Kurt::Inventory;
using Mdk::Game::Kurt::Mortality;

// Found by playtest: LEVEL4's sub airlock kept Kurt in. The nuke opens the hatch into
// it; past its door #1998 that door waits until it is shut (flag word 5 bit 3, its own door
// state), locks and opens #1999 to MEAT_6. Word 5 read the linked object's flags, so #1999
// never opened.
//   MEAT_5 ──hatch #1000 (y 13353)──► airlock ──#1998 (y 13375)──► CMEAT_5 ──#1999 (y 13406)──► MEAT_6

namespace
{
    constexpr int Level = 4;
    constexpr const char* Arena = "MEAT_5";
    constexpr float X = -126.f;
    constexpr float Floor = -1989.f;
    constexpr float Ahead = 90.f;
    // In the nuke's box (y 13306…13351), the key thrown along +y lands in it.
    constexpr float NukeSpot = 13310.f;
    // In the airlock (y 13354…13376), where the hatch shuts behind Kurt and unlocks #1998.
    constexpr float Airlock = 13365.f;
    // Past #1998, before #1999 (within its distance 10).
    constexpr float BeforeLastDoor = 13398.f;
    constexpr int OutDoor = 1999;
    constexpr int SecondTicks = 30;
    constexpr int NukeSeconds = 10;
    constexpr int WaitSeconds = 5;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ScriptRuntime> CreateRuntime()
    {
        auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, Level);
        auto number = std::to_string(Level);
        auto cmi = Formats::Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".CMI"));
        auto sprites = Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        auto space = std::make_shared<ArenaSpace>();
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

    // Holds Kurt at y for that many ticks.
    void StandAt(ScriptRuntime& runtime, float y, int ticks)
    {
        for (int i = 0; i < ticks; i++)
        {
            runtime.Kurt->Teleport(Vector3(X, y, Floor), Ahead);
            runtime.Update(ScriptRuntime::Tick);
        }
    }

    // Walks Kurt along +y, a unit a tick.
    void Walk(ScriptRuntime& runtime, float from, float to)
    {
        for (float y = from; y < to; y += 1.f)
        {
            StandAt(runtime, y, 1);
        }
    }
}

DATA_TEST_CASE("AirlockTests.AirlockOpensToMeat6")
{
    auto runtime = CreateRuntime();
    runtime->Kurt->Mortality = Mortality::God;
    runtime->TeleportKurt(Arena, Vector3(X, NukeSpot, Floor), Ahead);
    StandAt(*runtime, NukeSpot, 1);

    // The nuke goes off by the hatch.
    int health = Inventory::MaxHealth;
    runtime->Kurt->Inventory->Collect("SW_KEY", health);
    runtime->Items->UseItem();
    StandAt(*runtime, NukeSpot, NukeSeconds * SecondTicks);

    // Through the hatch into the airlock; it shuts behind him.
    Walk(*runtime, NukeSpot, Airlock);
    StandAt(*runtime, Airlock, WaitSeconds * SecondTicks);

    // Through #1998, then wait for it to shut.
    Walk(*runtime, Airlock, BeforeLastDoor);
    StandAt(*runtime, BeforeLastDoor, WaitSeconds * SecondTicks);

    std::shared_ptr<MdkObject> door;
    for (const auto& o : runtime->Objects)
    {
        if (!o->Dead && (o->Flags & MdkObject::FlagDoor) != 0 && o->InstanceId == OutDoor)
        {
            door = o;
            break;
        }
    }

    REQUIRE(door != nullptr);
    CHECK(0 != (door->DoorState & (ObjectBehaviors::DoorOpen | ObjectBehaviors::DoorOpening)));
}
