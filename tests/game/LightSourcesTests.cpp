#include <doctest.h>

#include "game/objects/LightSources.h"
#include "Asserts.h"

#include <unordered_set>

using namespace Mdk::Game::Objects;

// The enhanced look's point lights of the game: explosions fade as they play, fires flicker.
namespace
{
    constexpr float Tolerance = 1e-4f;
    constexpr int Frames = 20;
}

TEST_CASE("LightSourcesTests.ExplosionsFadeAsTheyPlay")
{
    struct Row { float Time; float Strength; };
    const Row rows[] = {{0.f, 1.f}, {10.f, 0.5f}, {20.f, 0.f}, {25.f, 0.f}};

    for (const Row& row : rows)
    {
        CAPTURE(row.Time);
        CHECK_NEAR(row.Strength, LightSources::Fade(row.Time, Frames), Tolerance);
    }
}

TEST_CASE("LightSourcesTests.FiresFlickerWithinTheirRange")
{
    std::unordered_set<float> seen;
    for (int tick = 0; tick < 100; tick++)
    {
        float flicker = LightSources::Flicker(tick, 3);
        CHECK(flicker >= 1.f - LightSources::FlickerDepth);
        CHECK(flicker <= 1.f);
        seen.insert(flicker);
    }

    CHECK(seen.size() > 1);
}
