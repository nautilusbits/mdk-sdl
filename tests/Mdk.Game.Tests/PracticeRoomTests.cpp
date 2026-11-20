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

// Level 7's practice room (DANT_2, docs/practice_room.md of the Godot port).
namespace
{
    constexpr int Level = 7;
    constexpr const char* Room = "DANT_2";
    constexpr const char* Crate = "XBANG";
    constexpr const char* Target = "XGTARG";
    constexpr float Tick = 1.0f / 30.0f;
    constexpr int SettleTicks = 30;
    constexpr int LandingTicks = 300;
    constexpr float Tolerance = 1.0f;
    const Vector3 KurtSpot{0.0f, 900.0f, -27.0f};

    // The pedestal tops XGEN's three targets fly to (move_to_point at 0x5458, 0x5490, 0x54c8).
    const Vector3 Pedestals[] = {{-17.0f, 941.0f, -20.0f}, {-24.0f, 959.0f, -15.0f}, {-23.0f, 977.0f, -10.0f}};

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
        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
        return std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    }

    void Run(ScriptRuntime& runtime, int ticks)
    {
        for (int i = 0; i < ticks; i++)
        {
            runtime.Update(Tick);
        }
    }
}

// Shooting the crate makes XGEN spit three targets; each lands on its pedestal (move_to_point's
// Manhattan step keeps them high enough to clear the glass sides).
DATA_TEST_CASE("PracticeRoomTests.TargetsLandOnThePedestals")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Room, KurtSpot, 90.0f);
    Run(*runtime, SettleTicks);
    auto crate = std::find_if(runtime->Objects.begin(), runtime->Objects.end(),
        [](const auto& o) { return o->TypeName == Crate && o->Arena == Room; });
    REQUIRE(crate != runtime->Objects.end());
    runtime->Kill(**crate);

    Run(*runtime, LandingTicks);

    std::vector<std::shared_ptr<MdkObject>> targets;
    std::copy_if(runtime->Objects.begin(), runtime->Objects.end(), std::back_inserter(targets),
        [](const auto& o) { return o->TypeName == Target && !o->Dead; });
    CHECK(targets.size() == std::size(Pedestals));
    for (const auto& pedestal : Pedestals)
    {
        CAPTURE(pedestal.Y);
        CHECK(std::any_of(targets.begin(), targets.end(),
            [&](const auto& t) { return Vector3::Distance(t->Position, pedestal) < Tolerance; }));
    }
}
