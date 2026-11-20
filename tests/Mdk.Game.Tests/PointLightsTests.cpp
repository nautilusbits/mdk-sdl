#include <doctest.h>

#include "Mdk.Engine/Render/PointLights.h"

#include <algorithm>
#include <array>
#include <vector>

using namespace Mdk;
using namespace Mdk::Engine::Render;

// The frame's point lights: gathered up to a limit, the ones reaching nearest the camera
// packed for the GPU.
namespace
{
    const Vector3 Warm{1.0f, 0.8f, 0.5f};

    PointLight At(float x, float radius = 1.0f) { return {Vector3(x, 0.0f, 0.0f), Warm, radius}; }
}

TEST_CASE("PointLightsTests.LightsBeyondTheLimitAreDropped")
{
    PointLights lights;
    for (int i = 0; i < PointLights::Gathered + 5; i++)
    {
        lights.Add(At(static_cast<float>(i)));
    }

    CHECK(lights.Count() == PointLights::Gathered);
}

TEST_CASE("PointLightsTests.ClearEmptiesTheFrame")
{
    PointLights lights;
    lights.Add(At(1.0f));

    lights.Clear();

    CHECK(lights.Count() == 0);
    std::array<PointLight, PointLights::Shown> packed{};
    CHECK(lights.Pack(Vector3::Zero(), packed) == 0);
}

TEST_CASE("PointLightsTests.FewLightsAreAllPacked")
{
    PointLights lights;
    lights.Add(At(5.0f));
    lights.Add(At(3.0f));
    std::array<PointLight, PointLights::Shown> packed{};

    CHECK(lights.Pack(Vector3::Zero(), packed) == 2);
}

TEST_CASE("PointLightsTests.NearestLightsArePacked")
{
    // 30 lights from x = 30 down to 1: the 16 nearest are x = 1 to 16.
    PointLights lights;
    for (int x = 30; x >= 1; x--)
    {
        lights.Add(At(static_cast<float>(x)));
    }

    std::array<PointLight, PointLights::Shown> packed{};
    const auto count = lights.Pack(Vector3::Zero(), packed);

    CHECK(count == PointLights::Shown);
    std::vector<float> xs;
    for (const auto& light : packed)
    {
        xs.push_back(light.Position.X);
    }

    std::sort(xs.begin(), xs.end());
    std::vector<float> expected;
    for (int x = 1; x <= PointLights::Shown; x++)
    {
        expected.push_back(static_cast<float>(x));
    }

    CHECK(xs == expected);
}

TEST_CASE("PointLightsTests.FarReachBeatsNearness")
{
    // A blast 100 away reaching 200 is nearer than any of 16 sparks 10 away reaching 1.
    PointLights lights;
    for (int i = 0; i < PointLights::Shown; i++)
    {
        lights.Add(At(10.0f));
    }

    lights.Add(At(100.0f, 200.0f));
    std::array<PointLight, PointLights::Shown> packed{};
    lights.Pack(Vector3::Zero(), packed);

    CHECK(std::any_of(packed.begin(), packed.end(), [](const PointLight& l) { return l.Radius == 200.0f; }));
}

TEST_CASE("PointLightsTests.DarkOrPointlessLightsAreSkipped")
{
    PointLights lights;
    lights.Add(PointLight{Vector3::Zero(), Vector3::Zero(), 10.0f});
    lights.Add(PointLight{Vector3::Zero(), Warm, 0.0f});

    CHECK(lights.Count() == 0);
}
