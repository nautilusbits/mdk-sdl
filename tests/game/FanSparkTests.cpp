#include <limits>
#include <memory>
#include <optional>
#include <string>

#include <doctest.h>

#include "core/DotNet.h"
#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/kurt/Kurt.h"
#include "game/level/LevelData.h"
#include "game/level/TriangleGroups.h"
#include "game/scripts/Debris.h"
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

// Found by playtest: nothing showed a fan blowing on level 6. Its fire sparks start a
// quarter unit below the fan's grate (0x414230) and bounced under it until they died. The original
// puts a piece that hits something at the contact (0x4061d8), and a segment starting on a plane
// crosses nothing (0x421470), so the updraft carries the spark through on the next tick.

namespace
{
    constexpr int Level = 6;
    const std::string Arena = "OLYM_6";
    // The arena's fan (hotspot 1): its bottom, 68 units below its top.
    constexpr float FanBottom = -2209.f;
    constexpr Vector3 NearFan(-1945.f, 2900.f, -2211.f);
    constexpr float Yaw = 90.f;
    // Sparks rising through the box get this far above the grate.
    constexpr float Risen = 20.f;
    constexpr int Seconds = 10;
    constexpr int SecondTicks = 30;

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
            if (!level->IsReachable(arena->Name))
            {
                continue;
            }

            space->Add(arena);
        }

        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, [](auto) { return 1; });
        return std::make_shared<ScriptRuntime>(level, cmi, sprites, space, std::make_shared<TriangleGroups>(), mixer, kurt);
    }
}

DATA_TEST_CASE("FanSparkTests.FanSparksRiseThroughTheGrate")
{
    auto runtime = CreateRuntime();
    runtime->TeleportKurt(Arena, NearFan, Yaw);
    auto highest = std::numeric_limits<float>::lowest();
    for (int i = 0; i < Seconds * SecondTicks; i++)
    {
        runtime->Kurt->Teleport(NearFan, Yaw);
        runtime->Update(ScriptRuntime::Tick);
        for (const auto& spark : runtime->Debris->Pieces())
        {
            if (!spark->IsSpark() || spark->Arena != Arena)
            {
                continue;
            }

            highest = MathF::Max(highest, spark->Center.Z);
        }
    }

    INFO("highest spark " << highest);
    CHECK(highest > FanBottom + Risen);
}
