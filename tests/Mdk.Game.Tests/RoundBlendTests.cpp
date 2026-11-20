#include <memory>
#include <optional>
#include <string>

#include <doctest.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Cmi.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Kurt/Scope.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/TriangleGroups.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Mdk.Game/Scripts/SniperRounds.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// Issue #16: sniper rounds and their cameras (the views at the top of the scope) moved at
// 30 Hz. A frame draws them between the last tick and this one; a new round is drawn where it is.
namespace
{
    constexpr int Level = 3;
    // High above the arenas: the round hits nothing.
    const Vector3 Eye{0.0f, 0.0f, 20000.0f};
    constexpr float Half = 0.5f;
    constexpr float Precision = 1e-3f;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    std::shared_ptr<ScriptRuntime> Runtime()
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

    void Near(Vector3 expected, Vector3 actual) { CHECK(Vector3::Distance(expected, actual) < Precision); }
}

DATA_TEST_CASE("RoundBlendTests.RoundCamerasAreDrawnBetweenTicks")
{
    auto runtime = Runtime();
    auto& rounds = *runtime->SniperRounds;
    rounds.Fire(Mdk::Game::Kurt::Scope::Bullets, Eye, 0.0f, 0.0f, nullptr);
    Near(rounds.Camera(0, 1.0f).Position, rounds.Camera(0, Half).Position);

    rounds.Update(1.0f);
    auto was = rounds.Camera(0, 1.0f).Position;
    rounds.Update(1.0f);
    auto now = rounds.Camera(0, 1.0f);
    CHECK(now.Shot == SniperRounds::Shot::Watching);
    CHECK(was != now.Position);
    Near(Vector3::Lerp(was, now.Position, Half), rounds.Camera(0, Half).Position);
    Near(now.Forward, rounds.Camera(0, Half).Forward);
}
