using System.Drawing;
using System.Numerics;
using Mdk.Engine.Platform;

namespace Mdk.Game.Menu;

/// <summary>Editing where the on-screen controls are (Options, Touch, "Edit touch layout"): a
/// control is dragged by its middle, resized by its handle (the corner facing the screen's middle;
/// the opposite corner stays) or by Smaller and Bigger; Reset brings the defaults back, Done keeps the edits,
/// Cancel drops them. Only the controls moved or resized are kept (<see cref="Places"/>): the
/// others follow the defaults on any display. Window pixels.
/// <code>
///      [RESET] [SMALLER] [BIGGER] [CANCEL] [DONE]
///   ┌ MOVE ──────┐                          [ ||]
///   │            │                    o──────┐
///   │            │                    │ FIRE │   o: the selected one's handle
///   └────────────┘                    └──────┘
/// </code></summary>
public sealed class TouchEditor
{
    public enum Tool { Reset, Shrink, Grow, Cancel, Done }

    public enum Outcome { Editing, Done, Cancelled }

    private enum Grab { None, Move, Resize }

    /// <summary>+ and - grow by a millimetre, or this part of the larger side (a zone: in fewer taps).</summary>
    private const float StepMm = 1f;
    private const float StepPart = 0.1f;
    /// <summary>The handle's side, the tools' height and their gaps (millimetres).</summary>
    private const float HandleMm = 7f;
    private const float ToolMm = 9f;
    private const float GapMm = 2f;
    /// <summary>The tools' widths in their height.</summary>
    private const float WideTool = 2f;
    /// <summary>The tools take at most this part of the safe area's width.</summary>
    private const float ToolsPart = 0.9f;

    private static readonly (Tool Tool, string Label, float Width)[] ToolSlots =
    [
        (Tool.Reset, "RESET", WideTool), (Tool.Shrink, "SMALLER", WideTool), (Tool.Grow, "BIGGER", WideTool),
        (Tool.Cancel, "CANCEL", WideTool), (Tool.Done, "DONE", WideTool),
    ];

    /// <summary>The controls moved or resized (by their places), and every control's area.</summary>
    private readonly Dictionary<TouchPart, TouchPlace> _places;
    private readonly Dictionary<TouchPart, RectangleF> _areas = [];
    private readonly List<(Tool Tool, RectangleF Area)> _tools = [];

    private Vector2 _screen;
    private RectangleF _safe;
    private float _unitsPerMm;
    private bool _measured;

    private Grab _grab;
    /// <summary>A move: the finger's offset from the area's corner; a resize: the corner that stays,
    /// and the finger's offset to the moving corner (the handle keeps inside the screen).</summary>
    private Vector2 _anchor;
    private Vector2 _offset;

    public TouchEditor(IReadOnlyDictionary<TouchPart, TouchPlace> saved) => _places = new Dictionary<TouchPart, TouchPlace>(saved);

    public static string LabelOf(Tool tool) => ToolSlots.First(t => t.Tool == tool).Label;

    public IReadOnlyDictionary<TouchPart, RectangleF> Areas => _areas;

    public IReadOnlyList<(Tool Tool, RectangleF Area)> Tools => _tools;

    public TouchPart? Selected { get; private set; }

    /// <summary>The selected control's handle, or null.</summary>
    public RectangleF? Handle => Selected is { } part ? HandleOf(_areas[part]) : null;

    public bool Overlapping(TouchPart part) => TouchLayout.Overlaps(part, _areas);

    /// <summary>The kept controls' places (see <see cref="TouchLayout"/>).</summary>
    public Dictionary<TouchPart, TouchPlace> Places() => new(_places);

    /// <summary>The display edited on; another one (a rotation, a fold) lays the controls out again.</summary>
    public void Measure(Vector2 screen, RectangleF safe, float unitsPerMm)
    {
        if (_measured && screen == _screen && safe == _safe && unitsPerMm == _unitsPerMm)
        {
            return;
        }

        (_screen, _safe, _unitsPerMm, _measured) = (screen, safe, unitsPerMm, true);
        _grab = Grab.None;
        foreach (var part in TouchLayout.Parts)
        {
            _areas[part] = TouchLayout.AreaOf(part, _places, screen, safe, unitsPerMm);
        }

        LayTools();
    }

    /// <summary>A finger lands (or a click): a tool, the selected one's handle, a control (the
    /// buttons over the zone), or nothing (none selected).</summary>
    public Outcome Press(Vector2 at)
    {
        foreach (var (tool, area) in _tools)
        {
            if (area.Contains(at.X, at.Y))
            {
                return Use(tool);
            }
        }

        if (Handle is { } handle && handle.Contains(at.X, at.Y))
        {
            var area = _areas[Selected!.Value];
            var (left, top) = HandleCorner(area);
            _anchor = new Vector2(left ? area.Right : area.Left, top ? area.Bottom : area.Top);
            _offset = new Vector2(left ? area.Left : area.Right, top ? area.Top : area.Bottom) - at;
            _grab = Grab.Resize;
            return Outcome.Editing;
        }

        Selected = null;
        _grab = Grab.None;
        foreach (var part in TouchLayout.Parts)
        {
            var area = _areas[part];
            if (area.Contains(at.X, at.Y))
            {
                Selected = part;
                _anchor = at - new Vector2(area.X, area.Y);
                _grab = Grab.Move;
                break;
            }
        }

        return Outcome.Editing;
    }

    /// <summary>The finger moves: the grabbed control follows.</summary>
    public void Drag(Vector2 at)
    {
        if (_grab == Grab.None || Selected is not { } part)
        {
            return;
        }

        var area = _areas[part];
        if (_grab == Grab.Move)
        {
            var corner = at - _anchor;
            Change(part, new RectangleF(corner.X, corner.Y, area.Width, area.Height));
            return;
        }

        Change(part, Resized(part, at + _offset));
    }

    public void Release() => _grab = Grab.None;

    /// <summary>The selected control grows (+1) or shrinks (-1) about its middle.</summary>
    public void Grow(int step)
    {
        if (Selected is not { } part)
        {
            return;
        }

        var area = _areas[part];
        var by = step * MathF.Max(StepMm * TouchLayout.UnitsPerMm(_unitsPerMm, _screen), StepPart * MathF.Max(area.Width, area.Height));
        area.Inflate(by / 2f, by / 2f);
        Change(part, area);
    }

    /// <summary>The next (+1) or previous (-1) control (keys, a gamepad).</summary>
    public void Select(int step)
    {
        var parts = TouchLayout.Parts;
        var index = Selected is { } part ? parts.ToList().IndexOf(part) + step : step > 0 ? 0 : parts.Count - 1;
        Selected = parts[(index % parts.Count + parts.Count) % parts.Count];
    }

    /// <summary>The defaults again (kept by Done).</summary>
    public void Reset()
    {
        _places.Clear();
        _measured = false;
        Measure(_screen, _safe, _unitsPerMm);
    }

    private Outcome Use(Tool tool)
    {
        switch (tool)
        {
            case Tool.Reset:
                Reset();
                break;
            case Tool.Shrink:
                Grow(-1);
                break;
            case Tool.Grow:
                Grow(1);
                break;
            case Tool.Cancel:
                return Outcome.Cancelled;
            case Tool.Done:
                return Outcome.Done;
        }

        return Outcome.Editing;
    }

    /// <summary>A control's new area, fitted (a button below the tools: under them it couldn't be
    /// picked again), and kept as a place. An empty window (mid-rotation) changes nothing.</summary>
    private void Change(TouchPart part, RectangleF area)
    {
        if (Bounds.Width <= 0f || Bounds.Height <= 0f)
        {
            return;
        }

        var fitted = TouchLayout.Fit(part, area, _screen, _safe, _unitsPerMm);
        var tools = ToolsArea();
        if (TouchLayout.IsButton(part) && fitted.IntersectsWith(tools))
        {
            fitted = TouchLayout.Fit(part, fitted with { Y = tools.Bottom + GapMm * TouchLayout.UnitsPerMm(_unitsPerMm, _screen) }, _screen, _safe, _unitsPerMm);
        }

        _areas[part] = fitted;
        _places[part] = TouchLayout.PlaceOf(fitted, _screen, _safe, _unitsPerMm);
    }

    /// <summary>The area from the staying corner to the finger: a button square (its larger side).</summary>
    private RectangleF Resized(TouchPart part, Vector2 at)
    {
        var width = MathF.Abs(at.X - _anchor.X);
        var height = MathF.Abs(at.Y - _anchor.Y);
        if (TouchLayout.IsButton(part))
        {
            width = height = MathF.Max(width, height);
        }

        // Fitted first, so the staying corner stays where the size is kept.
        var size = TouchLayout.Fit(part, new RectangleF(0f, 0f, width, height), _screen, _safe, _unitsPerMm).Size;
        var x = at.X < _anchor.X ? _anchor.X - size.Width : _anchor.X;
        var y = at.Y < _anchor.Y ? _anchor.Y - size.Height : _anchor.Y;
        return new RectangleF(new PointF(x, y), size);
    }

    /// <summary>The handle's corner: true for the left (top) one, when the area is right of (below) the
    /// safe area's middle.</summary>
    private (bool Left, bool Top) HandleCorner(RectangleF area)
    {
        var bounds = Bounds;
        return (area.X + area.Width / 2f > bounds.X + bounds.Width / 2f, area.Y + area.Height / 2f > bounds.Y + bounds.Height / 2f);
    }

    private RectangleF HandleOf(RectangleF area)
    {
        var side = HandleMm * TouchLayout.UnitsPerMm(_unitsPerMm, _screen);
        var (left, top) = HandleCorner(area);
        var corner = new Vector2(left ? area.Left : area.Right, top ? area.Top : area.Bottom);

        // Inside the safe area, half a handle off its edges (a zone's corner is on the screen's
        // edge, where a phone's gestures are).
        var bounds = Bounds;
        var x = Math.Clamp(corner.X - side / 2f, bounds.Left + side / 2f, MathF.Max(bounds.Right - 1.5f * side, bounds.Left));
        var y = Math.Clamp(corner.Y - side / 2f, bounds.Top + side / 2f, MathF.Max(bounds.Bottom - 1.5f * side, bounds.Top));
        return new RectangleF(x, y, side, side);
    }

    private RectangleF ToolsArea() => _tools.Count == 0 ? RectangleF.Empty : RectangleF.FromLTRB(_tools[0].Area.Left, _tools[0].Area.Top, _tools[^1].Area.Right, _tools[^1].Area.Bottom);

    private RectangleF Bounds => _safe.Width > 0f && _safe.Height > 0f ? _safe : new RectangleF(0f, 0f, _screen.X, _screen.Y);

    /// <summary>The tools in a row at the safe area's top middle, smaller when they don't fit.</summary>
    private void LayTools()
    {
        _tools.Clear();
        var bounds = Bounds;
        var units = TouchLayout.UnitsPerMm(_unitsPerMm, _screen);
        var widths = ToolSlots.Sum(t => t.Width) * ToolMm + (ToolSlots.Length - 1) * GapMm;
        var scale = MathF.Min(units, ToolsPart * bounds.Width / widths);
        var x = bounds.X + (bounds.Width - widths * scale) / 2f;
        var y = bounds.Y + GapMm * scale;
        foreach (var (tool, _, width) in ToolSlots)
        {
            var area = new RectangleF(x, y, width * ToolMm * scale, ToolMm * scale);
            _tools.Add((tool, area));
            x = area.Right + GapMm * scale;
        }
    }
}
