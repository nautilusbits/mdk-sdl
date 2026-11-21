#include <doctest.h>

#include "Asserts.h"
#include "core/DotNet.h"
#include "core/Numerics.h"
#include "engine/render/Renderer.h"
#include "engine/render/SunShadow.h"
#include "game/flow/Settings.h"
#include "game/level/EnhancedLook.h"

using namespace Mdk;
using namespace Mdk::Engine::Render;
using namespace Mdk::Game::Flow;
using namespace Mdk::Game::Level;

// The enhanced look: the sun (level.gd _enhance) and its shadow map.

namespace
{
    constexpr float Tolerance = 1e-3f;
    constexpr float Radius = 300.f;
    constexpr uint Size = 2048;

    Vector3 Clip(const Matrix4x4& matrix, Vector3 point)
    {
        auto clip = Vector4::Transform(Vector4(point, 1.f), matrix);
        return Vector3(clip.X, clip.Y, clip.Z) / clip.W;
    }
}

TEST_CASE("EnhancedLookTests.SunShinesAsGodots")
{
    // Godot's light at (-55, 35, 0) degrees shines along (-0.329, -0.819, -0.470): Y up, -Z ahead.
    auto sun = EnhancedLook::SunDirection();

    CHECK_NEAR(-0.329f, sun.X, Tolerance);
    CHECK_NEAR(0.470f, sun.Y, Tolerance);
    CHECK_NEAR(-0.819f, sun.Z, Tolerance);
    CHECK_NEAR(1.f, sun.Length(), Tolerance);
}

TEST_CASE("EnhancedLookTests.StraightDownSunHasNoYaw")
{
    auto sun = EnhancedLook::Sun(-90.f, 35.f);

    CHECK_NEAR(-1.f, sun.Z, Tolerance);
}

TEST_CASE("EnhancedLookTests.ShadowMapIsCentredOnTheCamera")
{
    Vector3 centre(-1167.f, -1173.f, -27.f);
    auto matrix = SunShadow::Matrix(EnhancedLook::SunDirection(), centre, Radius, Size);

    // Snapped to whole texels: off by half a texel at most.
    auto halfTexel = 1.f / Size;
    auto clip = Clip(matrix, centre);
    CHECK(MathF::Abs(clip.X) >= 0.f);
    CHECK(MathF::Abs(clip.X) <= halfTexel + Tolerance);
    CHECK(MathF::Abs(clip.Y) >= 0.f);
    CHECK(MathF::Abs(clip.Y) <= halfTexel + Tolerance);
    CHECK_NEAR(0.5f, clip.Z, Tolerance);
}

TEST_CASE("EnhancedLookTests.DepthGrowsAlongTheSunlight")
{
    auto sun = EnhancedLook::SunDirection();
    auto matrix = SunShadow::Matrix(sun, Vector3::Zero(), Radius, Size);

    auto near = Clip(matrix, -sun * 100.f);
    auto far = Clip(matrix, sun * 100.f);

    CHECK(near.Z < far.Z);
    CHECK_NEAR(near.X, far.X, Tolerance);
    CHECK_NEAR(near.Y, far.Y, Tolerance);
}

TEST_CASE("EnhancedLookTests.ShadowMapCoversTheRadius")
{
    auto matrix = SunShadow::Matrix(-Vector3::UnitZ(), Vector3::Zero(), Radius, Size);

    // Straight down: the square's edges are the radius away on the ground.
    auto edge = Clip(matrix, Vector3(Radius, 0.f, 0.f));

    CHECK_NEAR(1.f, MathF::Abs(edge.X) + MathF::Abs(edge.Y), Tolerance);
}

TEST_CASE("EnhancedLookTests.SmallMovesKeepTheTexelGrid")
{
    auto sun = EnhancedLook::SunDirection();
    auto texel = 2.f * Radius / Size;

    auto a = SunShadow::Matrix(sun, Vector3::Zero(), Radius, Size);
    auto b = SunShadow::Matrix(sun, Vector3(texel * 0.1f, 0.f, 0.f), Radius, Size);

    CHECK_NEAR(Clip(a, Vector3::One()).X, Clip(b, Vector3::One()).X, Tolerance);
}

TEST_CASE("EnhancedLookTests.LooksPickShading")
{
    CHECK(EnhancedLook::Surfaces(Graphics::Original) == Shading::Original);
    CHECK(EnhancedLook::Surfaces(Graphics::Enhanced) == Shading::Lit);
    CHECK(EnhancedLook::Sprites(Graphics::Enhanced) == Shading::Sprite);
    CHECK(EnhancedLook::Sky(Graphics::Enhanced) == Sampling::Linear);
    CHECK(EnhancedLook::Sky(Graphics::Original) == Sampling::Nearest);
}
