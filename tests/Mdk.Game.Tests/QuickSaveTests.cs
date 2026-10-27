using Mdk.Engine.Platform;
using Mdk.Game.Flow;
using Mdk.Game.Menu;
using SDL;

namespace Mdk.Game.Tests;

/// <summary>Quick save (F2, the original's full save) and quick load (F9): bindings, settings,
/// the pause menu's hint and the save quick load picks.</summary>
public class QuickSaveTests
{
    private static Input NewInput()
    {
        var input = new Input();
        input.BeginFrame();
        return input;
    }

    [Fact]
    public void QuickKeysDefaultToF2AndF9()
    {
        var input = NewInput();

        input.SetKey(SDL_Scancode.SDL_SCANCODE_F2, Input.State.Down, Repeat.No);
        input.SetKey(SDL_Scancode.SDL_SCANCODE_F9, Input.State.Down, Repeat.No);

        Assert.True(input.WasPressed(Key.QuickSave));
        Assert.True(input.WasPressed(Key.QuickLoad));
        Assert.Equal("F2", input.Describe(Key.QuickSave));
        Assert.Equal("F9", input.Describe(Key.QuickLoad));
    }

    [Fact]
    public void OldSettingsGetQuickDefaults()
    {
        var input = NewInput();
        var read = Settings.Parse("master_volume=80\nbind.Jump=Right Ctrl\n");

        read.Bind(input);

        Assert.False(read.Bindings.ContainsKey(Key.QuickSave));
        Assert.Equal("F2", input.Describe(Key.QuickSave));
        Assert.Equal("F9", input.Describe(Key.QuickLoad));
    }

    [Fact]
    public void QuickBindingsRoundTrip()
    {
        var input = NewInput();
        var settings = new Settings();
        settings.Bindings[Key.QuickSave] = "F5";
        settings.Bindings[Key.QuickLoad] = "F6";

        var read = Settings.Parse(settings.Format());
        read.Bind(input);

        Assert.Contains("bind.QuickLoad=F6", settings.Format());
        Assert.Equal("F5", input.Describe(Key.QuickSave));
        Assert.Equal("F6", input.Describe(Key.QuickLoad));
    }

    [Fact]
    public void ControlsListQuickActions()
    {
        Assert.Contains((Key.QuickSave, "Quick save"), Settings.Actions);
        Assert.Contains((Key.QuickLoad, "Quick load"), Settings.Actions);
    }

    [Fact]
    public void PauseHintNamesBoundKeys()
    {
        var input = NewInput();
        Assert.Equal("Quick save: F2   Quick load: F9", PauseMenu.KeysHint(input));

        input.Bind(Key.QuickLoad, "F5");

        Assert.Equal("Quick save: F2   Quick load: F5", PauseMenu.KeysHint(input));
    }

    [Fact]
    public void QuickLoadPicksSessionSaveElseNewest()
    {
        var folder = Path.Combine(Path.GetTempPath(), "mdk_quick_" + Guid.NewGuid().ToString("N"));
        var saves = new SaveGames(folder);
        var save = new SaveGame(SaveKind.Snapshot, 7, SaveGame.FullHealth, 0, false, "");
        try
        {
            Assert.Null(saves.QuickSlot(null));

            saves.Write("OLD", save);
            saves.Write("NEW", save);
            File.SetLastWriteTimeUtc(Path.Combine(folder, "OLD.sav"), DateTime.UtcNow.AddHours(-1));

            Assert.Equal("NEW", saves.QuickSlot(null));
            Assert.Equal("OLD", saves.QuickSlot("OLD"));
            Assert.Equal("NEW", saves.QuickSlot("GONE"));
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    [Fact]
    public void TestPressesFireOnceAtTheirTime()
    {
        var input = NewInput();
        var presses = TestPresses.Parse("QuickSave@1,Menu.Accept@1.5");

        presses.Apply(input, 0.5f);
        Assert.False(input.WasPressed(Key.QuickSave));

        presses.Apply(input, 1f);
        Assert.True(input.WasPressed(Key.QuickSave));
        Assert.False(input.WasPressed(MenuKey.Accept));

        input.BeginFrame();
        presses.Apply(input, 2f);
        Assert.False(input.WasPressed(Key.QuickSave));
        Assert.True(input.WasPressed(MenuKey.Accept));
    }

    [Fact]
    public void NamePromptAsksForKeyboard()
    {
        var snapshot = new SaveGame(SaveKind.Snapshot, 3, SaveGame.FullHealth, 0, false, "");
        var afterLevel = snapshot with { Kind = SaveKind.BeforeLevel };

        // A touch screen's keyboard: while the name is typed, not for Yes or No.
        var prompt = new SavePrompt(null!, null!, null!, snapshot, "4");
        Assert.Equal(Typing.On, prompt.Typing);
        Assert.Equal(Typing.Off, new SavePrompt(null!, null!, null!, afterLevel, "4").Typing);

        var input = NewInput();
        input.Press(MenuKey.Back);
        prompt.Update(input, 0f);
        Assert.True(prompt.Closed);
        Assert.Equal(Typing.Off, prompt.Typing);
    }
}
