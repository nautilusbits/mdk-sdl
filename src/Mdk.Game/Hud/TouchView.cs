using System.Drawing;
using System.Numerics;
using System.Text;
using Mdk.Engine.Platform;
using Mdk.Engine.Render;

namespace Mdk.Game.Hud;

/// <summary>Draws the on-screen controls of a touch screen (<see cref="TouchControls"/>) in play:
/// rounded buttons (a see-through fill, a thin outline, the label; brighter while held), the
/// stick's ring and knob where a finger walks.
/// <code>
///   a shape: 9 parts of a rounded square's mask (corners kept, edges and centre stretched)
///   ┌──┬────┬──┐
///   │ ╭┼────┼╮ │   r = corner radius on the canvas
///   ├──┼────┼──┤
///   │ ╰┼────┼╯ │
///   └──┴────┴──┘
/// </code></summary>
public sealed class TouchView
{
    /// <summary>The masks' corner radius and outline width (texels); a mask is 2 × radius + 1 wide.</summary>
    private const int MaskRadius = 32;
    private const float MaskLine = 4f;
    private const int MaskSide = 2 * MaskRadius + 1;
    private const int PaletteEntries = 256;
    private const int BytesPerColour = 4;
    private const byte Inside = 1;
    /// <summary>A button's corner radius, a part of its side.</summary>
    private const float Rounding = 0.25f;
    /// <summary>The knob's radius, a part of the stick's.</summary>
    private const float KnobPart = 0.4f;
    /// <summary>A label's height and widest width, parts of its button.</summary>
    private const float LabelHeight = 0.3f;
    private const float LabelWidth = 0.7f;
    private static readonly Vector4 Fill = new(1f, 1f, 1f, 0.18f);
    private static readonly Vector4 HeldFill = new(1f, 1f, 1f, 0.45f);
    private static readonly Vector4 Outline = new(1f, 1f, 1f, 0.55f);
    private static readonly Vector4 StickFill = new(1f, 1f, 1f, 0.1f);
    private static readonly Vector4 KnobFill = new(1f, 1f, 1f, 0.4f);
    /// <summary>The editor's selected control, and buttons covering each other.</summary>
    private static readonly Vector4 SelectedFill = new(1f, 0.8f, 0.2f, 0.45f);
    private static readonly Vector4 OverlapFill = new(1f, 0.2f, 0.15f, 0.55f);
    private const float LabelAlpha = 0.85f;
    private static readonly Vector2 MaskSize = new(MaskSide, MaskSide);

    private readonly Renderer _renderer;
    private readonly FontView _font;
    /// <summary>The rounded square's inside and its outline (index 1 through a white palette).</summary>
    private readonly int _fillMask;
    private readonly int _lineMask;
    private readonly int _palette;

    /// <summary>The labels' bytes, made once (frames don't allocate).</summary>
    private readonly Dictionary<string, byte[]> _labels = [];

    /// <summary>How a control is drawn: as it is, held (pressed), selected or overlapping (the editor).</summary>
    public enum Shade { Idle, Held, Selected, Overlap }

    /// <summary>The controls' opacity (1: as drawn by default); play takes the controls'.</summary>
    public float Opacity { get; set; } = 1f;

    public TouchView(Renderer renderer, FontView font)
    {
        _renderer = renderer;
        _font = font;
        _fillMask = renderer.CreateIndexTexture(MaskSide, MaskSide, MaskOf(Cover.Fill));
        _lineMask = renderer.CreateIndexTexture(MaskSide, MaskSide, MaskOf(Cover.Line));
        var white = new byte[PaletteEntries * BytesPerColour];
        Array.Fill(white, byte.MaxValue);
        _palette = renderer.CreatePalette(white);
    }

    /// <summary>What a mask covers.</summary>
    private enum Cover { Fill, Line }

    /// <summary>A rounded square's mask: the corners are quarter circles of the mask's radius about
    /// (radius, radius); the outline is the inside's last <see cref="MaskLine"/> texels.</summary>
    private static byte[] MaskOf(Cover cover)
    {
        var indices = new byte[MaskSide * MaskSide];
        for (var y = 0; y < MaskSide; y++)
        {
            for (var x = 0; x < MaskSide; x++)
            {
                // The distance in from the edge: along the straight sides, or the corner's arc.
                var dx = MathF.Max(MaskRadius - (x + 0.5f), x + 0.5f - (MaskSide - MaskRadius));
                var dy = MathF.Max(MaskRadius - (y + 0.5f), y + 0.5f - (MaskSide - MaskRadius));
                var outside = new Vector2(MathF.Max(dx, 0f), MathF.Max(dy, 0f)).Length();
                var depth = MaskRadius - outside;
                var covered = cover == Cover.Fill ? depth > 0f : depth > 0f && depth <= MaskLine;
                indices[y * MaskSide + x] = covered ? Inside : (byte)0;
            }
        }

        return indices;
    }

    public void Draw(Window window)
    {
        // In a VR viewer no finger reaches the screen: a gamepad plays.
        if (window.Touch is not { } touch || _renderer.StereoMode == Stereo.Vr)
        {
            return;
        }

        var (width, height) = window.Size;
        var screen = new Vector2(width, height);
        Opacity = touch.Opacity;
        // The first hit is drawn on top.
        var layout = touch.Layout(screen);
        for (var i = layout.Count - 1; i >= 0; i--)
        {
            var button = layout[i];
            Button(button.Area, button.Label, touch.Held(button.Key) ? Shade.Held : Shade.Idle);
        }

        if (touch.Stick is not { } stick || touch.StickAt is not { } at)
        {
            return;
        }

        // The stick's reach around where the finger landed, and the finger's knob.
        var reach = Canvas(Circle(stick, touch.StickRadius(screen)));
        Shape(_fillMask, reach, reach.Width / 2f, StickFill);
        Shape(_lineMask, reach, reach.Width / 2f, Outline);
        var knob = Canvas(Circle(at, KnobPart * touch.StickRadius(screen)));
        Shape(_fillMask, knob, knob.Width / 2f, KnobFill);
    }

    /// <summary>A rounded button (window pixels): its fill by <paramref name="shade"/>, an outline,
    /// its label. Its corners and label are as a square button's of <paramref name="side"/> (window
    /// pixels; null: its shorter side), so a large one (the stick's zone) isn't drawn heavier.</summary>
    public void Button(RectangleF windowArea, string label, Shade shade, float? side = null)
    {
        var area = Canvas(windowArea);
        var square = Canvas(new RectangleF(windowArea.Location, new SizeF(side ?? windowArea.Width, side ?? windowArea.Height)));
        var radius = Rounding * MathF.Min(MathF.Min(area.Width, area.Height), MathF.Min(square.Width, square.Height));
        var fill = shade switch
        {
            Shade.Held => HeldFill,
            Shade.Selected => SelectedFill,
            Shade.Overlap => OverlapFill,
            _ => Fill,
        };

        Shape(_fillMask, area, radius, fill);
        Shape(_lineMask, area, radius, Outline);
        Label(Bytes(label), area, MathF.Min(area.Height, square.Height));
    }

    /// <summary>A filled disc in <paramref name="windowArea"/> (the editor's handle).</summary>
    public void Disc(RectangleF windowArea)
    {
        var area = Canvas(windowArea);
        Shape(_fillMask, area, MathF.Min(area.Width, area.Height) / 2f, HeldFill);
        Shape(_lineMask, area, MathF.Min(area.Width, area.Height) / 2f, Outline);
    }

    private static RectangleF Circle(Vector2 centre, float radius) =>
        new(centre.X - radius, centre.Y - radius, 2f * radius, 2f * radius);

    /// <summary>A mask over <paramref name="area"/> with corners of <paramref name="radius"/>: its
    /// corners as they are, its middle row and column stretched.</summary>
    private void Shape(int mask, RectangleF area, float radius, Vector4 colour)
    {
        // Source and target edges, left to right (top to bottom alike).
        ReadOnlySpan<float> source = [0f, MaskRadius, MaskSide - MaskRadius, MaskSide];
        Span<float> xs = [area.Left, area.Left + radius, area.Right - radius, area.Right];
        Span<float> ys = [area.Top, area.Top + radius, area.Bottom - radius, area.Bottom];
        for (var row = 0; row < 3; row++)
        {
            for (var column = 0; column < 3; column++)
            {
                var from = RectangleF.FromLTRB(source[column], source[row], source[column + 1], source[row + 1]);
                var to = RectangleF.FromLTRB(xs[column], ys[row], xs[column + 1], ys[row + 1]);
                if (to.Width > 0f && to.Height > 0f)
                {
                    _renderer.DrawImage(mask, _palette, MaskSize, from, to, colour with { W = colour.W * Opacity });
                }
            }
        }
    }

    /// <summary>A label centred on its button, scaled to its size.</summary>
    private void Label(byte[] text, RectangleF area, float height)
    {
        var scale = LabelHeight * height / Math.Max(_font.Ascent, 1);
        scale = MathF.Min(scale, LabelWidth * area.Width / Math.Max(_font.Width(text), 1));
        var x = area.X + (area.Width - _font.Width(text) * scale) / 2f;
        var baseline = area.Y + (area.Height + _font.Ascent * scale) / 2f;
        _font.Draw(text, x, baseline, scale, LabelAlpha * Opacity);
    }

    /// <summary>A window rectangle on the canvas.</summary>
    private RectangleF Canvas(RectangleF area)
    {
        var from = _renderer.CanvasPoint(area.Left, area.Top);
        var to = _renderer.CanvasPoint(area.Right, area.Bottom);
        return RectangleF.FromLTRB(from.X, from.Y, to.X, to.Y);
    }

    private byte[] Bytes(string label)
    {
        if (!_labels.TryGetValue(label, out var bytes))
        {
            bytes = Encoding.ASCII.GetBytes(label);
            _labels[label] = bytes;
        }

        return bytes;
    }
}
