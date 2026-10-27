using Mdk.Engine.Diagnostics;
using Mdk.Engine.Platform;

namespace Mdk.Game.DevTools;

public enum ConsoleState { Closed, Open }

/// <summary>What the console's keys do to its line.</summary>
public enum EditKey { Submit, Erase, Complete, Previous, Next, ScrollUp, ScrollDown, Close }

/// <summary>The Quake-style console: a line typed, run by the registry, its output and the log
/// above it. The key left of 1 opens and closes it, sliding down from the top.
/// <code>
///   typed text ──► line ──Enter──► "] line" printed ──► registry ──► output printed ──► log
///                   ▲ Tab: completion   ▲ Up/Down: history
/// </code></summary>
public sealed class DevConsole
{
    /// <summary>The prompt the echoed lines start with.</summary>
    public const string Prompt = "] ";
    /// <summary>Opening or closing takes 1 / this seconds.</summary>
    private const float SlideSpeed = 5f;
    /// <summary>PageUp and PageDown scroll this many lines.</summary>
    private const int ScrollLines = 5;
    private const char FirstPrintable = ' ';
    private const char LastFontCharacter = 'ÿ';
    private const char Unknown = '?';

    private static readonly Dictionary<RawKey, EditKey> Keys = new()
    {
        [RawKey.Enter] = EditKey.Submit,
        [RawKey.Tab] = EditKey.Complete,
        [RawKey.Up] = EditKey.Previous,
        [RawKey.Down] = EditKey.Next,
        [RawKey.PageUp] = EditKey.ScrollUp,
        [RawKey.PageDown] = EditKey.ScrollDown,
        [RawKey.Escape] = EditKey.Close,
    };

    private readonly CommandRegistry _registry;
    private readonly CommandHistory _history;
    private readonly LogRing _log;
    private readonly Action<string> _print;

    public DevConsole(CommandRegistry registry, CommandHistory history, LogRing log, Action<string> print)
    {
        _registry = registry;
        _history = history;
        _log = log;
        _print = print;
        registry.Add(new Command("help", "help", _ => Help()));
        registry.Add(new Command("clear", "clear", _ => Clear()));
    }

    public ConsoleState State { get; private set; }
    /// <summary>The line being typed.</summary>
    public string Line { get; private set; } = "";
    /// <summary>How far it's down: 0 closed, 1 open.</summary>
    public float Slide { get; private set; }
    /// <summary>Log lines scrolled back from the newest.</summary>
    public int Scroll { get; private set; }

    public void Toggle() => State = State == ConsoleState.Open ? ConsoleState.Closed : ConsoleState.Open;

    /// <summary>A frame's keys and text (only Grave while closed); returns whether the console had
    /// the keys this frame (the game doesn't get them).</summary>
    public ConsoleState Update(Input input, float elapsed)
    {
        var owner = State;
        if (input.WasPressed(RawKey.Grave))
        {
            Toggle();
        }
        else if (owner == ConsoleState.Open)
        {
            // The text of the frame's Grave (` or ; by the layout) isn't typed.
            Type(input.Typed);
            foreach (var (raw, key) in Keys)
            {
                if (input.WasPressed(raw))
                {
                    Press(key);
                }
            }

            // A phone's Back button: it has no Escape key.
            if (State == ConsoleState.Open && input.WasPressed(MenuKey.Back))
            {
                Press(EditKey.Close);
            }
        }

        var goal = State == ConsoleState.Open ? 1f : 0f;
        Slide = goal > Slide ? MathF.Min(Slide + SlideSpeed * elapsed, goal) : MathF.Max(Slide - SlideSpeed * elapsed, goal);
        return owner;
    }

    /// <summary>Adds typed text in order: <see cref="Input.Erase"/> erases, other control characters
    /// are dropped, those the font hasn't are '?'.</summary>
    public void Type(string text)
    {
        foreach (var c in text)
        {
            if (c == Input.Erase)
            {
                Press(EditKey.Erase);
                continue;
            }

            if (c < FirstPrintable)
            {
                continue;
            }

            Line += c <= LastFontCharacter ? c : Unknown;
        }
    }

    public void Press(EditKey key)
    {
        switch (key)
        {
            case EditKey.Submit:
                Submit();
                break;
            case EditKey.Erase:
                Line = Line.Length == 0 ? Line : Line[..^1];
                break;
            case EditKey.Complete:
                Complete();
                break;
            case EditKey.Previous:
                Line = _history.Previous();
                break;
            case EditKey.Next:
                Line = _history.Next();
                break;
            case EditKey.ScrollUp:
                Scroll += ScrollLines;
                break;
            case EditKey.ScrollDown:
                Scroll = Math.Max(Scroll - ScrollLines, 0);
                break;
            case EditKey.Close:
                State = ConsoleState.Closed;
                break;
        }
    }

    /// <summary>Runs a line as typed: echoed, kept in the history, its output printed.</summary>
    public void Run(string line)
    {
        _print(Prompt + line);
        _history.Add(line);
        foreach (var output in _registry.Run(line))
        {
            _print(output);
        }
    }

    private void Submit()
    {
        var line = Line.Trim();
        Line = "";
        Scroll = 0;
        if (line.Length != 0)
        {
            Run(line);
        }
    }

    private void Complete()
    {
        var (line, matches) = _registry.Complete(Line);
        Line = line;
        if (matches.Count > 1)
        {
            _print(string.Join("  ", matches));
        }
    }

    private string Help() => string.Join('\n', _registry.Commands.Select(c =>
        c.Aliases is { Count: > 0 } aliases ? $"{c.Usage}  (also {string.Join(", ", aliases)})" : c.Usage));

    private string Clear()
    {
        _log.Clear();
        Scroll = 0;
        return "";
    }
}
