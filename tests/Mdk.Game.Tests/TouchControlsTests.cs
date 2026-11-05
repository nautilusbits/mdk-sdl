using System.Drawing;
using System.Numerics;
using System.Reflection;
using Mdk.Engine.Platform;
using SDL;
using static SDL.SDL3;

namespace Mdk.Game.Tests;

/// <summary>The on-screen controls of a touch screen (Android): the stick, the buttons, looking.</summary>
public class TouchControlsTests
{
    /// <summary>A phone held sideways (window pixels).</summary>
    private static readonly Vector2 Screen = new(2400f, 1080f);
    private const ulong First = 1;
    private const ulong Second = 2;

    private static (TouchControls Touch, Input Input) New()
    {
        var input = new Input();
        input.BeginFrame();
        return (new TouchControls(), input);
    }

    private static Vector2 CentreOf(TouchControls touch, Key key)
    {
        var area = touch.Layout(Screen).First(b => b.Key == key).Area;
        return new Vector2(area.X + area.Width / 2f, area.Y + area.Height / 2f);
    }

    [Theory]
    [InlineData(Key.Fire)]
    [InlineData(Key.Jump)]
    [InlineData(Key.Sniper)]
    public void AButtonHoldsItsKeyWhileTouched(Key key)
    {
        var (touch, input) = New();

        touch.Down(First, CentreOf(touch, key), Screen, input);
        Assert.True(input.IsDown(key));
        Assert.True(input.WasPressed(key));

        touch.Up(First, input);
        Assert.False(input.IsDown(key));
    }

    [Fact]
    public void PauseOpensThePauseMenu()
    {
        var (touch, input) = New();
        touch.Down(First, CentreOf(touch, Key.Escape), Screen, input);
        Assert.True(input.WasPressed(MenuKey.Back));
    }

    [Fact]
    public void TheStickWalksWhereItIsPushed()
    {
        var (touch, input) = New();
        var origin = new Vector2(400f, 700f);
        var radius = touch.StickRadius(Screen);

        touch.Down(First, origin, Screen, input);
        Assert.False(input.IsDown(Key.Forward));

        touch.Move(First, origin - new Vector2(0f, radius), Screen, input);
        Assert.True(input.IsDown(Key.Forward));
        Assert.False(input.IsDown(Key.StrafeLeft));

        touch.Move(First, origin + new Vector2(-radius, radius), Screen, input);
        Assert.False(input.IsDown(Key.Forward));
        Assert.True(input.IsDown(Key.Back));
        Assert.True(input.IsDown(Key.StrafeLeft));

        touch.Up(First, input);
        Assert.False(input.IsDown(Key.Back));
        Assert.False(input.IsDown(Key.StrafeLeft));
    }

    [Fact]
    public void TestHoldsDontReleaseTouches()
    {
        var (touch, input) = New();
        var origin = new Vector2(400f, 700f);

        // The viewer releases its test keys every frame.
        touch.Down(First, origin, Screen, input);
        touch.Move(First, origin - new Vector2(0f, touch.StickRadius(Screen)), Screen, input);
        touch.Down(Second, CentreOf(touch, Key.Fire), Screen, input);
        input.Hold(Key.Forward, Input.State.Up);
        input.Hold(Key.Fire, Input.State.Up);

        Assert.True(input.IsDown(Key.Forward));
        Assert.True(input.IsDown(Key.Fire));
    }

    [Fact]
    public void DraggingTheRightSideLooks()
    {
        var (touch, input) = New();
        var at = new Vector2(1500f, 300f);

        touch.Down(First, at, Screen, input);
        touch.Move(First, at + new Vector2(20f, -10f), Screen, input);
        Assert.True(input.MouseX > 0f);
        Assert.True(input.MouseY < 0f);
    }

    [Fact]
    public void DraggingFireAimsWhileFiring()
    {
        var (touch, input) = New();
        var at = CentreOf(touch, Key.Fire);

        touch.Down(First, at, Screen, input);
        touch.Move(First, at + new Vector2(-300f, -120f), Screen, input);
        Assert.True(input.IsDown(Key.Fire));
        Assert.True(input.MouseX < 0f);
        Assert.True(input.MouseY < 0f);
    }

    [Fact]
    public void FingersAreIndependent()
    {
        var (touch, input) = New();
        var origin = new Vector2(400f, 700f);

        touch.Down(First, origin, Screen, input);
        touch.Move(First, origin - new Vector2(0f, touch.StickRadius(Screen)), Screen, input);
        touch.Down(Second, CentreOf(touch, Key.Fire), Screen, input);
        touch.Up(Second, input);

        Assert.True(input.IsDown(Key.Forward));
        Assert.False(input.IsDown(Key.Fire));
    }

    [Fact]
    public void ReleaseLetsEverythingGo()
    {
        var (touch, input) = New();
        touch.Down(First, CentreOf(touch, Key.Fire), Screen, input);
        touch.Down(Second, new Vector2(400f, 700f), Screen, input);
        touch.Move(Second, new Vector2(400f, 400f), Screen, input);

        touch.Release(input);
        Assert.False(input.IsDown(Key.Fire));
        Assert.False(input.IsDown(Key.Forward));
        Assert.Null(touch.Stick);
    }

    [Theory]
    [InlineData(TouchButtons.Auto, InputSource.Touch, true)]
    [InlineData(TouchButtons.Auto, InputSource.Device, false)]
    [InlineData(TouchButtons.Off, InputSource.Touch, false)]
    [InlineData(TouchButtons.On, InputSource.Device, true)]
    public void ShownByTheSettingAndTheLastInput(TouchButtons mode, InputSource last, bool shown)
    {
        // Auto: a gamepad, a keyboard or a mouse hides them; a touch shows them again.
        Assert.Equal(shown, TouchControls.Shown(mode, last));
    }

    /// <summary>A 16:10 tablet (2880 x 1800, 320 dpi: 12.6 px/mm).</summary>
    private static readonly Vector2 Tablet = new(2880f, 1800f);
    private const float TabletDensity = 320f / 25.4f;
    /// <summary>A phone's 420 dpi.</summary>
    private const float PhoneDensity = 420f / 25.4f;
    private const float ButtonMm = 11f;

    [Fact]
    public void ATabletsButtonsAreAFingertipWide()
    {
        // Not 15 % of 1800 pixels (270): 11 mm.
        var touch = new TouchControls();
        touch.Measure(TabletDensity, RectangleF.Empty);

        var fire = touch.Layout(Tablet).First(b => b.Key == Key.Fire).Area;
        Assert.Equal(ButtonMm * TabletDensity, fire.Width, 0.01f);
        Assert.Equal(fire.Width, fire.Height);
        Assert.True(touch.StickRadius(Tablet) < 0.12f * Tablet.Y);
    }

    [Fact]
    public void APhonesButtonsKeepTheirPartOfTheHeight()
    {
        // 11 mm would be 182 pixels: a phone keeps 15 % of its height (162).
        var touch = new TouchControls();
        touch.Measure(PhoneDensity, RectangleF.Empty);

        var fire = touch.Layout(Screen).First(b => b.Key == Key.Fire).Area;
        Assert.Equal(0.15f * Screen.Y, fire.Width, 0.01f);
    }

    [Fact]
    public void AnUnknownDisplayUsesTheHeight()
    {
        var touch = new TouchControls();
        touch.Measure(0f, RectangleF.Empty);
        Assert.Equal(0.15f * Tablet.Y, touch.Layout(Tablet).First(b => b.Key == Key.Fire).Area.Width, 0.01f);
        Assert.Equal(0.12f * Tablet.Y, touch.StickRadius(Tablet), 0.01f);
    }

    [Fact]
    public void ButtonsKeepInsideTheSafeArea()
    {
        // A cut-out on the right and a rounded top: the buttons move in.
        var touch = new TouchControls();
        var safe = new RectangleF(0f, 60f, Tablet.X - 100f, Tablet.Y - 60f);
        touch.Measure(TabletDensity, safe);

        var layout = touch.Layout(Tablet);
        Assert.All(layout, b => Assert.True(b.Area.Right <= safe.Right && b.Area.Top >= safe.Top));
    }

    [Fact]
    public void AnotherDisplayLaysThemOutAgain()
    {
        var touch = new TouchControls();
        touch.Measure(0f, RectangleF.Empty);
        var before = touch.Layout(Tablet).First(b => b.Key == Key.Fire).Area.Width;

        touch.Measure(TabletDensity, RectangleF.Empty);
        Assert.NotEqual(before, touch.Layout(Tablet).First(b => b.Key == Key.Fire).Area.Width);
    }

    [Fact]
    public void AHeldButtonIsPressed()
    {
        var (touch, input) = New();
        touch.Down(First, CentreOf(touch, Key.Jump), Screen, input);
        Assert.True(touch.Held(Key.Jump));
        Assert.False(touch.Held(Key.Fire));

        touch.Up(First, input);
        Assert.False(touch.Held(Key.Jump));
    }

    [Theory]
    [InlineData(2.625f, 1f, ScreenDensity.AndroidDpi, 16.54f)]
    [InlineData(1f, 1f, ScreenDensity.DesktopDpi, 3.78f)]
    [InlineData(2f, 2f, ScreenDensity.DesktopDpi, 3.78f)]
    [InlineData(0f, 1f, ScreenDensity.AndroidDpi, 0f)]
    public void UnitsPerMmFollowTheDisplayScale(float scale, float pixelDensity, float baseDpi, float expected)
    {
        // A Retina window's units are points: two pixels each.
        Assert.Equal(expected, ScreenDensity.UnitsPerMm(scale, pixelDensity, baseDpi), 0.01f);
    }

    [Fact]
    public void AFingersClickReleaseReachesPlay()
    {
        // A menu tap (SDL's left click of a finger) starts play: its release must still let go of
        // the click, or the left button (fire) stays held (issue #6).
        var isTouch = typeof(Window).GetMethod("IsTouch", BindingFlags.NonPublic | BindingFlags.Static)!;
        var up = new SDL_Event
        {
            button = new SDL_MouseButtonEvent { type = SDL_EventType.SDL_EVENT_MOUSE_BUTTON_UP, which = SDL_TOUCH_MOUSEID, button = (byte)SDL_BUTTON_LEFT },
        };

        Assert.False((bool)isTouch.Invoke(null, [up])!);
    }
}
