using System.Drawing;
using System.Numerics;
using Mdk.Engine.Diagnostics;
using Mdk.Engine.Render;

namespace Mdk.Game.DevTools;

/// <summary>Whether the console shows a close button (a touch screen: no Escape key).</summary>
public enum CloseButton { Hidden, Shown }

/// <summary>Where a touch lands on the open console: its log, its typed line, its close button,
/// or the game below it.</summary>
public enum ConsoleSpot { Log, Line, Close, Below }

/// <summary>Draws the console: half the canvas high, slid down from the top by
/// <see cref="DevConsole.Slide"/>; the line typed at the bottom, the log above it, newest last.
/// <code>
///   ┌──────────────────────────────┐ ▲
///   │ Level 3: 12 arenas ...       │ │ half the canvas
///   │ ] pos                        │ │
///   │ ] god_          MDK SDL 1.2.1│ ▼ × slide
///   └──────────────────────────────┘
/// </code>
/// A touch screen's console has a close button at its top right (<see cref="Spot"/>).</summary>
public sealed class ConsoleView(DevTextView text)
{
    private const float Margin = 4f;
    private const float EdgeHeight = 1f;
    private const string Cursor = "_";
    private static readonly Vector4 Background = new(0.02f, 0.03f, 0.06f, 0.85f);
    private static readonly Vector4 Edge = new(0.5f, 0.6f, 0.8f, 1f);
    /// <summary>The close button's side (canvas units: about 5 mm on a phone) and its cross's
    /// squares (as many on each diagonal).</summary>
    private const float CloseSize = 32f;
    private const int CrossSquares = 9;
    private const float CrossInset = 8f;
    private static readonly Vector4 CloseFill = new(0.15f, 0.18f, 0.25f, 0.9f);
    /// <summary>The typed line's touch area: its row and the one above (canvas units).</summary>
    private const float LineArea = DevTextView.LineHeight * 2f + Margin;
    /// <summary>The version, at the bottom right as in Quake's console.</summary>
    private static readonly string Version = "MDK SDL " + BuildInfo.Current().Version;

    /// <summary>The console's bottom edge on the canvas (0 when closed).</summary>
    public static float Bottom(DevConsole console) => Height * console.Slide;

    private static float Height => Renderer.CanvasHeight / 2f;

    public CloseButton CloseButton { get; set; }

    /// <summary>The close button on the canvas for a console whose bottom edge is at
    /// <paramref name="bottom"/>: its top right, inside the safe area.</summary>
    public static RectangleF CloseArea(float bottom, RectangleF safe)
    {
        var top = MathF.Max(bottom - Height, safe.Top) + Margin;
        return new RectangleF(safe.Right - Margin - CloseSize, top, CloseSize, CloseSize);
    }

    /// <summary>What a touch at <paramref name="point"/> (canvas) hits on a console whose bottom
    /// edge is at <paramref name="bottom"/>.
    /// <code>
    ///   ┌──────────────────[X]┐  Close
    ///   │ log                 │  Log: hides the keyboard
    ///   │ ] line_             │  Line: shows it
    ///   └─────────────────────┘
    ///     game                   Below: closes the console
    /// </code></summary>
    public static ConsoleSpot Spot(Vector2 point, float bottom, RectangleF close)
    {
        if (point.Y > bottom)
        {
            return ConsoleSpot.Below;
        }

        if (close.Contains(point.X, point.Y))
        {
            return ConsoleSpot.Close;
        }

        return point.Y >= bottom - LineArea ? ConsoleSpot.Line : ConsoleSpot.Log;
    }

    /// <summary>A touch on this console (<see cref="Spot(Vector2, float, RectangleF)"/>).</summary>
    public ConsoleSpot Spot(DevConsole console, Vector2 point)
    {
        var bottom = Bottom(console);
        return Spot(point, bottom, CloseArea(bottom, text.Renderer.CanvasSafeArea));
    }

    public void Draw(DevConsole console, LogRing log)
    {
        if (console.Slide <= 0f)
        {
            return;
        }

        var width = text.Renderer.CanvasWidth;
        var height = Height;
        var bottom = Bottom(console);
        text.Fill(new RectangleF(0f, bottom - height, width, height), Background);
        text.Fill(new RectangleF(0f, bottom, width, EdgeHeight), Edge);

        // The line typed, then the log upwards from the newest line not scrolled away.
        var baseline = bottom - Margin;
        if (!ReferenceEquals(_typed, console.Line))
        {
            (_typed, _prompt) = (console.Line, DevConsole.Prompt + console.Line + Cursor);
        }

        text.Text(_prompt, Margin, baseline);
        DrawClose(bottom);
        text.Text(Version, width - Margin - text.Width(Version), baseline);
        var rows = (int)((height - Margin) / DevTextView.LineHeight) - 1;
        log.CopyLast(rows + console.Scroll, _lines);
        var newest = Math.Max(_lines.Count - console.Scroll, 0);
        for (var i = newest - 1; i >= Math.Max(newest - rows, 0); i--)
        {
            baseline -= DevTextView.LineHeight;
            text.Text(_lines[i], Margin, baseline);
        }
    }

    /// <summary>A touch screen's close button: a box with a cross of squares.</summary>
    private void DrawClose(float bottom)
    {
        if (CloseButton == CloseButton.Hidden)
        {
            return;
        }

        var area = CloseArea(bottom, text.Renderer.CanvasSafeArea);
        text.Fill(area, CloseFill);
        var span = CloseSize - CrossInset * 2f;
        var square = span / CrossSquares;
        for (var i = 0; i < CrossSquares; i++)
        {
            var x = area.X + CrossInset + square * i;
            text.Fill(new RectangleF(x, area.Y + CrossInset + square * i, square, square), Edge);
            text.Fill(new RectangleF(x, area.Y + CrossInset + span - square * (i + 1), square, square), Edge);
        }
    }

    /// <summary>The log's lines shown (kept), the line typed and its prompt (made when it changes).</summary>
    private readonly List<string> _lines = [];
    private string? _typed;
    private string _prompt = "";
}
