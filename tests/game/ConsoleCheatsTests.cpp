#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include <doctest.h>

#include "Asserts.h"
#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "engine/platform/Input.h"
#include "formats/MdkData.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/collision/Bsp.h"
#include "game/kurt/Kurt.h"
#include "game/level/ArenaStops.h"
#include "game/level/LevelData.h"
#include "game/scripts/GameStats.h"
#include "shared/GameData.h"

using namespace Mdk;
using Mdk::Engine::Audio::AudioDevice;
using Mdk::Engine::Audio::Output;
using Mdk::Engine::Platform::Input;
using Mdk::Engine::Platform::Key;
using Mdk::Game::Audio::SoundMixer;
using Mdk::Game::Collision::ArenaSpace;
using Mdk::Game::Kurt::Clipping;
using Mdk::Game::Kurt::Mortality;
using Mdk::Game::Level::ArenaStops;
using Mdk::Game::Level::LevelData;
using Mdk::Game::Scripts::GameStats;
using Mdk::Tests::GameData;

// What the console's cheats do to Kurt and the level: god, noclip, the arenas' stops, enemies.

namespace
{
    using KurtType = Mdk::Game::Kurt::Kurt;

    constexpr int Level = 3;
    constexpr float Step = 1.f / 60.f;
    constexpr int Frames = 5;
    constexpr int Fatal = 1000;
    // Above level 3's start pad.
    constexpr Vector3 Pad{-4.f, 0.f, 195.f};

    // C#'s Lazy<LevelData>.
    const std::shared_ptr<LevelData>& Data()
    {
        static auto data = std::make_shared<LevelData>(*GameData::Data(), Level);
        return data;
    }

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ArenaSpace> SpaceOf(const LevelData& level)
    {
        auto space = std::make_shared<ArenaSpace>();
        for (const auto& arena : level.Arenas)
        {
            if (!level.IsReachable(arena->Name))
            {
                continue;
            }

            space->Add(arena);
        }

        return space;
    }

    // Kurt keeps references to his space and mixer: the returned pointer owns them too.
    struct KurtRig
    {
        std::shared_ptr<ArenaSpace> Space;
        std::shared_ptr<SoundMixer> Mixer;
        std::shared_ptr<KurtType> Kurt;
    };

    std::shared_ptr<KurtType> NewKurt()
    {
        auto rig = std::make_shared<KurtRig>();
        rig->Space = SpaceOf(*Data());
        rig->Mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        rig->Kurt = std::make_shared<KurtType>(*rig->Space, *rig->Mixer, [](KurtType::State) { return Frames; });
        rig->Kurt->Feet = Pad;
        return std::shared_ptr<KurtType>(rig, rig->Kurt.get());
    }

    void Run(KurtType& kurt, Input& input, float seconds)
    {
        for (auto t = 0.f; t < seconds; t += Step)
        {
            kurt.Update(input, Step);
        }
    }
}

DATA_TEST_CASE("ConsoleCheatsTests.GodModeTakesNoDamage")
{
    auto kurt = NewKurt();
    Input before;
    Run(*kurt, before, 1.f);
    kurt->Mortality = Mortality::God;
    kurt->Hurt(Fatal);
    Input after;
    Run(*kurt, after, 1.f);
    CHECK(KurtType::MaxHealth == kurt->Health);

    kurt->Mortality = Mortality::Mortal;
    kurt->Hurt(Fatal);
    CHECK(0 == kurt->Health);
}

DATA_TEST_CASE("ConsoleCheatsTests.NoclipFliesWithoutFalling")
{
    auto kurt = NewKurt();
    kurt->Clipping = Clipping::Off;
    kurt->Yaw = 0.f;
    Input input;
    input.Hold(Key::Forward, Input::State::Down);
    Run(*kurt, input, 1.f);

    // Straight ahead (yaw 0 is +X), level, through whatever is there.
    CHECK(kurt->Feet.X > Pad.X + 10.f);
    CHECK_PRECISION(Pad.Z, kurt->Feet.Z, 3);

    input.Hold(Key::Forward, Input::State::Up);
    input.Hold(Key::Jump, Input::State::Down);
    Run(*kurt, input, 0.5f);
    CHECK(kurt->Feet.Z > Pad.Z + 5.f);
}

DATA_TEST_CASE("ConsoleCheatsTests.EveryArenaHasAStop")
{
    const auto& level = *Data();
    auto space = SpaceOf(level);
    auto start = level.Dti->Arenas[level.Dti->StartArena].Name;
    auto stop = ArenaStops::Find(level, *space, start);
    REQUIRE(stop.has_value());
    auto bsps = space->At(*stop);
    CHECK(std::any_of(bsps.begin(), bsps.end(), [&](auto* b) { return b->Arena->Name == start; }));
    CHECK_FALSE(ArenaStops::Find(level, *space, "NOWHERE").has_value());
}

TEST_CASE("ConsoleCheatsTests.EnemiesAreTheOriginalsTypes")
{
    CHECK(GameStats::IsEnemy("xb"));
    CHECK_FALSE(GameStats::IsEnemy("SW_HBOMB"));
}
