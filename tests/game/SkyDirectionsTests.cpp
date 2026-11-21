#include <cmath>
#include <tuple>

#include <doctest.h>

#include "core/DotNet.h"
#include "core/Numerics.h"
#include "engine/render/Renderer.h"
#include "game/kurt/FollowCamera.h"

using namespace Mdk;
using namespace Mdk::Engine::Render;
using namespace Mdk::Game::Kurt;

// The sky's clip-to-direction matrix (ViewCamera::SkyDirections) is built from the
// camera's axes: the same directions as inverting rotation × projection, without that inverse's
// float32 error, which changed with the camera's angle and shook the sky when turning.
namespace
{
    constexpr float Near = 0.5f;
    constexpr float Far = 20000.f;
    // float.DegreesToRadians.
    constexpr float RadiansPerDegree = MathF::PI / 180.f;

    struct CameraCase
    {
        float Yaw;
        float Pitch;
        float Aspect;
    };

    const CameraCase Cameras[] = {
        {0.f, -10.f, 16.f / 9.f},
        {37.5f, 4.f, 16.f / 9.f},
        {123.4f, -25.f, 32.f / 9.f},
        {271.f, 12.f, 4.f / 3.f},
    };

    enum class Lens { Normal, Scope };

    struct Exact3
    {
        double X;
        double Y;
        double Z;
    };

    View ViewAt(float yaw, float pitch, float aspect, Lens lens)
    {
        auto [yawSin, yawCos] = MathF::SinCos(yaw * RadiansPerDegree);
        auto [pitchSin, pitchCos] = MathF::SinCos(pitch * RadiansPerDegree);
        Vector3 forward(yawCos * pitchCos, yawSin * pitchCos, pitchSin);
        if (lens == Lens::Scope)
        {
            return CameraMath::ScopeView(Vector3::Zero(), forward, 0.5f, aspect, Near, Far);
        }

        return CameraMath::View(Vector3::Zero(), forward, FollowCamera::UpOf(forward), 60.f, aspect, Near, Far);
    }

    Vector3 Direction(const Matrix4x4& clipToDirection, float x, float y)
    {
        auto p = Vector4::Transform(Vector4(x, y, 1.f, 1.f), clipToDirection);
        return Vector3::Normalize(Vector3(p.X, p.Y, p.Z) / p.W);
    }

    // A pixel's direction as sky.hlsl gets it: the matrix gives the screen triangle's corners
    // ((-1, -1), (3, -1), (-1, 3), at clip depth 1, divided by w), the GPU blends them linearly to the
    // pixel, and the pixel normalizes the blend. It's right only if the corners lie in one plane.
    Vector3 ShaderDirection(const Matrix4x4& clipToDirection, float x, float y)
    {
        auto corner = [&](float cx, float cy)
        {
            auto p = Vector4::Transform(Vector4(cx, cy, 1.f, 1.f), clipToDirection);
            return Vector3(p.X, p.Y, p.Z) / p.W;
        };

        float b1 = (x + 1.f) / 4.f, b2 = (y + 1.f) / 4.f;
        return Vector3::Normalize(corner(-1.f, -1.f) * (1.f - b1 - b2) + corner(3.f, -1.f) * b1 + corner(-1.f, 3.f) * b2);
    }

    // The exact direction through clip point (x, y), in doubles.
    Exact3 Exact(const ViewCamera& camera, double x, double y)
    {
        auto f = Vector3::Normalize(camera.Forward);
        auto r = Vector3::Normalize(Vector3::Cross(f, camera.Up));
        auto u = Vector3::Cross(r, f);
        const auto& p = camera.Projection;
        double a = (x + p.M31) / p.M11, b = (y + p.M32) / p.M22;
        double dx = f.X + r.X * a + u.X * b, dy = f.Y + r.Y * a + u.Y * b, dz = f.Z + r.Z * a + u.Z * b;
        double length = std::sqrt(dx * dx + dy * dy + dz * dz);
        return {dx / length, dy / length, dz / length};
    }

    // How far a unit direction is from the exact one (the chord: the angle, for small ones;
    // acos of the dot product loses its precision there).
    double Angle(Vector3 d, Exact3 exact)
    {
        return std::sqrt((d.X - exact.X) * (d.X - exact.X) + (d.Y - exact.Y) * (d.Y - exact.Y) + (d.Z - exact.Z) * (d.Z - exact.Z));
    }
}

TEST_CASE("SkyDirectionsTests.SkyDirectionsAreTheInversesDirections")
{
    for (const auto& c : Cameras)
    {
        CAPTURE(c.Yaw);
        for (auto lens : {Lens::Normal, Lens::Scope})
        {
            auto view = ViewAt(c.Yaw, c.Pitch, c.Aspect, lens);
            REQUIRE(view.Camera.has_value());
            auto sky = view.Camera->SkyDirections();
            for (float x = -1.f; x <= 1.f; x += 0.5f)
            {
                for (float y = -1.f; y <= 1.f; y += 0.5f)
                {
                    auto ours = Direction(sky, x, y);
                    auto inverse = Direction(view.ClipToDirection, x, y);
                    INFO("(", x, ", ", y, ")");
                    CHECK(Vector3::Distance(ours, inverse) < 1e-3f);
                }
            }
        }
    }
}

TEST_CASE("SkyDirectionsTests.SkyDirectionsStaySteadyAllTheWayRound")
{
    // Turning a full circle (32:9, looking 4 degrees up), as sky.hlsl draws the directions: ours
    // stay within float rounding everywhere. The inverse's corners leave their plane at some
    // angles (by 0.002 rad at yaw 37.5: about 3 pixels of a 1440-high view), so the sky shook.
    constexpr float Pitch = 4.f;
    constexpr float Aspect = 32.f / 9.f;
    double ours = 0.0, inverse = 0.0;
    for (float yaw = 0.f; yaw < 360.f; yaw += 0.5f)
    {
        auto view = ViewAt(yaw, Pitch, Aspect, Lens::Normal);
        REQUIRE(view.Camera.has_value());
        const auto& camera = *view.Camera;
        auto sky = camera.SkyDirections();
        for (float x = -1.f; x <= 1.f; x += 0.5f)
        {
            for (float y = -1.f; y <= 1.f; y += 0.5f)
            {
                auto exact = Exact(camera, x, y);
                ours = Math::Max(ours, Angle(ShaderDirection(sky, x, y), exact));
                inverse = Math::Max(inverse, Angle(ShaderDirection(view.ClipToDirection, x, y), exact));
            }
        }
    }

    INFO("ours ", ours, ", inverse ", inverse);
    CHECK(ours < 1e-5);
    CHECK(inverse > 1e-4);
}
