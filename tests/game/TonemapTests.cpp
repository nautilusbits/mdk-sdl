#include <doctest.h>

#include "engine/render/Tonemap.h"

#include <array>
#include <cmath>

using namespace Mdk::Engine::Render;

// The enhanced look's tone curve (shaders/enhanced.hlsl tonemap): the original's range
// kept, brighter light rolled off below white.
namespace
{
    constexpr float Tolerance = 1e-4f;
    constexpr float Step = 1e-3f;
}

TEST_CASE("TonemapTests.DarkerThanTheKneeIsKept")
{
    for (const auto light : std::array{0.0f, 0.25f, Tonemap::Knee})
    {
        CHECK(std::abs(Tonemap::Map(light) - light) <= Tolerance);
    }
}

TEST_CASE("TonemapTests.BrightLightStaysBelowWhite")
{
    for (const auto light : std::array{1.0f, 2.0f, 100.0f})
    {
        const auto mapped = Tonemap::Map(light);

        CHECK(mapped >= Tonemap::Knee);
        CHECK(mapped <= 1.0f);
        CHECK(mapped < light);
    }
}

TEST_CASE("TonemapTests.CurveRisesSmoothly")
{
    // Rising everywhere; at the knee as steep on both sides (no crease).
    for (auto x = 0.0f; x < 4.0f; x += 0.01f)
    {
        CHECK(Tonemap::Map(x + 0.01f) > Tonemap::Map(x));
    }

    const auto below = (Tonemap::Map(Tonemap::Knee) - Tonemap::Map(Tonemap::Knee - Step)) / Step;
    const auto above = (Tonemap::Map(Tonemap::Knee + Step) - Tonemap::Map(Tonemap::Knee)) / Step;
    CHECK(std::abs(below - above) <= 1e-2f);
}
