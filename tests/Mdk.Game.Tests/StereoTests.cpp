#include <utility>

#include <doctest.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/Kurt/FollowCamera.h"
#include "Asserts.h"

using namespace Mdk;
using namespace Mdk::Engine::Render;
using namespace Mdk::Game::Kurt;

// The stereo eyes (View::Eye): the eyes' images meet at the convergence
// distance, everything farther converges behind the screen and everything nearer in front, the
// sky (a direction: no distance) keeps the frustums' shift, the full eye separation over it.
namespace
{
    constexpr float Tolerance = 1e-3f;
    constexpr float Separation = 0.5f;
    constexpr float Convergence = 10.f;
    constexpr float Near = 0.5f;
    constexpr float Far = 20000.f;
    constexpr float FieldOfView = 71.5f;
    constexpr float Aspect = 4.f / 3.f;
    // The projection's horizontal scale (System.Numerics' perspective matrix).
    constexpr float ScaleX = 1.f / (Aspect * 0.71984f);
    const Vector3 Position(1.f, 2.f, 3.f);
    const Vector3 Forward = Vector3::Normalize(Vector3(0.2f, 1.f, -0.1f));
    const Vector3 Up(0.f, 0.f, 1.f);
    // Enum.GetValues<Stereo>().Length.
    constexpr size_t StereoCount = static_cast<size_t>(Stereo::Vr) + 1;

    View Center() { return CameraMath::View(Position, Forward, Up, FieldOfView, Aspect, Near, Far); }

    std::pair<View, View> Eyes(float convergence)
    {
        return {Center().Eye(-1.f, Separation, convergence), Center().Eye(+1.f, Separation, convergence)};
    }

    // Where a world point (w = 1) or direction (w = 0) lands in NDC x.
    float NdcX(const View& view, Vector3 point, float w = 1.f)
    {
        auto clip = Vector4::Transform(Vector4(point, w), view.ViewProjection);
        return clip.X / clip.W;
    }
}

TEST_CASE("StereoTests.NoStereoLeavesTheView")
{
    auto view = Center();
    CHECK(view.ViewProjection == view.Eye(0.f, Separation, Convergence).ViewProjection);
}

TEST_CASE("StereoTests.WithoutACameraTheViewStays")
{
    View view{Matrix4x4::Identity(), Matrix4x4::Identity(), Vector3::Zero()};
    CHECK(Matrix4x4::Identity() == view.Eye(-1.f, Separation, Convergence).ViewProjection);
    CHECK(Matrix4x4::Identity() == view.Eye(+1.f, Separation, Convergence).ViewProjection);
}

TEST_CASE("StereoTests.TheConvergencePlaneMeets")
{
    auto [left, right] = Eyes(Convergence);
    auto point = Position + Forward * Convergence;

    CHECK_NEAR(0.f, NdcX(left, point), Tolerance);
    CHECK_NEAR(0.f, NdcX(right, point), Tolerance);
}

TEST_CASE("StereoTests.FartherThanConvergenceGoesUncrossed")
{
    // The left eye's image to the left, the right eye's to the right: it appears behind.
    auto [left, right] = Eyes(Convergence);
    auto farPoint = Position + Forward * 200.f;

    CHECK(NdcX(left, farPoint) < -Tolerance);
    CHECK_NEAR(NdcX(left, farPoint), -NdcX(right, farPoint), Tolerance);
}

TEST_CASE("StereoTests.NearerThanConvergenceGoesCrossed")
{
    auto [left, right] = Eyes(Convergence);
    auto nearPoint = Position + Forward * (Convergence * 0.5f);

    CHECK(NdcX(left, nearPoint) > Tolerance);
    CHECK_NEAR(NdcX(left, nearPoint), -NdcX(right, nearPoint), Tolerance);
}

TEST_CASE("StereoTests.TheSkyKeepsItsFullSeparation")
{
    // A direction (w 0): the two cameras see it along their axes, so its NDC is the frustums'
    // shift alone (the full eye separation over the convergence distance).
    for (float convergence : {3.f, 10.f, 1000.f})
    {
        CAPTURE(convergence);
        auto [left, right] = Eyes(convergence);
        float apart = Separation * ScaleX / (2.f * convergence);

        CHECK_NEAR(apart, NdcX(right, Forward, 0.f), Tolerance);
        CHECK_NEAR(-apart, NdcX(left, Forward, 0.f), Tolerance);
    }

    CHECK_NEAR(0.f, NdcX(Center(), Forward, 0.f), Tolerance);
}

TEST_CASE("StereoTests.FullWidthHalvesTheEyesAspect")
{
    // Full side by side (VR viewers, PC glasses, crossview): each eye sees its half as it is,
    // so its camera has the half's aspect; half side by side is stretched back by the TV.
    const std::pair<Stereo, float> cases[] = {
        {Stereo::Off, Aspect},
        {Stereo::Sbs, Aspect},
        {Stereo::Interlaced, Aspect},
        {Stereo::Tab, Aspect},
        {Stereo::SbsFull, Aspect / 2.f},
        {Stereo::CrossView, Aspect / 2.f},
        {Stereo::Vr, Aspect / 2.f},
    };
    for (const auto& [mode, expected] : cases)
    {
        CAPTURE(static_cast<int>(mode));
        CHECK_NEAR(expected, StereoModes::EyeAspect(mode, Aspect), Tolerance);
    }
}

TEST_CASE("StereoTests.FullSideBySideParses")
{
    Stereo stereo{};
    CHECK(StereoModes::TryParse("sbsfull", stereo));
    CHECK(Stereo::SbsFull == stereo);
    CHECK(StereoCount == StereoModes::Names.size());
}

TEST_CASE("StereoTests.TheVrViewerParses")
{
    Stereo stereo{};
    CHECK(StereoModes::TryParse("vr", stereo));
    CHECK(Stereo::Vr == stereo);
}

TEST_CASE("StereoTests.TheVrViewerDrawsTheCanvasSmaller")
{
    // The viewer's lenses blur the edges: the HUD and the menus keep to the middle.
    float zoom = StereoModes::CanvasZoom(Stereo::Vr);
    CHECK(zoom >= 0.5f);
    CHECK(zoom <= 0.9f);
    CHECK(1.f == StereoModes::CanvasZoom(Stereo::SbsFull));
    CHECK(1.f == StereoModes::CanvasZoom(Stereo::Off));
}
