using System.Drawing;
using System.Numerics;
using Mdk.Engine.Platform;
using Mdk.Game.DevTools;

namespace Mdk.Game.Menu;

/// <summary>Editing where the on-screen controls are (Options, Touch, "Edit touch layout"): a
/// control is dragged by its middle, resized by its handle (the corner facing the screen's middle;
/// the opposite corner stays) or by Smaller and Bigger; Reset brings the defaults back, Done keeps the edits,
/// Cancel drops them. Only the controls moved or resized are kept (<see cref="Places"/>): the
/// others follow the defaults on any display. The tools' row moves to the bottom while a control
/// is dragged into the top half (and back), and keeps clear of the buttons after, so a control
/// can go anywhere. Cancel with changes asks first (Discard, Keep editing). Window pixels.
/// <code>
///      [RESET] [SMALLER] [BIGGER] [CANCEL] [DONE]
///   ┌ MOVE ──────┐                          [ ||]
///   │            │                    o──────┐
///   │            │                    │ FIRE │   o: the selected one's handle
///   └────────────┘                    └──────┘
/// </code></summary>
public sealed class TouchEditor
{
    public enum Tool { Reset, Shrink, Grow, Cancel, Done, Discard, Keep }

    public enum Outcome { Editing, Done, Cancelled }

    private enum Grab { None, Move, Resize }

    /// <summary>The safe area's edge the tools' row stands at (while a control is dragged).</summary>
    private enum Edge { Top, Bottom }

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

    /// <summary>The question's buttons, in the safe area's middle.</summary>
    private static readonly (Tool Tool, string Label, float Width)[] PromptSlots =
    [
        (Tool.Discard, "DISCARD", WideTool * 1.5f), (Tool.Keep, "KEEP EDITING", WideTool * 2f),
    ];

    /// <summary>The controls moved or resized (by their places), and every control's area.</summary>
    private readonly Dictionary<TouchPart, TouchPlace> _places;
    private readonly Dictionary<TouchPart, TouchPlace> _saved;
    private Edge _edge;
    /// <summary>The row's top and where it stands across after a drop moved it clear of the
    /// buttons, or null (at its edge, in the middle).</summary>
    private float? _rowTop;
    private Across _rowAcross = Across.Centre;

    /// <summary>Where the tools' row stands across the safe area.</summary>
    private enum Across { Centre, Left, Right }
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

    public TouchEditor(IReadOnlyDictionary<TouchPart, TouchPlace> saved)
    {
        _places = new Dictionary<TouchPart, TouchPlace>(saved);
        _saved = new Dictionary<TouchPart, TouchPlace>(saved);
    }

    public static string LabelOf(Tool tool) => ToolSlots.Concat(PromptSlots).First(t => t.Tool == tool).Label;

    /// <summary>The places differ from the saved ones (Cancel asks first).</summary>
    public bool Changed => _places.Count != _saved.Count || _places.Any(p => !_saved.TryGetValue(p.Key, out var place) || place != p.Value);

    /// <summary>"Discard changes?" is asked: only its buttons work (<see cref="Tools"/>).</summary>
    public bool Confirming { get; private set; }

    /// <summary>The help line's area: beside the tools' row, towards the middle.</summary>
    public RectangleF HelpArea { get; private set; }

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
        AvoidButtons();
    }

    /// <summary>A finger lands (or a click): a tool, the selected one's handle, a control (the
    /// buttons over the zone), or nothing (none selected). Asked, only the question's buttons.</summary>
    public Outcome Press(Vector2 at)
    {
        foreach (var (tool, area) in _tools)
        {
            if (area.Contains(at.X, at.Y))
            {
                return Use(tool);
            }
        }

        if (Confirming)
        {
            return Outcome.Editing;
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
        }
        else
        {
            Change(part, Resized(part, at + _offset));
        }

        // The row gives way: to the edge away from the dragged control.
        var bounds = Bounds;
        var moved = _areas[part];
        MoveTools(moved.Y + moved.Height / 2f < bounds.Y + bounds.Height / 2f ? Edge.Bottom : Edge.Top);
    }

    /// <summary>The finger lifts: the tools' row keeps clear of the buttons where it can.</summary>
    public void Release()
    {
        if (_grab != Grab.None)
        {
            AvoidButtons();
        }

        _grab = Grab.None;
    }

    private void MoveTools(Edge edge)
    {
        if (edge == _edge && _rowTop == null && _rowAcross == Across.Centre)
        {
            return;
        }

        _edge = edge;
        _rowTop = null;
        _rowAcross = Across.Centre;
        LayTools();
    }

    /// <summary>The row covers a button (it couldn't be picked again): the other edge, else the
    /// nearest height clear of the buttons, centred or to a side; none clear, it stays.
    /// <code>
    ///        [||]                        [||]
    ///               [- +]  ─►                   [- +]
    ///     [TOOLS....JUMP]           [TOOLS]     [JUMP]   (the bottom, to the left)
    /// </code></summary>
    private void AvoidButtons()
    {
        if (Confirming || !CoversButton())
        {
            return;
        }

        var edge = _edge;
        var top = ToolsArea().Top;
        MoveTools(edge == Edge.Top ? Edge.Bottom : Edge.Top);
        if (!CoversButton())
        {
            return;
        }

        // Every height a gap apart, the nearest to where the row was first.
        var bounds = Bounds;
        var height = ToolsArea().Height;
        var step = GapMm * TouchLayout.UnitsPerMm(_unitsPerMm, _screen);
        var tries = new List<float>();
        for (var y = bounds.Top + step; y + height <= bounds.Bottom - step && step > 0f; y += step)
        {
            tries.Add(y);
        }

        foreach (var y in tries.OrderBy(y => MathF.Abs(y - top)))
        {
            foreach (var across in Enum.GetValues<Across>())
            {
                (_rowTop, _rowAcross) = (y, across);
                LayTools();
                if (!CoversButton())
                {
                    return;
                }
            }
        }

        MoveTools(edge);
    }

    private bool CoversButton()
    {
        var tools = ToolsArea();
        return TouchLayout.Parts.Any(p => TouchLayout.IsButton(p) && _areas[p].IntersectsWith(tools));
    }

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

    /// <summary>Cancel (its tool, Back): ends at once without changes, else asks first; asked
    /// already, Back keeps editing.</summary>
    public Outcome Cancel()
    {
        if (Confirming)
        {
            Ask(Switch.Off);
            return Outcome.Editing;
        }

        if (!Changed)
        {
            return Outcome.Cancelled;
        }

        Ask(Switch.On);
        return Outcome.Editing;
    }

    /// <summary>Asked, Enter discards the changes; else it's Done.</summary>
    public Outcome Accept() => Confirming ? Outcome.Cancelled : Outcome.Done;

    private void Ask(Switch asking)
    {
        Confirming = asking == Switch.On;
        _grab = Grab.None;
        LayTools();
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
                return Cancel();
            case Tool.Done:
                return Outcome.Done;
            case Tool.Discard:
                return Outcome.Cancelled;
            case Tool.Keep:
                Ask(Switch.Off);
                break;
        }

        return Outcome.Editing;
    }

    /// <summary>A control's new area, fitted, and kept as a place. An empty window (mid-rotation)
    /// changes nothing.</summary>
    private void Change(TouchPart part, RectangleF area)
    {
        if (Bounds.Width <= 0f || Bounds.Height <= 0f)
        {
            return;
        }

        var fitted = TouchLayout.Fit(part, area, _screen, _safe, _unitsPerMm);
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

    /// <summary>The tools in a row at the safe area's top or bottom middle, smaller when they
    /// don't fit, the help line beside them; asked, the question's buttons in the middle.</summary>
    private void LayTools()
    {
        _tools.Clear();
        var bounds = Bounds;
        var units = TouchLayout.UnitsPerMm(_unitsPerMm, _screen);
        var slots = Confirming ? PromptSlots : ToolSlots;
        var widths = slots.Sum(t => t.Width) * ToolMm + (slots.Length - 1) * GapMm;
        var scale = MathF.Min(units, ToolsPart * bounds.Width / widths);
        var height = ToolMm * scale;
        var gap = GapMm * scale;
        var x = _rowAcross switch
        {
            _ when Confirming => bounds.X + (bounds.Width - widths * scale) / 2f,
            Across.Left => bounds.X + gap,
            Across.Right => bounds.Right - gap - widths * scale,
            _ => bounds.X + (bounds.Width - widths * scale) / 2f,
        };
        var y = Confirming ? bounds.Y + (bounds.Height - height) / 2f
            : _rowTop ?? (_edge == Edge.Top ? bounds.Y + gap : bounds.Bottom - gap - height);
        foreach (var (tool, _, width) in slots)
        {
            var area = new RectangleF(x, y, width * ToolMm * scale, height);
            _tools.Add((tool, area));
            x = area.Right + gap;
        }

        // The help line (the question's, asked): under (over) the row, as wide, towards the middle.
        var line = height * HelpPart;
        var helpY = y + height / 2f > bounds.Y + bounds.Height / 2f || Confirming ? y - gap - line : y + height + gap;
        HelpArea = new RectangleF(_tools[0].Area.X, helpY, _tools[^1].Area.Right - _tools[0].Area.X, line);
    }

    /// <summary>The help line's height, a part of a tool's.</summary>
    private const float HelpPart = 0.45f;
}
