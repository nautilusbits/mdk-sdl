#include <doctest.h>

#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Game/Angles.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Kurt/KurtSprite.h"

#include <optional>
#include <string>
#include <tuple>

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Game;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using Mdk::Game::Kurt::KurtSprite;
using KurtState = Mdk::Game::Kurt::Kurt::State;

namespace Mdk::Game::Kurt
{
    // KurtSprite's private animation table (reflection in the C#).
    struct KurtSpriteTestsProbe
    {
        static std::string Name(Kurt::State state) { return Animation(state).Name; }

        static bool Once(Kurt::State state) { return Animation(state).Repeat == KurtSprite::Repeat::Once; }

    private:
        static KurtSprite::StateAnimation Animation(Kurt::State state)
        {
            KurtSprite::StateAnimation animation;
            REQUIRE(KurtSprite::Animations().TryGetValue(state, animation));
            return animation;
        }
    };
}

// Kurt's sprite: the rows at and below his feet (the shadow) drawn in front of the floor.
namespace
{
    const Vector3 Feet(100.f, 200.f, 50.f);

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    // The follow camera: 8 behind, 6 above.
    const Vector3 Eye = Feet + Vector3(0.f, -8.f, 6.f);

    Vector3 Forward() { return Vector3::Normalize(Feet - Eye); }

    Vector3 SpriteUp() { return Vector3::Normalize(Vector3::UnitZ() - Forward() * Forward().Z); }

    float Depth() { return Vector3::Dot(Feet - Eye, Forward()); }

    // The shadow's bottom row, 0.6 below the feet on the sprite's plane (facing the eye).
    Vector3 Bottom() { return Feet - SpriteUp() * 0.6f; }

    Vector3 Moved(float scale) { return Eye + (Bottom() - Eye) * scale; }
}

TEST_CASE("KurtSpriteTests.ShadowStaysAboveTheFloor")
{
    for (float bump : {0.f, 0.5f})
    {
        CAPTURE(bump);
        auto moved = Moved(KurtSprite::Nearer(Depth(), std::nullopt));

        CHECK(moved.Z > Feet.Z + bump);
    }
}

TEST_CASE("KurtSpriteTests.ShadowStaysAboveADownhill")
{
    // The floor falls away from the camera at 30°: higher than the feet between them.
    auto slope = MathF::Tan(DegreesToRadians(30.f));
    auto floorAt = [slope](float y) { return Feet.Z + (Feet.Y - y) * slope; };
    auto direction = Bottom() - Eye;
    auto t = (floorAt(Eye.Y) - Eye.Z) / (direction.Z + direction.Y * slope);
    auto floorDepth = Vector3::Dot(direction * t, Forward());

    auto moved = Moved(KurtSprite::Nearer(Depth(), floorDepth));

    CHECK(moved.Z > floorAt(moved.Y));
}

TEST_CASE("KurtSpriteTests.BodyStaysInFrontOfACrest")
{
    // Kurt has stepped off a flat top onto a slope, 0.3 below the top's plane: the ray to his
    // ankles (0.1 above the feet) meets the top's edge first.
    constexpr float top = 0.3f;
    auto ankles = Feet + SpriteUp() * 0.1f;
    auto direction = ankles - Eye;
    auto t = (Feet.Z + top - Eye.Z) / direction.Z;
    CHECK(t >= 0.f);
    CHECK(t <= 1.f);

    auto moved = Eye + direction * KurtSprite::InFront(Depth(), Vector3::Dot(direction * t, Forward()));

    CHECK(moved.Z > Feet.Z + top);
}

// damp_animate (0x4646a4) draws K_SIDE unmirrored both ways: the gun stays in his right hand.
// Only the demo's left roll is mirrored.
TEST_CASE("KurtSpriteTests.MirrorsOnlyTheLeftRoll")
{
    const std::tuple<KurtState, float, float> cases[] = {
        {KurtState::Side, -1.f, 1.f},
        {KurtState::Side, 1.f, 1.f},
        {KurtState::RollLeft, 0.f, -1.f},
        {KurtState::RollRight, 0.f, 1.f},
    };
    for (auto [state, strafe, mirror] : cases)
    {
        CAPTURE(strafe);
        ArenaSpace space;
        SoundMixer mixer(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<Mdk::Game::Kurt::Kurt>(space, mixer, [](auto) { return 1; });
        kurt->Current = state;
        kurt->StrafeSpeed = strafe;

        CHECK(KurtSprite::Mirror(*kurt) == mirror);
    }
}

// Issue #20: braking, Kurt flickered. damp_animate (0x4646a4, state 0x32a) plays K_BSLIDE's 3
// frames once and holds the last; the port looped them 10 times a second.
TEST_CASE("KurtSpriteTests.TheBrakeHoldsItsLastFrame")
{
    using Mdk::Game::Kurt::KurtSpriteTestsProbe;

    CHECK(KurtSpriteTestsProbe::Name(KurtState::SlideBrake) == "K_BSLIDE");
    CHECK(KurtSpriteTestsProbe::Once(KurtState::SlideBrake));
}

TEST_CASE("KurtSpriteTests.CloseUpStaysInFront")
{
    auto nearer = KurtSprite::Nearer(1.f, std::nullopt);
    CHECK(nearer >= 0.1f);
    CHECK(nearer <= 1.f);
}
