#include <doctest.h>

#include "engine/audio/AudioDevice.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "game/Angles.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/kurt/Kurt.h"
#include "game/level/ArenaStops.h"
#include "game/level/LevelData.h"
#include "game/level/TriangleGroups.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/ScriptRuntime.h"
#include "game/scripts/SniperRounds.h"
#include "shared/GameData.h"

#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Formats;
using namespace Mdk::Game;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;

// Onehit spares scripted targets: level 8's XEARTH in GUNT_4 (its script resets its health and
// counts hits in arena variable 0: 6 a hit, 320 below 9950 hit points; at 720 it sinks and
// explodes, breaking the glass below) and shatterable groups (level 3's HMO_8) take the
// original's damage.
namespace
{
    constexpr int EarthLevel = 8;
    constexpr int GroupLevel = 3;
    constexpr float Tick = 1.f / 30.f;
    constexpr int SettleTicks = 30;
    const std::string EarthArena = "GUNT_4";
    const std::string GroupArena = "HMO_8";
    const std::string Earth = "XEARTH";
    constexpr int BulletRound = 0;
    constexpr int Shots = 3;
    constexpr int ShotTicks = 10;
    // Eyes this far from a target, tried around it until one sees it.
    constexpr float EyeRange = 30.f;
    const std::array<Vector3, 6> Around{
        Vector3::UnitX(), -Vector3::UnitX(), Vector3::UnitY(), -Vector3::UnitY(), Vector3::UnitZ(), -Vector3::UnitZ(),
    };

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    void Run(ScriptRuntime& runtime, int ticks)
    {
        for (int i = 0; i < ticks; i++)
        {
            runtime.Update(Tick);
        }
    }

    // Kurt in arena of level number, its scripts started.
    std::shared_ptr<ScriptRuntime> CreateRuntime(int number, const std::string& arena, Lethality lethality)
    {
        auto& data = *GameData::Data();
        auto level = std::make_shared<LevelData>(data, number);
        auto name = std::to_string(number);
        auto cmi = Cmi::Load(data.PathOf("TRAVERSE/LEVEL" + name + "/LEVEL" + name + ".CMI"));
        auto sprites = Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        auto space = std::make_shared<ArenaSpace>();
        auto groups = std::make_shared<TriangleGroups>();
        for (auto& arenaData : level->Arenas)
        {
            if (!level->IsReachable(arenaData->Name))
            {
                continue;
            }

            space->Add(arenaData);
            groups->Add(arenaData);
        }

        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<Mdk::Game::Kurt::Kurt>(*space, *mixer, [](auto) { return 1; });
        auto runtime = std::make_shared<ScriptRuntime>(level, cmi, sprites, space, groups, mixer, kurt);
        runtime->TeleportKurt(arena, ArenaStops::Find(*level, *space, arena).value(), 0.f);
        Run(*runtime, SettleTicks);
        runtime->Lethality = lethality;
        return runtime;
    }

    // A point that sees target (the ray's first hit is group, 0 for none), or none.
    std::optional<Vector3> EyeOn(ScriptRuntime& runtime, Vector3 target, int group)
    {
        for (auto way : Around)
        {
            auto eye = target + way * EyeRange;
            auto hit = runtime.Raycast(eye, target);
            if (group == 0 ? !hit.has_value() : hit.has_value() && hit->Group == group)
            {
                return eye;
            }
        }

        return std::nullopt;
    }

    Vector3 Middle(const Box& box) { return (box.Min + box.Max) * 0.5f; }

    // Bullets from eye at target, one at a time.
    void Shoot(ScriptRuntime& runtime, Vector3 eye, Vector3 target)
    {
        auto to = target - eye;
        auto yaw = RadiansToDegrees(MathF::Atan2(to.Y, to.X));
        auto pitch = -RadiansToDegrees(MathF::Atan2(to.Z, Vector2(to.X, to.Y).Length()));
        for (int i = 0; i < Shots; i++)
        {
            runtime.SniperRounds->Fire(BulletRound, eye, yaw, pitch, nullptr);
            Run(runtime, ShotTicks);
        }
    }

    // XEARTH's hit count (GUNT_4's variable 0) after the shots.
    float EarthCount(Lethality lethality)
    {
        auto runtime = CreateRuntime(EarthLevel, EarthArena, lethality);
        std::shared_ptr<MdkObject> earth;
        for (auto& o : runtime->Objects)
        {
            if (o->TypeName == Earth && !o->Dead)
            {
                earth = o;
                break;
            }
        }

        if (earth == nullptr)
        {
            throw std::runtime_error("No XEARTH");
        }

        auto center = Middle(runtime->GetWorldBounds(*earth));
        auto eye = EyeOn(*runtime, center, 0);
        if (!eye.has_value())
        {
            throw std::runtime_error("No eye on XEARTH");
        }

        Shoot(*runtime, *eye, center);
        CHECK_FALSE(earth->Dead);
        return runtime->GetArenaState(EarthArena)->Variables[0];
    }

    // The counter of HMO_8's first group whose hit script counts shots, after the shots.
    int GroupCount(Lethality lethality)
    {
        auto runtime = CreateRuntime(GroupLevel, GroupArena, lethality);
        auto state = runtime->GetArenaState(GroupArena);
        for (int i = 0; i < ScriptRuntime::GroupCount; i++)
        {
            if (state->GroupHitScripts[i] == 0 || (state->GroupHitMasks[i] & ScriptRuntime::HitShot) == 0)
            {
                continue;
            }

            for (auto center : runtime->GroupCenters(GroupArena, i + 1))
            {
                auto eye = EyeOn(*runtime, center, i + 1);
                if (!eye.has_value())
                {
                    continue;
                }

                Shoot(*runtime, *eye, center);
                return state->GroupCounters[i];
            }
        }

        throw std::runtime_error("No shot group in " + GroupArena);
    }
}

DATA_TEST_CASE("OneHitScriptedTests.EarthCountsHitsAsUsual")
{
    CHECK(EarthCount(Lethality::Normal) == EarthCount(Lethality::OneHit));
}

DATA_TEST_CASE("OneHitScriptedTests.GroupCountsHitsAsUsual")
{
    CHECK(GroupCount(Lethality::Normal) == GroupCount(Lethality::OneHit));
}
