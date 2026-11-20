#include <utility>

#include <doctest.h>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/HeadLook.h"
#include "Mdk.Game.Tests/Asserts.h"

using namespace Mdk;
using namespace Mdk::Engine::Platform;

// The gyroscope's rates (the phone's portrait axes: x right, y up, z out of the screen) become the
// look's turn in a landscape phone: the head turning right looks right, nodding up looks up,
// whichever way up the phone is.

namespace
{
    constexpr float Tolerance = 1e-4f;
    constexpr float Rate = 1.0f;
    constexpr float Seconds = 0.5f;
    const float Degrees = Rate * Seconds * 180.0f / MathF::PI;

    // The world's up and right axes in the phone's axes: landscape is its right side up (x up, y to
    // the left), flipped its left side up.
    std::pair<Vector3, Vector3> Axes(ScreenTurn turn)
    {
        return turn == ScreenTurn::Landscape
                   ? std::pair{Vector3::UnitX(), -Vector3::UnitY()}
                   : std::pair{-Vector3::UnitX(), Vector3::UnitY()};
    }

    constexpr ScreenTurn Turns[] = {ScreenTurn::Landscape, ScreenTurn::LandscapeFlipped};
}

TEST_CASE("HeadLookTests.TurningTheHeadRightLooksRight")
{
    for (auto turn : Turns)
    {
        // Turning right is a clockwise turn seen from above: negative about up.
        Vector2 look = HeadLook::FromGyro(-Axes(turn).first * Rate, Seconds, turn);
        CHECK_NEAR(Degrees, look.X, Tolerance);
        CHECK_NEAR(0.0f, look.Y, Tolerance);
    }
}

TEST_CASE("HeadLookTests.NoddingUpLooksUp")
{
    for (auto turn : Turns)
    {
        // Nodding up turns the view about the right axis; the look's Y is down, as the mouse's.
        Vector2 look = HeadLook::FromGyro(Axes(turn).second * Rate, Seconds, turn);
        CHECK_NEAR(0.0f, look.X, Tolerance);
        CHECK_NEAR(-Degrees, look.Y, Tolerance);
    }
}

TEST_CASE("HeadLookTests.TiltingSidewaysDoesNotLook")
{
    Vector2 look = HeadLook::FromGyro(Vector3::UnitZ() * Rate, Seconds, ScreenTurn::Landscape);
    CHECK(look == Vector2::Zero());
}

TEST_CASE("HeadLookTests.ANoiseLevelRateDoesNotLook")
{
    // A still phone's gyroscope reads a little: the view must not creep.
    Vector2 look = HeadLook::FromGyro(Vector3(0.005f, -0.005f, 0.0f), Seconds, ScreenTurn::Landscape);
    CHECK(look == Vector2::Zero());
}
