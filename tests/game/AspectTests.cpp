#include <tuple>
#include <utility>

#include <doctest.h>

#include "Asserts.h"
#include "core/DotNet.h"
#include "core/Drawing.h"
#include "core/Numerics.h"
#include "game/Angles.h"
#include "game/kurt/FollowCamera.h"
#include "game/menu/Ui.h"

using namespace Mdk;
using namespace Mdk::Game::Kurt;
using namespace Mdk::Game::Menu;

// Screens of other shapes than 4:3 (a 16:10 tablet, a near-square foldable): the 2D
// screens fit whole, the 3D view keeps the 4:3 view's width.

namespace
{
    constexpr float CanvasHeight = 360.f;
}

TEST_CASE("AspectTests.TwoDScreensAreNeverCut")
{
    const std::pair<ScreenView::Fit, float> data[] = {
        {ScreenView::Fit::Inside, 480.f},
        {ScreenView::Fit::Inside, 576.f},
        {ScreenView::Fit::Inside, 391.f},
        {ScreenView::Fit::Height, 576.f},
        {ScreenView::Fit::Height, 391.f},
        {ScreenView::Fit::Height, 331.f},
    };
    for (auto [fit, canvasWidth] : data)
    {
        CAPTURE(canvasWidth);
        Vector2 canvas(canvasWidth, CanvasHeight);
        auto [scale, origin] = ScreenView::Place(fit, ScreenView::Size, canvas);

        CHECK((origin.X >= -0.001f && origin.Y >= -0.001f));
        CHECK(origin.X + ScreenView::Size.X * scale <= canvas.X + 0.001f);
        CHECK(origin.Y + ScreenView::Size.Y * scale <= canvas.Y + 0.001f);
    }
}

TEST_CASE("AspectTests.TwoDScreensStayCentredBesideACutOut")
{
    // A phone in landscape (emulator, 2400 x 1080 as canvas units): the cut-out's 69 on the
    // left, rounded corners' 26 on the right, 21 above, 28 below.
    Vector2 canvas(800.f, CanvasHeight);
    auto safe = RectangleF::FromLTRB(69.f, 21.f, 774.f, 332.f);

    auto place = ScreenView::Centred(safe, canvas);

    CHECK(RectangleF::FromLTRB(69.f, 28.f, 731.f, 332.f) == place);
    CHECK_PRECISION(canvas.X / 2.f, place.X + place.Width / 2.f, 3);
    CHECK(RectangleF(0.f, 0.f, 800.f, CanvasHeight) == ScreenView::Centred(RectangleF(0.f, 0.f, 800.f, CanvasHeight), canvas));
}

TEST_CASE("AspectTests.TheLoadingScreenFillsA4By3CanvasHeight")
{
    // The original's framing: the 600-wide screen at full height, its sides cut.
    auto [scale, origin] = ScreenView::Place(ScreenView::Fit::Height, ScreenView::Size, Vector2(480.f, CanvasHeight));
    CHECK(1.f == scale);
    CHECK(Vector2(-60.f, 0.f) == origin);
}

TEST_CASE("AspectTests.A16By10CanvasHasBarsAboveAndBelow")
{
    auto [scale, origin] = ScreenView::Place(ScreenView::Fit::Height, ScreenView::Size, Vector2(576.f, CanvasHeight));
    CHECK_PRECISION(0.96f, scale, 4);
    CHECK_PRECISION(0.f, origin.X, 3);
    CHECK_PRECISION(7.2f, origin.Y, 3);
}

TEST_CASE("AspectTests.FourByThreeAndWiderKeepTheFieldOfView")
{
    for (float aspect : {4.f / 3.f, 16.f / 9.f})
    {
        CAPTURE(aspect);
        CHECK(FollowCamera::FieldOfView == CameraMath::KeepWidth(FollowCamera::FieldOfView, aspect));
    }
}

TEST_CASE("AspectTests.ANarrowerScreenSeesAs4By3Wide")
{
    // The horizontal half-angle's tangent: tan(vertical / 2) × aspect.
    auto width = [](float fieldOfView, float aspect) { return MathF::Tan(Mdk::Game::DegreesToRadians(fieldOfView) / 2.f) * aspect; };

    for (float aspect : {2344.f / 2156.f, 1.f, 3.f / 4.f})
    {
        CAPTURE(aspect);
        auto widened = CameraMath::KeepWidth(FollowCamera::FieldOfView, aspect);
        CHECK(widened > FollowCamera::FieldOfView);
        CHECK_PRECISION(width(FollowCamera::FieldOfView, 4.f / 3.f), width(widened, aspect), 4);
    }
}
