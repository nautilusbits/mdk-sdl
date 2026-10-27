using System.Drawing;
using System.Globalization;
using System.Numerics;

namespace Mdk.Engine.Platform;

/// <summary>An on-screen control the player can place: a button, or the stick's zone.</summary>
public enum TouchPart { Fire, Jump, Use, Sniper, Item, ZoomIn, ZoomOut, Pause, Stick }

/// <summary>The safe area's edge a placed control keeps its distance to.</summary>
public enum Across { Left, Right }

public enum Along { Top, Bottom }

/// <summary>A placed control: its gaps to the nearer edges of the safe area (parts of its width and
/// height) and its size in millimetres. Kept apart from the pixels, it fits another display (a
/// foldable's other screen, a tablet).
/// <code>
///   touch.Fire=Right,0.0113,Bottom,0.2,9.8,9.8
///   ┌ safe area ──────────────────┐
///   │                  gap x ─►┌──┤  gap x × width from the right edge
///   │                          └──┤
///   │                  gap y × height from the bottom
///   └─────────────────────────────┘
/// </code></summary>
public readonly record struct TouchPlace(Across X, float GapX, Along Y, float GapY, float WidthMm, float HeightMm)
{
    private const char Separator = ',';
    private const int Fields = 6;
    /// <summary>Sizes past this are a broken line, not a control.</summary>
    private const float MaxSizeMm = 1000f;

    public override string ToString() => string.Join(Separator, X, Number(GapX), Y, Number(GapY), Number(WidthMm), Number(HeightMm));

    private static string Number(float value) => value.ToString(CultureInfo.InvariantCulture);

    /// <summary>A place of its text (<see cref="ToString"/>); false for a bad one.</summary>
    public static bool TryParse(string text, out TouchPlace place)
    {
        place = default;
        var parts = text.Split(Separator);
        if (parts.Length != Fields
            || !Enum.TryParse<Across>(parts[0], out var x) || !Enum.IsDefined(x)
            || !Enum.TryParse<Along>(parts[2], out var y) || !Enum.IsDefined(y)
            || !Fraction(parts[1], out var gapX) || !Fraction(parts[3], out var gapY)
            || !Size(parts[4], out var width) || !Size(parts[5], out var height))
        {
            return false;
        }

        place = new TouchPlace(x, gapX, y, gapY, width, height);
        return true;
    }

    private static bool Fraction(string text, out float value) =>
        float.TryParse(text, CultureInfo.InvariantCulture, out value) && value is >= 0f and <= 1f;

    private static bool Size(string text, out float value) =>
        float.TryParse(text, CultureInfo.InvariantCulture, out value) && value is > 0f and <= MaxSizeMm;
}

/// <summary>Where the on-screen controls are (window pixels): the defaults, or the player's places
/// (<see cref="TouchPlace"/>) fitted to the display. The defaults:
/// <code>
///   ┌───────────────────────────────────────────────┐
///   │ stick zone (left 40 %)                  [ ||] │  pause
///   │                                  [ - ] [ + ] [ITEM]
///   │                                        [SNIPE] [USE ]
///   │                                        [JUMP ] [FIRE]
///   │                                                (radar)
///   └───────────────────────────────────────────────┘
/// Placed controls keep inside the safe area, 7 to 25 mm (the zone: the stick's reach to 60 % of its width).
/// </code></summary>
public static class TouchLayout
{
    /// <summary>Sizes in parts of the screen's height (the largest; all of it when the display's
    /// density is unknown).</summary>
    private const float ButtonSize = 0.15f;
    private const float Margin = 0.025f;
    /// <summary>Sizes in millimetres: a fingertip's button, the gap between buttons.</summary>
    private const float ButtonMm = 11f;
    private const float MarginMm = 3f;
    /// <summary>Room under the buttons for the HUD's radar (bottom right).</summary>
    private const float RadarSpace = 0.2f;
    /// <summary>The stick's zone: the left part of the screen's width.</summary>
    private const float StickSide = 0.4f;
    /// <summary>A placed button's size: at least, at most, and at most this part of the safe area's height.</summary>
    public const float MinMm = 7f;
    public const float MaxMm = 25f;
    private const float MaxPart = 0.5f;
    /// <summary>The stick's zone leaves room to look: at most this part of the safe area's width.</summary>
    private const float MaxZone = 0.6f;

    private readonly record struct Slot(TouchPart Part, Key Key, string Label, int Column, int Row);

    /// <summary>The buttons by column from the right edge and row from the bottom; pause stands at the
    /// top right, hit first (a button put over it can't hide it).</summary>
    private static readonly Slot[] Slots =
    [
        new(TouchPart.Pause, Key.Escape, "||", 0, 0),
        new(TouchPart.Fire, Key.Fire, "FIRE", 0, 0),
        new(TouchPart.Jump, Key.Jump, "JUMP", 1, 0),
        new(TouchPart.Use, Key.UseItem, "USE", 0, 1),
        new(TouchPart.Sniper, Key.Sniper, "SNIPE", 1, 1),
        new(TouchPart.Item, Key.ItemNext, "ITEM", 0, 2),
        new(TouchPart.ZoomIn, Key.ZoomIn, "+", 1, 2),
        new(TouchPart.ZoomOut, Key.ZoomOut, "-", 2, 2),
    ];

    /// <summary>The stick's zone has no key; the editor names it.</summary>
    private const string StickLabel = "MOVE";

    /// <summary>The buttons in the order they're hit (drawn the other way: the first on top), then the stick's zone.</summary>
    public static IReadOnlyList<TouchPart> Parts { get; } = [.. Slots.Select(s => s.Part), TouchPart.Stick];

    public static bool IsButton(TouchPart part) => part != TouchPart.Stick;

    public static Key KeyOf(TouchPart part) => SlotOf(part).Key;

    public static string LabelOf(TouchPart part) => part == TouchPart.Stick ? StickLabel : SlotOf(part).Label;

    private static Slot SlotOf(TouchPart part) => Slots.First(s => s.Part == part);

    /// <summary>The safe area, or all of the screen when it's empty (unknown).</summary>
    private static RectangleF SafeOf(Vector2 screen, RectangleF safe) =>
        safe.Width > 0f && safe.Height > 0f ? safe : new RectangleF(0f, 0f, screen.X, screen.Y);

    /// <summary>Window units per millimetre; on an unknown display, those making a default button
    /// 11 mm (its part of the height).</summary>
    public static float UnitsPerMm(float unitsPerMm, Vector2 screen) =>
        unitsPerMm > 0f ? unitsPerMm : ButtonSize * screen.Y / ButtonMm;

    /// <summary>A control's area, placed (and fitted) or the default.</summary>
    public static RectangleF AreaOf(TouchPart part, IReadOnlyDictionary<TouchPart, TouchPlace> places, Vector2 screen, RectangleF safe, float unitsPerMm) =>
        places.TryGetValue(part, out var place) ? Fit(part, Place(place, screen, safe, unitsPerMm), screen, safe, unitsPerMm) : DefaultArea(part, screen, safe, unitsPerMm);

    /// <summary>The default area: the buttons from the safe area's right edge, over the radar's room
    /// (its height's part); the stick's zone the screen's left part.</summary>
    public static RectangleF DefaultArea(TouchPart part, Vector2 screen, RectangleF safe, float unitsPerMm)
    {
        if (part == TouchPart.Stick)
        {
            return new RectangleF(0f, 0f, StickSide * screen.X, screen.Y);
        }

        var right = safe.Width > 0f ? MathF.Min(safe.Right, screen.X) : screen.X;
        var top = safe.Height > 0f ? MathF.Max(safe.Top, 0f) : 0f;
        var size = TouchControls.Physical(ButtonSize, ButtonMm, screen.Y, unitsPerMm);
        var margin = TouchControls.Physical(Margin, MarginMm, screen.Y, unitsPerMm);
        if (part == TouchPart.Pause)
        {
            return new RectangleF(right - margin - size, top + margin, size, size);
        }

        var slot = SlotOf(part);
        var step = size + margin;
        return new RectangleF(right - margin - size - slot.Column * step, screen.Y - RadarSpace * screen.Y - size - slot.Row * step, size, size);
    }

    /// <summary>A place's area on this display (not fitted).</summary>
    public static RectangleF Place(TouchPlace place, Vector2 screen, RectangleF safe, float unitsPerMm)
    {
        var area = SafeOf(screen, safe);
        var units = UnitsPerMm(unitsPerMm, screen);
        var width = place.WidthMm * units;
        var height = place.HeightMm * units;
        var x = place.X == Across.Left ? area.Left + place.GapX * area.Width : area.Right - place.GapX * area.Width - width;
        var y = place.Y == Along.Top ? area.Top + place.GapY * area.Height : area.Bottom - place.GapY * area.Height - height;
        return new RectangleF(x, y, width, height);
    }

    /// <summary>An area's place: its gaps to the safe area's nearer edges, its size in millimetres.</summary>
    public static TouchPlace PlaceOf(RectangleF area, Vector2 screen, RectangleF safe, float unitsPerMm)
    {
        var bounds = SafeOf(screen, safe);
        var units = UnitsPerMm(unitsPerMm, screen);
        var centre = new Vector2(area.X + area.Width / 2f, area.Y + area.Height / 2f);
        var x = centre.X < bounds.X + bounds.Width / 2f ? Across.Left : Across.Right;
        var y = centre.Y < bounds.Y + bounds.Height / 2f ? Along.Top : Along.Bottom;
        var gapX = x == Across.Left ? area.Left - bounds.Left : bounds.Right - area.Right;
        var gapY = y == Along.Top ? area.Top - bounds.Top : bounds.Bottom - area.Bottom;
        return new TouchPlace(x, Math.Clamp(gapX / bounds.Width, 0f, 1f), y, Math.Clamp(gapY / bounds.Height, 0f, 1f), area.Width / units, area.Height / units);
    }

    /// <summary>An area kept to its sizes (a button square, 7-25 mm and at most half the safe area's
    /// height; the zone at least the stick's reach) and inside the safe area.</summary>
    public static RectangleF Fit(TouchPart part, RectangleF area, Vector2 screen, RectangleF safe, float unitsPerMm)
    {
        var bounds = SafeOf(screen, safe);
        var (width, height) = part == TouchPart.Stick ? ZoneSize(area, screen, bounds, unitsPerMm) : ButtonSide(area, screen, bounds, unitsPerMm);
        var x = Math.Clamp(area.X, bounds.Left, MathF.Max(bounds.Right - width, bounds.Left));
        var y = Math.Clamp(area.Y, bounds.Top, MathF.Max(bounds.Bottom - height, bounds.Top));
        return new RectangleF(x, y, width, height);
    }

    private static (float, float) ButtonSide(RectangleF area, Vector2 screen, RectangleF bounds, float unitsPerMm)
    {
        var units = UnitsPerMm(unitsPerMm, screen);
        var max = MathF.Min(MaxMm * units, MaxPart * MathF.Min(bounds.Width, bounds.Height));
        var side = Math.Clamp(MathF.Max(area.Width, area.Height), MathF.Min(MinMm * units, max), max);
        return (side, side);
    }

    private static (float, float) ZoneSize(RectangleF area, Vector2 screen, RectangleF bounds, float unitsPerMm)
    {
        var reach = 2f * TouchControls.StickReach(screen.Y, unitsPerMm);
        var max = MaxZone * bounds.Width;
        var width = Math.Clamp(area.Width, MathF.Min(reach, max), max);
        var height = Math.Clamp(area.Height, MathF.Min(reach, bounds.Height), bounds.Height);
        return (width, height);
    }

    /// <summary>Whether a button covers part of another (the zone lies under them: buttons win).</summary>
    public static bool Overlaps(TouchPart part, IReadOnlyDictionary<TouchPart, RectangleF> areas)
    {
        if (!IsButton(part) || !areas.TryGetValue(part, out var area))
        {
            return false;
        }

        foreach (var (other, rect) in areas)
        {
            if (other != part && IsButton(other) && rect.IntersectsWith(area))
            {
                return true;
            }
        }

        return false;
    }
}
