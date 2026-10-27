using System.Drawing;
using System.Numerics;
using Mdk.Engine.Diagnostics;
using Mdk.Engine.Platform;
using Mdk.Game.DevTools;
using SDL;

namespace Mdk.Game.Tests;

/// <summary>The console's parts: parsing, the registry, completion, history, the input line, its keys.</summary>
public class DevConsoleTests
{
    [Fact]
    public void TouchesOnTheConsoleFindItsParts()
    {
        // The open console (bottom at 180) on a phone's 800-wide canvas, a cut-out's 69 on the left.
        var close = ConsoleView.CloseArea(180f, RectangleF.FromLTRB(69f, 0f, 774f, 360f));

        Assert.Equal(new RectangleF(738f, 4f, 32f, 32f), close);
        Assert.Equal(ConsoleSpot.Close, ConsoleView.Spot(new Vector2(750f, 20f), 180f, close));
        Assert.Equal(ConsoleSpot.Log, ConsoleView.Spot(new Vector2(300f, 100f), 180f, close));
        Assert.Equal(ConsoleSpot.Line, ConsoleView.Spot(new Vector2(300f, 175f), 180f, close));
        Assert.Equal(ConsoleSpot.Below, ConsoleView.Spot(new Vector2(300f, 250f), 180f, close));
    }

    [Fact]
    public void LinesSplitIntoCommandsAndWords()
    {
        var commands = CommandText.Parse("  tp  ARENA_1 ; god;; health 50 ");
        Assert.Equal(3, commands.Count);
        Assert.Equal(["tp", "ARENA_1"], commands[0]);
        Assert.Equal(["god"], commands[1]);
        Assert.Equal(["health", "50"], commands[2]);
    }

    [Fact]
    public void RegistryRunsCommandsByNameOrAlias()
    {
        var registry = new CommandRegistry();
        var seen = new List<string>();
        registry.Add(new Command("teleport", "teleport <arena>", args => { seen.AddRange(args); return "done"; }, ["tp"]));

        Assert.Equal(["done"], registry.Run("TP ARENA_1"));
        Assert.Equal(["ARENA_1"], seen);
        Assert.Equal(["Unknown command: fly"], registry.Run("fly"));
    }

    [Fact]
    public void CompletionFindsCommandNames()
    {
        var registry = new CommandRegistry();
        foreach (var name in new[] { "god", "give", "health" })
        {
            registry.Add(new Command(name, name, _ => ""));
        }

        Assert.Equal("health ", registry.Complete("he").Line);
        var (line, matches) = registry.Complete("g");
        Assert.Equal("g", line);
        Assert.Equal(["give", "god"], matches);
        Assert.Equal("gi", registry.Complete("gi").Line[..2]);
        Assert.Equal("give ", registry.Complete("gi").Line);

        // Only the command's name completes.
        Assert.Equal("give al", registry.Complete("give al").Line);
    }

    [Fact]
    public void HistoryWalksBackAndForth()
    {
        var history = new CommandHistory();
        history.Add("god");
        history.Add("pos");
        history.Add("pos");

        Assert.Equal("pos", history.Previous());
        Assert.Equal("god", history.Previous());
        Assert.Equal("god", history.Previous());
        Assert.Equal("pos", history.Next());
        Assert.Equal("", history.Next());
    }

    [Fact]
    public void ConsoleEditsRunsAndRemembers()
    {
        var log = new LogRing(10);
        var console = NewConsole(log, out var ran);
        console.Toggle();
        Assert.Equal(ConsoleState.Open, console.State);

        console.Type("hea");
        console.Press(EditKey.Complete);
        Assert.Equal("health ", console.Line);
        console.Type("51");
        console.Press(EditKey.Erase);
        console.Type("0");
        console.Press(EditKey.Submit);

        Assert.Equal(["health 50"], ran);
        Assert.Equal("", console.Line);
        Assert.Contains("] health 50", log.Lines());

        console.Press(EditKey.Previous);
        Assert.Equal("health 50", console.Line);
    }

    [Fact]
    public void ConsoleClearsTheLogAndListsCommands()
    {
        var log = new LogRing(10);
        var console = NewConsole(log, out _);
        console.Run("help");
        Assert.Contains(log.Lines(), l => l.StartsWith("health", StringComparison.Ordinal));

        console.Run("clear");
        Assert.Empty(log.Lines());
    }

    [Fact]
    public void ConsoleKeysAreFixedByPlace()
    {
        var input = new Input();
        input.BeginFrame();

        // The key left of 1, whatever the layout types there (";" on a Slovak keyboard).
        input.SetKey(SDL_Scancode.SDL_SCANCODE_GRAVE, Input.State.Down, Repeat.No);
        input.SetKey(SDL_Scancode.SDL_SCANCODE_F3, Input.State.Down, Repeat.No);
        input.SetKey(SDL_Scancode.SDL_SCANCODE_BACKSPACE, Input.State.Down, Repeat.Yes);
        Assert.True(input.WasPressed(RawKey.Grave));
        Assert.True(input.WasPressed(RawKey.F3));
        Assert.True(input.WasPressed(RawKey.Backspace));
        Assert.False(input.WasPressed(RawKey.Tab));

        input.BeginFrame();
        Assert.False(input.WasPressed(RawKey.Grave));
    }

    /// <summary>A phone's Back button (no Escape key there) closes the open console.</summary>
    [Fact]
    public void BackClosesTheConsole()
    {
        var console = NewConsole(new LogRing(10), out _);
        console.Toggle();
        var input = new Input();
        input.BeginFrame();
        input.SetKey(SDL_Scancode.SDL_SCANCODE_AC_BACK, Input.State.Down, Repeat.No);

        console.Update(input, 0f);

        Assert.Equal(ConsoleState.Closed, console.State);
    }

    /// <summary>Any screen: while the console is open (and on the frame Grave opens it) the screen
    /// gets no keys, typing, clicks or mouse; keys held by tests stay. Closed, input passes.</summary>
    [Fact]
    public void OpenConsoleTakesTheScreensInput()
    {
        var log = new LogRing(10);
        var console = NewConsole(log, out _);
        var overlays = 0;
        var dev = new DevUi(console, () => overlays++);
        var input = new Input();

        input.BeginFrame();
        input.SetKey(SDL_Scancode.SDL_SCANCODE_W, Input.State.Down, Repeat.No);
        input.SetKey(SDL_Scancode.SDL_SCANCODE_F3, Input.State.Down, Repeat.No);
        Assert.Equal(ConsoleState.Closed, dev.Update(input, Step));
        Assert.True(input.WasPressed(Key.Forward));
        Assert.Equal(1, overlays);

        input.BeginFrame();
        input.SetKey(SDL_Scancode.SDL_SCANCODE_W, Input.State.Up, Repeat.No);
        input.SetKey(SDL_Scancode.SDL_SCANCODE_GRAVE, Input.State.Down, Repeat.No);
        input.AddText("`");
        Assert.Equal(ConsoleState.Open, dev.Update(input, Step));
        Assert.Equal("", input.Typed);

        input.BeginFrame();
        input.Hold(Key.Jump, Input.State.Down);
        input.SetKey(SDL_Scancode.SDL_SCANCODE_W, Input.State.Down, Repeat.No);
        input.SetButton(MouseButton.Left, Input.State.Down);
        input.AddMouseMotion(5f, 5f);
        input.AddText("w");
        Assert.Equal(ConsoleState.Open, dev.Update(input, Step));
        Assert.Equal("w", console.Line);
        Assert.False(input.WasPressed(Key.Forward));
        Assert.False(input.IsDown(Key.Forward));
        Assert.False(input.WasClicked(Pointer.Left));
        Assert.Equal(0f, input.MouseX);
        Assert.Equal("", input.Typed);
        Assert.True(input.IsDown(Key.Jump));
    }

    [Fact]
    public void KeyboardCorrectionsEraseInOrder()
    {
        // A phone's keyboard corrects "fps" to "gps": three Backspaces, then the new word, in one frame.
        var console = NewConsole(new LogRing(10), out _);
        console.Toggle();
        var input = new Input();
        input.BeginFrame();
        input.AddText("fps");
        console.Update(input, Step);

        input.BeginFrame();
        for (var i = 0; i < 3; i++)
        {
            input.SetKey(SDL_Scancode.SDL_SCANCODE_BACKSPACE, Input.State.Down, Repeat.No);
            input.SetKey(SDL_Scancode.SDL_SCANCODE_BACKSPACE, Input.State.Up, Repeat.No);
        }

        input.AddText("gps");
        console.Update(input, Step);
        Assert.Equal("gps", console.Line);
    }

    [Fact]
    public void ConsoleOptionRunsOnce()
    {
        var log = new LogRing(10);
        var console = NewConsole(log, out var ran);
        var dev = new DevUi(console, () => { });
        dev.RunOnce("health 5");
        dev.RunOnce("health 6");
        Assert.Equal(["health 5"], ran);
        Assert.Equal(ConsoleState.Open, console.State);
    }

    private const float Step = 1f / 60f;

    /// <summary>A console with a "health" command that records its lines; its output goes to <paramref name="log"/>.</summary>
    private static DevConsole NewConsole(LogRing log, out List<string> ran)
    {
        var lines = new List<string>();
        ran = lines;
        var registry = new CommandRegistry();
        registry.Add(new Command("health", "health <n>", args => { lines.Add("health " + string.Join(' ', args)); return ""; }));
        return new DevConsole(registry, new CommandHistory(), log, log.Add);
    }
}
