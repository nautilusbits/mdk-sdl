#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// LEVEL7's flying ships (XE) carry a bomb (XBANG) or soldiers (XG) on a chain (spawn_chain).
// The ship has a single reference point, the hook under it: the cargo hangs there, below the ship,
// not from the ship's centre (inside it).
namespace
{
    constexpr float Tick = 1.f / 30.f;
    // DANT_10's bomb ship script: follow_path, then spawn_chain 1, "XBANG".
    constexpr int BombShip = 0x20fbc;
    // DANT_10's soldier ship script: follow_path, then spawn_chain 3, "XG".
    constexpr int SoldierShip = 0x20fe6;
    // A hanging soldier (XG, about 8 high) holds the feet of the one above: about 12 apart.
    constexpr float SoldierSpacing = 8.f;
    const Vector3 ShipStart(1301.f, 5018.f, 315.f);

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ScriptRuntime> CreateRuntime()
    {
        auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, 7);
        auto cmi = Formats::Cmi::Load(data.PathOf("TRAVERSE/LEVEL7/LEVEL7.CMI"));
        auto sprites = Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
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
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
        auto runtime = std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
        runtime->TeleportKurt("DANT_10", Vector3(545.89f, 5023.39f, 184.54f), 0.f);
        runtime->Update(Tick);
        return runtime;
    }

    std::shared_ptr<MdkObject> AnyoneInDant10(ScriptRuntime& runtime)
    {
        for (const auto& o : runtime.Objects)
        {
            if (o->Arena == "DANT_10" && !o->Dead)
            {
                return o;
            }
        }

        return nullptr;
    }
}

DATA_TEST_CASE("ShipCargoTests.SoldiersHangInALineBelowTheShip")
{
    auto runtime = CreateRuntime();
    auto anyone = AnyoneInDant10(*runtime);
    REQUIRE(anyone != nullptr);
    auto ship = runtime->Spawn(*anyone, "XE", ShipStart, 180.f, -1, SoldierShip, ScriptRuntime::Spawning::Plain);
    REQUIRE(ship != nullptr);
    for (int i = 0; i < 10; i++)
    {
        runtime->Update(Tick);
    }

    // The chain hangs by the hand its animation raises, not the rest pose's (which bunched it up).
    std::vector<std::shared_ptr<MdkObject>> soldiers;
    for (const auto& o : runtime->Objects)
    {
        if (o->TypeName == "XG" && o->Leader == ship)
        {
            soldiers.push_back(o);
        }
    }

    std::stable_sort(soldiers.begin(), soldiers.end(), [](const auto& a, const auto& b) { return a->InstanceId < b->InstanceId; });
    REQUIRE(soldiers.size() == 3);
    float above = ship->Position.Z;
    for (const auto& soldier : soldiers)
    {
        INFO("soldier at ", soldier->Position.Z, ", the one above at ", above);
        CHECK(above - soldier->Position.Z > SoldierSpacing);
        above = soldier->Position.Z;
    }
}

DATA_TEST_CASE("ShipCargoTests.TheBombHangsFromTheShipsHook")
{
    auto runtime = CreateRuntime();
    auto anyone = AnyoneInDant10(*runtime);
    REQUIRE(anyone != nullptr);
    auto ship = runtime->Spawn(*anyone, "XE", ShipStart, 180.f, -1, BombShip, ScriptRuntime::Spawning::Plain);
    REQUIRE(ship != nullptr);
    for (int i = 0; i < 9; i++)
    {
        runtime->Update(Tick);
    }

    auto before = ship->Position;
    runtime->Update(Tick);

    // The bomb, newer, runs before the ship (0x45fd4c): it hangs where the hook was a tick ago.
    std::shared_ptr<MdkObject> bomb;
    for (const auto& o : runtime->Objects)
    {
        if (o->TypeName == "XBANG" && o->Leader == ship)
        {
            bomb = o;
            break;
        }
    }

    REQUIRE(bomb != nullptr);
    auto hook = ship->ReferencePoint(0) - (ship->Position - before);
    REQUIRE(ship->Model != nullptr);
    CHECK(ship->Model->ReferencePoints.size() == 1);
    CHECK(Vector3::Distance(bomb->ReferencePoint(0), hook) < 0.5f);
    CHECK(bomb->Position.Z < ship->Position.Z - 5.f);
}
