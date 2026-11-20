#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "Mdk.Core/Dictionary.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Formats/Arena.h"
#include "Mdk.Formats/BetaDemo.h"
#include "Mdk.Formats/Fti.h"
#include "Mdk.Game/Audio/LevelMusic.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Collision/Bsp.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Menu/MenuCursor.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Menu;
using Mdk::Formats::BetaDemo;
using Mdk::Formats::BspNode;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;
using State = KurtPlayer::State;

// The 1996 demo's moves (Kurt.Beta.cs), its triangles that only stop Kurt, its ambient
// loops, on a synthetic floor; and the menus' ARROW cursor.

namespace
{
    constexpr float Step = 1.f / 60.f;
    // Frames of every animation: a move lasts 9 ticks (it ends on its last frame).
    constexpr int Frames = 10;
    constexpr float StartYaw = 90.f;
    // The turn of a roll over 9 of its 10 frames.
    constexpr float RollTurn = 90.f * (Frames - 1) / Frames;
    constexpr float Tolerance = 1.f;
    // The demo's flag of triangles that only stop Kurt.
    constexpr uint Clip = BetaDemo::NotDrawn;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    // A square floor at z 0 (one node holding two up-facing triangles), the second one
    // flagged Clip; a vertex high above makes the arena's box tall enough for Kurt.
    std::shared_ptr<Formats::Arena> Floor()
    {
        auto arena = std::make_shared<Formats::Arena>();
        arena->Name = "HMO_1";
        arena->Materials = {"WALL"};
        arena->Vertices = {Vector3(-100.f, -100.f, 0.f), Vector3(100.f, -100.f, 0.f), Vector3(100.f, 100.f, 0.f), Vector3(-100.f, 100.f, 0.f), Vector3(0.f, 0.f, 50.f)};
        arena->TriangleIndices = {0, 1, 2, 0, 2, 3};
        arena->TriangleMaterials = {0, 0};
        arena->TriangleUvs = std::vector<Vector2>(6);
        arena->TriangleFlags = {0, Clip};
        arena->Nodes = {BspNode{Vector3::UnitZ(), 0.f, Bsp::None, Bsp::None, 0, 2, 0, 0}};
        arena->ClipFlag = Clip;
        return arena;
    }

    // Kurt with the space and mixer he refers to (the C#'s garbage collector kept them).
    struct Standing
    {
        std::shared_ptr<ArenaSpace> Space = std::make_shared<ArenaSpace>();
        std::shared_ptr<SoundMixer> Mixer;
        std::shared_ptr<KurtPlayer> Kurt;
    };

    void Run(KurtPlayer& kurt, Input& input, float seconds)
    {
        for (float t = 0.f; t < seconds; t += Step)
        {
            kurt.Update(input, Step);
        }
    }

    void Run(KurtPlayer& kurt, Input&& input, float seconds) { Run(kurt, input, seconds); }

    enum class Moves { Retail, Beta };

    // Kurt standing on the floor, facing +Y.
    Standing StandingKurt(Moves moves)
    {
        Standing standing;
        standing.Space->Add(Floor());
        standing.Mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        standing.Kurt = std::make_shared<KurtPlayer>(*standing.Space, *standing.Mixer, [](auto) { return Frames; });
        standing.Kurt->Feet = Vector3(0.f, 0.f, 1.f);
        standing.Kurt->Yaw = StartYaw;
        standing.Kurt->BetaMoves = moves == Moves::Beta;
        Run(*standing.Kurt, Input(), 1.f);
        CHECK(standing.Kurt->OnFloor);
        return standing;
    }

    // Presses a key for one step.
    void Press(KurtPlayer& kurt, Input& input, Key key)
    {
        input.Hold(key, Input::State::Down);
        kurt.Update(input, Step);
        input.Hold(key, Input::State::Up);
    }

    void Press(KurtPlayer& kurt, Input&& input, Key key) { Press(kurt, input, key); }
}

TEST_CASE("BetaTests.RollLeftMovesLeftAndTurnsRight")
{
    auto standing = StandingKurt(Moves::Beta);
    auto& kurt = *standing.Kurt;
    Input input;
    Press(kurt, input, Key::RollLeft);
    CHECK(State::RollLeft == kurt.Current);
    Run(kurt, input, 0.5f);
    CHECK(State::Still == kurt.Current);
    CHECK(kurt.Yaw >= StartYaw - RollTurn - Tolerance);
    CHECK(kurt.Yaw <= StartYaw - RollTurn + Tolerance);
    CAPTURE(kurt.Feet.X);
    CHECK(kurt.Feet.X < -1.f);
    CHECK(kurt.OnFloor);
}

TEST_CASE("BetaTests.RollRightTurnsLeft")
{
    auto standing = StandingKurt(Moves::Beta);
    auto& kurt = *standing.Kurt;
    Press(kurt, Input(), Key::RollRight);
    Run(kurt, Input(), 0.5f);
    CHECK(kurt.Yaw >= StartYaw + RollTurn - Tolerance);
    CHECK(kurt.Yaw <= StartYaw + RollTurn + Tolerance);
    CAPTURE(kurt.Feet.X);
    CHECK(kurt.Feet.X > 1.f);
}

TEST_CASE("BetaTests.RetailKurtDoesntRoll")
{
    auto standing = StandingKurt(Moves::Retail);
    auto& kurt = *standing.Kurt;
    Press(kurt, Input(), Key::RollLeft);
    CHECK(State::Still == kurt.Current);
}

TEST_CASE("BetaTests.TheHelmetGoesOnBeforeSniperModeAndComesOffAfter")
{
    auto standing = StandingKurt(Moves::Beta);
    auto& kurt = *standing.Kurt;
    Input input;
    Press(kurt, input, Key::Sniper);
    CHECK(State::HelmetOn == kurt.Current);
    CHECK_FALSE(kurt.Sniping);
    Run(kurt, input, 0.5f);
    CHECK(kurt.Sniping);

    Press(kurt, input, Key::Sniper);
    CHECK_FALSE(kurt.Sniping);
    CHECK(State::HelmetOff == kurt.Current);
    Run(kurt, input, 0.5f);
    CHECK(State::Still == kurt.Current);
}

TEST_CASE("BetaTests.BackingUpPlaysItsFramesForwards")
{
    auto standing = StandingKurt(Moves::Beta);
    auto& kurt = *standing.Kurt;
    Input input;
    input.Hold(Key::Back, Input::State::Down);
    Run(kurt, input, 0.5f);
    auto frame = kurt.AnimationFrame;
    Run(kurt, input, 0.1f);
    CHECK(kurt.BackingUp());
    CHECK(kurt.AnimationFrame > frame);
}

TEST_CASE("BetaTests.ScriptRaysPassTheTrianglesThatOnlyStopKurt")
{
    Bsp bsp(Floor());
    // Triangle 1 (flagged) is the floor's half where y > x.
    Vector3 above(-50.f, 50.f, 10.f);
    auto below = above;
    below.Z = -10.f;
    Vector3 point;
    CHECK(1 == bsp.Segment(above, below, Bsp::SegmentMode::Any, point));
    CHECK(Bsp::None == bsp.Segment(above, below, Bsp::SegmentMode::Any, point, Bsp::Clip::PassesThrough));
    Vector3 aboveRight(50.f, -50.f, above.Z);
    Vector3 belowRight(50.f, -50.f, below.Z);
    CHECK(0 == bsp.Segment(aboveRight, belowRight, Bsp::SegmentMode::Any, point, Bsp::Clip::PassesThrough));
}

TEST_CASE("BetaTests.ArenasPlayTheirAmbientLoop")
{
    AudioDevice device(Output::Muted);
    SoundMixer::Entry entry{std::make_shared<Sound>(std::vector<float>(1), 1, 1, Looping::Forever), 0};
    SoundMixer mixer(device, [entry](const std::string&) { return std::optional<SoundMixer::Entry>(entry); });
    LevelMusic music(mixer, Dictionary<std::string, std::string>());
    music.Ambience = Dictionary<std::string, std::string>{{"ARENA_1", "AMB1"}, {"ARENA_2", "AMB2"}};
    music.Enter("ARENA_1");
    CHECK(mixer.IsPlaying("AMB1"));
    music.Enter("ARENA_2");
    CHECK_FALSE(mixer.IsPlaying("AMB1"));
    CHECK(mixer.IsPlaying("AMB2"));
    music.Enter("CORR_1");
    CHECK_FALSE(mixer.IsPlaying("AMB2"));
}

DATA_TEST_CASE("BetaTests.TheCursorIsTheOriginalsArrowScaledByWholeSteps")
{
    auto fti = Formats::Fti::Load(GameData::Data()->PathOf("MISC/MDKFONT.FTI"));
    auto one = MenuCursor::Build(*fti, 1);
    auto two = MenuCursor::Build(*fti, 2);
    REQUIRE(one.has_value());
    REQUIRE(two.has_value());
    CHECK(one->Width * 2 == two->Width);
    CHECK(one->Height * 2 == two->Height);
    CHECK(two->Width * two->Height * 4 == static_cast<int>(two->Rgba.size()));
    CHECK(two->HotX >= 0);
    CHECK(two->HotX <= two->Width - 1);

    // Opaque where the sprite is drawn, see-through elsewhere.
    std::vector<byte> alphas;
    for (size_t i = 3; i < two->Rgba.size(); i += 4)
    {
        alphas.push_back(two->Rgba[i]);
    }

    CHECK(std::find(alphas.begin(), alphas.end(), static_cast<byte>(255)) != alphas.end());
    CHECK(std::find(alphas.begin(), alphas.end(), static_cast<byte>(0)) != alphas.end());
}
