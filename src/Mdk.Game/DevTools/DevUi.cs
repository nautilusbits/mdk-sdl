using System.Diagnostics;
using Mdk.Engine.Platform;
using Mdk.Engine.Render;
using Mdk.Game.Hud;

namespace Mdk.Game.DevTools;

/// <summary>The developer tools of every screen: the console (Grave) and the debug overlay (F3).
/// The game updates them before each screen's frame; while the console is open the screen runs
/// on without input.
/// <code>
///   Input ──► Grave, F3 ──► console / overlay
///         └─► console open? ──► console's line, the screen gets nothing : the screen's keys
/// </code></summary>
public sealed class DevUi(DevConsole console, Action toggleOverlay)
{
    private bool _optionRan;

    public DevConsole Console => console;

    /// <summary>The frame's keys: returns <see cref="ConsoleState.Open"/> when the console had them
    /// (also on the frame Grave opens it, so its character isn't typed elsewhere).</summary>
    public ConsoleState Update(Input input, float elapsed)
    {
        if (input.WasPressed(RawKey.F3))
        {
            toggleOverlay();
        }

        var grave = input.WasPressed(RawKey.Grave);
        if (console.Update(input, elapsed) == ConsoleState.Closed && !grave)
        {
            return ConsoleState.Closed;
        }

        input.Withhold();
        return ConsoleState.Open;
    }

    /// <summary>The tests' --console: the console opens and runs these lines, once.</summary>
    public void RunOnce(string? lines)
    {
        if (lines == null || _optionRan)
        {
            return;
        }

        _optionRan = true;
        console.Toggle();
        console.Run(lines);
    }
}

/// <summary>Draws the developer tools on the canvas over any screen: the overlay (the frame's
/// timings, then the screen's lines) and the console.</summary>
public sealed class DevUiView
{
    /// <summary>The overlay's text is made again this often (seconds): its numbers are averages, and
    /// making it every frame would make garbage every frame.</summary>
    private const double Refresh = 0.25;

    private readonly Renderer _renderer;
    private readonly OverlayView _overlay;
    private readonly ConsoleView _console;
    private IReadOnlyList<string> _lines = [];
    private long _made;

    public DevUiView(Renderer renderer, FontView font)
    {
        _renderer = renderer;
        var text = new DevTextView(renderer, font);
        _overlay = new OverlayView(text);
        _console = new ConsoleView(text);
    }

    public ConsoleView Console => _console;

    /// <summary>The GPU's and the display's lines, set when they change.</summary>
    public IReadOnlyList<string> System { get; set; } = [];

    /// <summary>The overlay asks the screen's lines only when it's shown.</summary>
    public void Draw(DevUi dev, DevSession session, Func<IReadOnlyList<string>> screen)
    {
        if (session.Overlay == Switch.On)
        {
            if (_lines.Count == 0 || Stopwatch.GetElapsedTime(_made).TotalSeconds >= Refresh)
            {
                var sample = new OverlaySample(session.Profiler, _renderer.Stats, MemorySample.Now(), screen()) { System = System };
                _lines = OverlayText.Lines(sample);
                _made = Stopwatch.GetTimestamp();
            }

            _overlay.Draw(_lines, ConsoleView.Bottom(dev.Console));
        }

        _console.Draw(dev.Console, session.Log);
    }
}
