using System.Drawing;
using System.Numerics;
using Mdk.Game.Kurt;
using Mdk.Game.Menu;

namespace Mdk.Game.Tests;

/// <summary>Screens of other shapes than 4:3 (a 16:10 tablet, a near-square foldable): the 2D
/// screens fit whole, the 3D view keeps the 4:3 view's width.</summary>
public class AspectTests
{
    private const float CanvasHeight = 360f;

    [Theory]
    [InlineData(ScreenView.Fit.Inside, 480f)]
    [InlineData(ScreenView.Fit.Inside, 576f)]
    [InlineData(ScreenView.Fit.Inside, 391f)]
    [InlineData(ScreenView.Fit.Height, 576f)]
    [InlineData(ScreenView.Fit.Height, 391f)]
    [InlineData(ScreenView.Fit.Height, 331f)]
    public void TwoDScreensAreNeverCut(ScreenView.Fit fit, float canvasWidth)
    {
        var canvas = new Vector2(canvasWidth, CanvasHeight);
        var (scale, origin) = ScreenView.Place(fit, ScreenView.Size, canvas);

        Assert.True(origin.X >= -0.001f && origin.Y >= -0.001f);
        Assert.True(origin.X + ScreenView.Size.X * scale <= canvas.X + 0.001f);
        Assert.True(origin.Y + ScreenView.Size.Y * scale <= canvas.Y + 0.001f);
    }

    [Fact]
    public void TwoDScreensStayCentredBesideACutOut()
    {
        // A phone in landscape (emulator, 2400 x 1080 as canvas units): the cut-out's 69 on the
        // left, rounded corners' 26 on the right, 21 above, 28 below.
        var canvas = new Vector2(800f, CanvasHeight);
        var safe = RectangleF.FromLTRB(69f, 21f, 774f, 332f);

        var place = ScreenView.Centred(safe, canvas);

        Assert.Equal(RectangleF.FromLTRB(69f, 28f, 731f, 332f), place);
        Assert.Equal(canvas.X / 2f, place.X + place.Width / 2f, 3);
        Assert.Equal(new RectangleF(0f, 0f, 800f, CanvasHeight), ScreenView.Centred(new RectangleF(0f, 0f, 800f, CanvasHeight), canvas));
    }

    [Fact]
    public void TheLoadingScreenFillsA4By3CanvasHeight()
    {
        // The original's framing: the 600-wide screen at full height, its sides cut.
        var (scale, origin) = ScreenView.Place(ScreenView.Fit.Height, ScreenView.Size, new Vector2(480f, CanvasHeight));
        Assert.Equal(1f, scale);
        Assert.Equal(new Vector2(-60f, 0f), origin);
    }

    [Fact]
    public void A16By10CanvasHasBarsAboveAndBelow()
    {
        var (scale, origin) = ScreenView.Place(ScreenView.Fit.Height, ScreenView.Size, new Vector2(576f, CanvasHeight));
        Assert.Equal(0.96f, scale, 4);
        Assert.Equal(0f, origin.X, 3);
        Assert.Equal(7.2f, origin.Y, 3);
    }

    [Theory]
    [InlineData(4f / 3f)]
    [InlineData(16f / 9f)]
    public void FourByThreeAndWiderKeepTheFieldOfView(float aspect)
    {
        Assert.Equal(FollowCamera.FieldOfView, CameraMath.KeepWidth(FollowCamera.FieldOfView, aspect));
    }

    [Theory]
    [InlineData(2344f / 2156f)]
    [InlineData(1f)]
    [InlineData(3f / 4f)]
    public void ANarrowerScreenSeesAs4By3Wide(float aspect)
    {
        // The horizontal half-angle's tangent: tan(vertical / 2) × aspect.
        static float Width(float fieldOfView, float aspect) => MathF.Tan(float.DegreesToRadians(fieldOfView) / 2f) * aspect;

        var widened = CameraMath.KeepWidth(FollowCamera.FieldOfView, aspect);
        Assert.True(widened > FollowCamera.FieldOfView);
        Assert.Equal(Width(FollowCamera.FieldOfView, 4f / 3f), Width(widened, aspect), 4);
    }
}
