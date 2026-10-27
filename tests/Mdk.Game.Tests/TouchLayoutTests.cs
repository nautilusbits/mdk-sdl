using System.Drawing;
using System.Numerics;
using Mdk.Engine.Platform;
using Mdk.Game.Flow;
using Mdk.Game.Menu;

namespace Mdk.Game.Tests;

/// <summary>The player's places of the on-screen controls: saved, fitted to other displays, edited.</summary>
public class TouchLayoutTests
{
    /// <summary>A phone held sideways (420 dpi) with a cut-out on the left.</summary>
    private static readonly Vector2 Phone = new(2400f, 1080f);
    private static readonly RectangleF PhoneSafe = new(90f, 0f, 2310f, 1080f);
    private const float PhoneDensity = 420f / 25.4f;
    /// <summary>A foldable's inner display (near square) and a 16:10 tablet.</summary>
    private static readonly Vector2 Inner = new(2208f, 1768f);
    private const float InnerDensity = 373f / 25.4f;
    private static readonly Vector2 Tablet = new(2880f, 1800f);
    private const float TabletDensity = 320f / 25.4f;
    private static readonly Dictionary<TouchPart, TouchPlace> None = [];
    private const float Tolerance = 0.01f;

    private static Vector2 CentreOf(RectangleF area) => new(area.X + area.Width / 2f, area.Y + area.Height / 2f);

    private static TouchEditor Editor(IReadOnlyDictionary<TouchPart, TouchPlace>? saved = null)
    {
        var editor = new TouchEditor(saved ?? None);
        editor.Measure(Phone, PhoneSafe, PhoneDensity);
        return editor;
    }

    [Fact]
    public void APlaceRoundTripsThroughText()
    {
        var place = new TouchPlace(Across.Right, 0.0113f, Along.Bottom, 0.2f, 9.8f, 9.8f);
        Assert.Equal("Right,0.0113,Bottom,0.2,9.8,9.8", place.ToString());
        Assert.True(TouchPlace.TryParse(place.ToString(), out var parsed));
        Assert.Equal(place, parsed);
    }

    [Theory]
    [InlineData("")]
    [InlineData("Right,0.1,Bottom,0.2,9")]
    [InlineData("Up,0.1,Bottom,0.2,9,9")]
    [InlineData("Right,1.5,Bottom,0.2,9,9")]
    [InlineData("Right,0.1,Bottom,0.2,0,9")]
    [InlineData("Right,0.1,Bottom,0.2,NaN,9")]
    public void BadPlacesAreRefused(string text) => Assert.False(TouchPlace.TryParse(text, out _));

    [Fact]
    public void SettingsKeepThePlacesAndTheOpacity()
    {
        var settings = new Settings { TouchOpacity = 50 };
        settings.TouchPlaces[TouchPart.Fire] = new TouchPlace(Across.Right, 0.05f, Along.Bottom, 0.3f, 12f, 12f);
        settings.TouchPlaces[TouchPart.Stick] = new TouchPlace(Across.Left, 0f, Along.Bottom, 0f, 40f, 30f);

        var text = settings.Format();
        Assert.Contains("touch.Fire=Right,0.05,Bottom,0.3,12,12\n", text);
        var parsed = Settings.Parse(text);
        Assert.Equal(50, parsed.TouchOpacity);
        Assert.Equal(settings.TouchPlaces, parsed.TouchPlaces);
    }

    [Fact]
    public void NothingSavedKeepsTheDefaults()
    {
        var settings = Settings.Parse("touch.Fire=broken\ntouch.Nose=Right,0,Bottom,0,9,9\ntouch_opacity=3\n");
        Assert.Empty(settings.TouchPlaces);
        Assert.Equal(Settings.MinOpacity, settings.TouchOpacity);
        Assert.DoesNotContain("touch.", new Settings().Format());

        foreach (var part in TouchLayout.Parts)
        {
            Assert.Equal(TouchLayout.DefaultArea(part, Phone, PhoneSafe, PhoneDensity), TouchLayout.AreaOf(part, None, Phone, PhoneSafe, PhoneDensity));
        }
    }

    [Fact]
    public void AnAreaRoundTripsThroughItsPlace()
    {
        var area = new RectangleF(1900f, 700f, 180f, 180f);
        var place = TouchLayout.PlaceOf(area, Phone, PhoneSafe, PhoneDensity);
        Assert.Equal(Across.Right, place.X);
        Assert.Equal(Along.Bottom, place.Y);

        var back = TouchLayout.Place(place, Phone, PhoneSafe, PhoneDensity);
        Assert.Equal(area.X, back.X, Tolerance);
        Assert.Equal(area.Y, back.Y, Tolerance);
        Assert.Equal(area.Width, back.Width, Tolerance);
    }

    [Fact]
    public void APlaceKeepsItsEdgeAndSizeOnAnotherDisplay()
    {
        // A button 2 % of the width from the right edge, 11 mm: on the tablet as far (in parts)
        // from its right edge, still 11 mm.
        var place = new TouchPlace(Across.Right, 0.02f, Along.Bottom, 0.1f, 11f, 11f);
        var area = TouchLayout.AreaOf(TouchPart.Fire, new Dictionary<TouchPart, TouchPlace> { [TouchPart.Fire] = place }, Tablet, RectangleF.Empty, TabletDensity);

        Assert.Equal(Tablet.X - 0.02f * Tablet.X, area.Right, Tolerance);
        Assert.Equal(Tablet.Y - 0.1f * Tablet.Y, area.Bottom, Tolerance);
        Assert.Equal(11f * TabletDensity, area.Width, Tolerance);
    }

    [Theory]
    [InlineData(TouchPart.Fire)]
    [InlineData(TouchPart.Pause)]
    [InlineData(TouchPart.Stick)]
    public void EveryPartFitsAFoldablesOtherDisplay(TouchPart part)
    {
        // Placed on the outer display (a wide phone), opened on the inner one (near square).
        var area = TouchLayout.DefaultArea(part, Phone, PhoneSafe, PhoneDensity);
        var places = new Dictionary<TouchPart, TouchPlace> { [part] = TouchLayout.PlaceOf(area, Phone, PhoneSafe, PhoneDensity) };
        var inner = TouchLayout.AreaOf(part, places, Inner, RectangleF.Empty, InnerDensity);

        Assert.InRange(inner.Left, 0f, Inner.X);
        Assert.InRange(inner.Right, 0f, Inner.X);
        Assert.InRange(inner.Top, 0f, Inner.Y);
        Assert.InRange(inner.Bottom, 0f, Inner.Y);
    }

    [Fact]
    public void FittingKeepsButtonsInsideAndBetweenTheirSizes()
    {
        var tiny = TouchLayout.Fit(TouchPart.Fire, new RectangleF(-50f, 1000f, 20f, 30f), Phone, PhoneSafe, PhoneDensity);
        Assert.Equal(TouchLayout.MinMm * PhoneDensity, tiny.Width, Tolerance);
        Assert.Equal(tiny.Width, tiny.Height);
        Assert.Equal(PhoneSafe.Left, tiny.Left);
        Assert.Equal(PhoneSafe.Bottom, tiny.Bottom, Tolerance);

        var huge = TouchLayout.Fit(TouchPart.Fire, new RectangleF(3000f, -10f, 900f, 900f), Phone, PhoneSafe, PhoneDensity);
        Assert.Equal(TouchLayout.MaxMm * PhoneDensity, huge.Width, Tolerance);
        Assert.Equal(PhoneSafe.Right, huge.Right, Tolerance);
        Assert.Equal(0f, huge.Top);
    }

    [Fact]
    public void TheStickZoneHoldsAFullPush()
    {
        var zone = TouchLayout.Fit(TouchPart.Stick, new RectangleF(100f, 100f, 10f, 5000f), Phone, PhoneSafe, PhoneDensity);
        var reach = new TouchControls().StickRadius(Phone);
        Assert.True(zone.Width >= 2f * reach - Tolerance);
        Assert.Equal(PhoneSafe.Height, zone.Height, Tolerance);
    }

    [Fact]
    public void OverlappingButtonsAreFound()
    {
        var areas = new Dictionary<TouchPart, RectangleF>
        {
            [TouchPart.Fire] = new(100f, 100f, 100f, 100f),
            [TouchPart.Jump] = new(150f, 150f, 100f, 100f),
            [TouchPart.Use] = new(200f, 100f, 100f, 100f),
            [TouchPart.Stick] = new(0f, 0f, 1000f, 1000f),
        };

        Assert.True(TouchLayout.Overlaps(TouchPart.Fire, areas));
        Assert.True(TouchLayout.Overlaps(TouchPart.Jump, areas));
        // Touching edges and the zone under the buttons are fine.
        areas.Remove(TouchPart.Jump);
        Assert.False(TouchLayout.Overlaps(TouchPart.Fire, areas));
        Assert.False(TouchLayout.Overlaps(TouchPart.Stick, areas));
    }

    [Fact]
    public void TheDefaultsDontOverlap()
    {
        var areas = TouchLayout.Parts.ToDictionary(p => p, p => TouchLayout.DefaultArea(p, Phone, PhoneSafe, PhoneDensity));
        Assert.All(TouchLayout.Parts, p => Assert.False(TouchLayout.Overlaps(p, areas)));
    }

    // --- Play: the placed controls, and fingers keeping their roles -------------------------------

    private static (TouchControls Touch, Input Input) Playing(Dictionary<TouchPart, TouchPlace> places)
    {
        var input = new Input();
        input.BeginFrame();
        var touch = new TouchControls();
        touch.Measure(PhoneDensity, PhoneSafe);
        touch.Arrange(places, 1f);
        return (touch, input);
    }

    [Fact]
    public void APlacedButtonPressesWhereItWasPut()
    {
        // Fire on the left, inside the stick's zone: the button wins.
        var area = new RectangleF(300f, 700f, 180f, 180f);
        var (touch, input) = Playing(new() { [TouchPart.Fire] = TouchLayout.PlaceOf(area, Phone, PhoneSafe, PhoneDensity) });

        touch.Down(1, CentreOf(area), Phone, input);
        Assert.True(input.IsDown(Key.Fire));
        Assert.Null(touch.Stick);
    }

    [Fact]
    public void APlacedZoneLeavesTheRestToLooking()
    {
        // The zone shrunk to the bottom left corner: the left side's top looks.
        var zone = new RectangleF(PhoneSafe.Left, 600f, 600f, 480f);
        var (touch, input) = Playing(new() { [TouchPart.Stick] = TouchLayout.PlaceOf(zone, Phone, PhoneSafe, PhoneDensity) });

        touch.Down(1, new Vector2(400f, 200f), Phone, input);
        Assert.Null(touch.Stick);
        touch.Move(1, new Vector2(420f, 200f), Phone, input);
        Assert.True(input.MouseX > 0f);

        touch.Down(2, new Vector2(400f, 900f), Phone, input);
        Assert.NotNull(touch.Stick);
    }

    [Fact]
    public void ALookSlidingOverAButtonDoesntPressIt()
    {
        var (touch, input) = Playing([]);
        var fire = CentreOf(touch.Layout(Phone).First(b => b.Key == Key.Fire).Area);
        // From the look area left of the buttons, across Jump to Fire.
        var start = fire - new Vector2(600f, 0f);

        touch.Down(1, start, Phone, input);
        touch.Move(1, fire, Phone, input);
        Assert.False(input.IsDown(Key.Fire));
        Assert.False(input.IsDown(Key.Jump));
        Assert.True(input.MouseX > 0f);
    }

    [Fact]
    public void AButtonsFingerDoesntLook()
    {
        var (touch, input) = Playing([]);
        var fire = CentreOf(touch.Layout(Phone).First(b => b.Key == Key.Fire).Area);

        touch.Down(1, fire, Phone, input);
        touch.Move(1, fire - new Vector2(300f, 300f), Phone, input);
        Assert.True(input.IsDown(Key.Fire));
        Assert.Equal(0f, input.MouseX);
        Assert.Equal(0f, input.MouseY);
        Assert.Null(touch.Stick);
    }

    [Fact]
    public void ThePointerIsHeldUntilLetGo()
    {
        // The editor drags while the menus' finger (the left button) stays down.
        var input = new Input();
        input.BeginFrame();
        input.SetButton(MouseButton.Left, Input.State.Down);
        input.BeginFrame();
        Assert.True(input.IsDown(Pointer.Left));
        Assert.False(input.WasClicked(Pointer.Left));

        input.SetButton(MouseButton.Left, Input.State.Up);
        Assert.False(input.IsDown(Pointer.Left));
    }

    [Fact]
    public void PauseCoveredByAButtonStillPauses()
    {
        var pause = TouchLayout.DefaultArea(TouchPart.Pause, Phone, PhoneSafe, PhoneDensity);
        var (touch, input) = Playing(new() { [TouchPart.Fire] = TouchLayout.PlaceOf(pause, Phone, PhoneSafe, PhoneDensity) });

        touch.Down(1, CentreOf(pause), Phone, input);
        Assert.True(input.WasPressed(MenuKey.Back));
        Assert.False(input.IsDown(Key.Fire));
    }

    [Fact]
    public void TheZoneLeavesRoomToLook()
    {
        var zone = TouchLayout.Fit(TouchPart.Stick, PhoneSafe, Phone, PhoneSafe, PhoneDensity);
        Assert.True(zone.Width < PhoneSafe.Width);
    }

    [Fact]
    public void AnEmptyWindowKeepsThePlaces()
    {
        // Mid-rotation (0 x 0): an edit can't make a place of nothing.
        var editor = Editor();
        editor.Select(1);
        editor.Measure(Vector2.Zero, RectangleF.Empty, 0f);
        editor.Grow(1);
        Assert.Empty(editor.Places());
    }

    [Fact]
    public void ButtonsKeepClearOfTheTools()
    {
        // Under a tool a button couldn't be picked again: it stops below them.
        var editor = Editor();
        var done = editor.Tools.First(t => t.Tool == TouchEditor.Tool.Done).Area;
        var fire = editor.Areas[TouchPart.Fire];
        editor.Press(CentreOf(fire));
        editor.Drag(CentreOf(fire) + (CentreOf(done) - CentreOf(fire)));
        editor.Release();

        Assert.All(editor.Tools, t => Assert.False(t.Area.IntersectsWith(editor.Areas[TouchPart.Fire])));
    }

    [Fact]
    public void WithholdingLetsThePointerGo()
    {
        var input = new Input();
        input.SetButton(MouseButton.Left, Input.State.Down);
        input.Withhold();
        Assert.False(input.IsDown(Pointer.Left));
    }

    // --- The editor ---------------------------------------------------------------------------

    private static Vector2 ToolAt(TouchEditor editor, TouchEditor.Tool tool) => CentreOf(editor.Tools.First(t => t.Tool == tool).Area);

    [Fact]
    public void TheEditorStartsFromTheSavedPlaces()
    {
        var area = new RectangleF(1000f, 400f, 200f, 200f);
        var saved = new Dictionary<TouchPart, TouchPlace> { [TouchPart.Jump] = TouchLayout.PlaceOf(area, Phone, PhoneSafe, PhoneDensity) };
        var editor = Editor(saved);

        Assert.Equal(area.X, editor.Areas[TouchPart.Jump].X, Tolerance);
        Assert.Equal(TouchLayout.DefaultArea(TouchPart.Fire, Phone, PhoneSafe, PhoneDensity), editor.Areas[TouchPart.Fire]);
        Assert.Equal(saved, editor.Places());
    }

    [Fact]
    public void DraggingAButtonMovesIt()
    {
        var editor = Editor();
        var before = editor.Areas[TouchPart.Fire];
        var grab = CentreOf(before) + new Vector2(10f, 10f);

        Assert.Equal(TouchEditor.Outcome.Editing, editor.Press(grab));
        Assert.Equal(TouchPart.Fire, editor.Selected);
        editor.Drag(grab - new Vector2(500f, 300f));
        editor.Release();

        var after = editor.Areas[TouchPart.Fire];
        Assert.Equal(before.X - 500f, after.X, Tolerance);
        Assert.Equal(before.Y - 300f, after.Y, Tolerance);
        Assert.Equal(before.Size, after.Size);

        // Only the moved button is saved; the others keep their defaults.
        var places = editor.Places();
        Assert.Equal([TouchPart.Fire], places.Keys);
        Assert.Equal(after.X, TouchLayout.Place(places[TouchPart.Fire], Phone, PhoneSafe, PhoneDensity).X, Tolerance);
    }

    [Fact]
    public void DraggingStopsAtTheSafeArea()
    {
        var editor = Editor();
        var grab = CentreOf(editor.Areas[TouchPart.Fire]);
        editor.Press(grab);
        editor.Drag(new Vector2(-1000f, -1000f));
        Assert.Equal(PhoneSafe.Left, editor.Areas[TouchPart.Fire].X);
        Assert.Equal(PhoneSafe.Top, editor.Areas[TouchPart.Fire].Y);
    }

    [Fact]
    public void TheHandleResizesTowardsTheMiddle()
    {
        // Fire is at the bottom right: its handle is its top left corner, its bottom right stays.
        var editor = Editor();
        var before = editor.Areas[TouchPart.Fire];
        editor.Press(CentreOf(before));
        editor.Release();
        var handle = editor.Handle!.Value;
        Assert.True(handle.Contains(before.Left, before.Top));

        editor.Press(new Vector2(before.Left, before.Top));
        editor.Drag(new Vector2(before.Left - 60f, before.Top - 40f));
        editor.Release();

        var after = editor.Areas[TouchPart.Fire];
        Assert.Equal(before.Width + 60f, after.Width, Tolerance);
        Assert.Equal(after.Width, after.Height);
        Assert.Equal(before.Right, after.Right, Tolerance);
        Assert.Equal(before.Bottom, after.Bottom, Tolerance);
    }

    [Fact]
    public void TheZonesHandleIsWithinReach()
    {
        // The zone fills the height: its corner is on the screen's edge, the handle keeps inside.
        var editor = Editor();
        var before = editor.Areas[TouchPart.Stick];
        editor.Press(CentreOf(before));
        editor.Release();
        var handle = editor.Handle!.Value;
        Assert.True(PhoneSafe.Contains(handle));

        editor.Press(CentreOf(handle));
        editor.Drag(CentreOf(handle) - new Vector2(100f, 100f));
        editor.Release();

        var after = editor.Areas[TouchPart.Stick];
        Assert.Equal(before.Width - 100f, after.Width, Tolerance);
        Assert.Equal(before.Height - 100f, after.Height, Tolerance);
        Assert.Equal(before.Top, after.Top, Tolerance);
    }

    [Fact]
    public void PlusGrowsALargeZoneInLargerSteps()
    {
        var editor = Editor();
        editor.Press(CentreOf(editor.Areas[TouchPart.Stick]));
        editor.Release();
        var before = editor.Areas[TouchPart.Stick].Width;

        editor.Grow(-1);
        Assert.True(before - editor.Areas[TouchPart.Stick].Width > 5f * PhoneDensity);
    }

    [Fact]
    public void PlusAndMinusResizeTheSelectedOne()
    {
        var editor = Editor();
        var before = editor.Areas[TouchPart.Jump];
        editor.Press(CentreOf(before));
        editor.Release();

        editor.Press(ToolAt(editor, TouchEditor.Tool.Grow));
        editor.Release();
        Assert.Equal(before.Width + PhoneDensity, editor.Areas[TouchPart.Jump].Width, Tolerance);
        Assert.Equal(TouchPart.Jump, editor.Selected);

        for (var i = 0; i < 20; i++)
        {
            editor.Grow(-1);
        }

        Assert.Equal(TouchLayout.MinMm * PhoneDensity, editor.Areas[TouchPart.Jump].Width, Tolerance);
    }

    [Fact]
    public void KeysPickTheControls()
    {
        var editor = Editor();
        editor.Select(1);
        Assert.Equal(TouchLayout.Parts[0], editor.Selected);
        editor.Select(-1);
        Assert.Equal(TouchLayout.Parts[^1], editor.Selected);
    }

    [Fact]
    public void ButtonsDroppedOnEachOtherAreFlagged()
    {
        var editor = Editor();
        var jump = editor.Areas[TouchPart.Jump];
        var fire = editor.Areas[TouchPart.Fire];
        editor.Press(CentreOf(fire));
        editor.Drag(CentreOf(fire) - new Vector2((fire.X - jump.X) / 2f, 0f));
        editor.Release();

        Assert.True(editor.Overlapping(TouchPart.Fire));
        Assert.True(editor.Overlapping(TouchPart.Jump));
        Assert.False(editor.Overlapping(TouchPart.Use));
    }

    [Fact]
    public void ResetRestoresTheDefaults()
    {
        var saved = new Dictionary<TouchPart, TouchPlace> { [TouchPart.Jump] = new(Across.Left, 0.3f, Along.Top, 0.3f, 15f, 15f) };
        var editor = Editor(saved);

        editor.Press(ToolAt(editor, TouchEditor.Tool.Reset));
        editor.Release();
        Assert.Empty(editor.Places());
        Assert.All(TouchLayout.Parts, p => Assert.Equal(TouchLayout.DefaultArea(p, Phone, PhoneSafe, PhoneDensity), editor.Areas[p]));
    }

    [Theory]
    [InlineData(TouchEditor.Tool.Done, TouchEditor.Outcome.Done)]
    [InlineData(TouchEditor.Tool.Cancel, TouchEditor.Outcome.Cancelled)]
    public void DoneAndCancelEndTheEditor(TouchEditor.Tool tool, TouchEditor.Outcome outcome)
    {
        var editor = Editor();
        Assert.Equal(outcome, editor.Press(ToolAt(editor, tool)));
    }

    [Fact]
    public void ATapOnNothingLetsGo()
    {
        var editor = Editor();
        editor.Press(CentreOf(editor.Areas[TouchPart.Fire]));
        editor.Release();

        editor.Press(new Vector2(1200f, 500f));
        Assert.Null(editor.Selected);
        Assert.Null(editor.Handle);
    }

    [Fact]
    public void TheToolsKeepInsideTheSafeArea()
    {
        var editor = Editor();
        Assert.All(editor.Tools, t => Assert.True(PhoneSafe.Contains(t.Area)));
        Assert.All(editor.Tools, t => Assert.False(t.Area.IntersectsWith(editor.Areas[TouchPart.Pause])));
    }

    [Fact]
    public void AnotherDisplayKeepsTheEdits()
    {
        // The phone unfolds while editing: the moved button keeps its place on the inner display.
        var editor = Editor();
        var fire = editor.Areas[TouchPart.Fire];
        editor.Press(CentreOf(fire));
        editor.Drag(CentreOf(fire) - new Vector2(400f, 0f));
        editor.Release();
        var place = editor.Places()[TouchPart.Fire];

        editor.Measure(Inner, RectangleF.Empty, InnerDensity);
        Assert.Equal(place.GapX, editor.Places()[TouchPart.Fire].GapX, Tolerance);
        Assert.Equal(TouchLayout.DefaultArea(TouchPart.Jump, Inner, RectangleF.Empty, InnerDensity), editor.Areas[TouchPart.Jump]);
    }
}
