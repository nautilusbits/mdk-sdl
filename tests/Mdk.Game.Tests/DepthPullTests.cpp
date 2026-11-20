#include <doctest.h>

#include "Mdk.Engine/Render/DepthPull.h"
#include "Mdk.Engine/Render/Renderer.h"

#include <array>
#include <cmath>

using namespace Mdk;
using namespace Mdk::Engine::Render;

// Layers and outlines drawn nearer than the surfaces they lie on, at the same place on screen.
namespace
{
    constexpr float Near = 0.5f;
    constexpr float Far = 20000.0f;
    const Vector3 Eye{-2222.0f, 3200.0f, -2885.0f};
    const Matrix4x4 Camera =
        Matrix4x4::CreateLookAt(Eye, Eye - Vector3::UnitY(), Vector3::UnitZ()) * Matrix4x4::CreatePerspectiveFieldOfView(1.2f, 4.0f / 3.0f, Near, Far);

    Vector3 Ndc(Vector3 point, const Matrix4x4& transform)
    {
        const auto clip = Vector4::Transform(Vector4(point, 1.0f), transform);
        return Vector3(clip.X, clip.Y, clip.Z) / clip.W;
    }

    Vector3 Ahead(float distance) { return Eye + Vector3(0.3f * distance, -distance, -0.1f * distance); }
}

TEST_CASE("DepthPullTests.AnEdgeIsPulledTowardsTheEye")
{
    for (const auto distance : std::array{10.0f, 100.0f, 1000.0f})
    {
        // Seen at a grazing angle: half a pixel off the edge, the surface is that much nearer.
        const auto edge = Ahead(distance);
        const auto surface = edge + Vector3(0.0f, 0.004f * distance, 0.0f);

        const auto line = Ndc(edge, DepthPull::Of(Eye, Primitive::Lines, 0) * Camera);
        const auto plain = Ndc(edge, Camera);

        CHECK(std::abs(plain.X - line.X) <= 1e-4f);
        CHECK(std::abs(plain.Y - line.Y) <= 1e-4f);
        CHECK(line.Z < Ndc(surface, Camera).Z);
    }
}

TEST_CASE("DepthPullTests.ALayerIsNearerThanItsWallInTheDepthBuffer")
{
    for (const auto distance : std::array{10.0f, 100.0f, 1000.0f, 4000.0f})
    {
        // A 32-bit float depth keeps 24 bits: the pull must span more than one step.
        constexpr float DepthStep = 1.0f / (1 << 24);
        const auto point = Ahead(distance);

        const auto wall = Ndc(point, Camera);
        const auto poster = Ndc(point, DepthPull::Of(Eye, Primitive::Triangles, 1) * Camera);

        CHECK(std::abs(wall.X - poster.X) <= 1e-4f);
        CHECK(std::abs(wall.Y - poster.Y) <= 1e-4f);
        CHECK(wall.Z - poster.Z > 4.0f * DepthStep);
    }
}

TEST_CASE("DepthPullTests.PlainTrianglesStayPut")
{
    CHECK(DepthPull::Of(Eye, Primitive::Triangles, 0) == Matrix4x4::Identity());
}
