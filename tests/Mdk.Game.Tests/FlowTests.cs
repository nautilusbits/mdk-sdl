using System.Text;
using Mdk.Engine.Platform;
using Mdk.Engine.Render;
using Mdk.Game.Flow;
using Mdk.Game.Kurt;
using Mdk.Game.Menu;
using Mdk.Game.Scripts;

namespace Mdk.Game.Tests;

/// <summary>The menus, saves and level flow (game_state.gd, settings.gd, menu_items.gd, stats_screen.gd).</summary>
public class FlowTests
{
    [Fact]
    public void LevelsPlayInOrder()
    {
        var level = GameState.Order[0];
        var played = new List<int> { level };
        while (LevelFlow.AfterLevel(level, ScriptRuntime.GameOver.No) != LevelFlow.After.Menu)
        {
            level = LevelFlow.Next(level);
            played.Add(level);
        }

        Assert.Equal([7, 6, 3, 4, 8, 5], played);
    }

    [Theory]
    [InlineData(7, LevelFlow.After.Statistics)]
    [InlineData(4, LevelFlow.After.Statistics)]
    [InlineData(8, LevelFlow.After.LastLevel)]
    [InlineData(5, LevelFlow.After.Menu)]
    public void AfterLevel(int level, LevelFlow.After after) =>
        Assert.Equal(after, LevelFlow.AfterLevel(level, ScriptRuntime.GameOver.No));

    [Fact]
    public void GameOverGoesToMenu() =>
        Assert.Equal(LevelFlow.After.Menu, LevelFlow.AfterLevel(7, ScriptRuntime.GameOver.Yes));

    [Fact]
    public void SaveNameIsLevelNumberInOrder()
    {
        Assert.Equal("1", LevelFlow.SaveName(7));
        Assert.Equal("6", LevelFlow.SaveName(5));
    }

    [Fact]
    public void SettingsRoundTrip()
    {
        var settings = new Settings
        {
            MasterVolume = 30,
            MusicVolume = 0,
            EffectsVolume = 100,
            MusicFilter = false,
            MouseSensitivity = 2.5f,
            InvertMouse = true,
            Fullscreen = Engine.Platform.Fullscreen.Desktop,
            Difficulty = Difficulty.Hard,
            Gore = false,
            Graphics = Graphics.Enhanced,
            AntiAliasing = AntiAliasing.X4,
            Stereo = Stereo.InterlacedReversed,
            StereoSeparation = 0.4f,
            StereoConvergence = 12f,
            HeadTracking = HeadTracking.On,
        };
        settings.Bindings[Key.Fire] = "Left mouse";
        settings.Bindings[Key.Jump] = "Right Ctrl";
        settings.PadBindings[Key.Fire] = "RB";
        settings.PadSensitivity = 1.5f;
        settings.InvertPad = true;
        settings.Touch = TouchButtons.Off;

        var read = Settings.Parse(settings.Format());

        Assert.Equal(settings.Format(), read.Format());
        Assert.Equal(Difficulty.Hard, read.Difficulty);
        Assert.Equal(Graphics.Enhanced, read.Graphics);
        Assert.Equal(AntiAliasing.X4, read.AntiAliasing);
        Assert.Equal(Stereo.InterlacedReversed, read.Stereo);
        Assert.Equal(0.4f, read.StereoSeparation);
        Assert.Equal(HeadTracking.On, read.HeadTracking);
        Assert.Equal("Right Ctrl", read.Bindings[Key.Jump]);
        Assert.Equal("RB", read.PadBindings[Key.Fire]);
        Assert.Equal(1.5f, read.PadSensitivity);
        Assert.True(read.InvertPad);
        Assert.Equal(TouchButtons.Off, read.Touch);
    }

    [Fact]
    public void BadSettingsKeepDefaults()
    {
        var read = Settings.Parse("master_volume=loud\nmusic_volume=500\nunknown=1\nbind.Nothing=W\ngraphics=7\nantialiasing=X16\ntextures=8K\n");

        Assert.Equal(new Settings().MasterVolume, read.MasterVolume);
        Assert.Empty(read.Mods);
        Assert.Equal(Graphics.Original, read.Graphics);
        Assert.Equal(AntiAliasing.Off, read.AntiAliasing);
        Assert.Equal(Settings.MaxVolume, read.MusicVolume);
        Assert.Empty(read.Bindings);
    }

    [Fact]
    public void OldSmoothModelsKeyIgnored()
    {
        // settings.cfg saved while "Smooth models" existed.
        var read = Settings.Parse("smooth_models=On\ntextures=Hd\n");

        Assert.Equal(Mods.ModState.On, read.StateOf(HdTextures.HdGenerator.ModFolder));
        Assert.DoesNotContain("smooth_models", read.Format());
    }

    [Fact]
    public void GunterStreamCarriesHealthOnly()
    {
        var state = new GameState { Level = 8 };

        state.EnterLastLevel(37);

        Assert.Equal(5, state.Level);
        Assert.Equal(37, state.Carry?.Health);
        Assert.Empty(state.Carry!.Pickups);
    }

    [Fact]
    public void SaveRoundTrip()
    {
        var save = new SaveGame(SaveKind.BeforeLevel, 4, 100, 2, true, "2026-10-06T10:00:00");

        Assert.Equal(save, SaveGame.Parse(save.ToJson()));
    }

    [Fact]
    public void ReadsGodotSaves()
    {
        const string godot = "{\"type\": 3, \"level\": 8.0, \"health\": 100, \"deaths\": 1, \"strike_used\": false, \"time\": \"2026-01-01T12:00:00\"}";

        Assert.Equal(new SaveGame(SaveKind.LevelStart, 8, 100, 1, false, "2026-01-01T12:00:00"), SaveGame.Parse(godot));
    }

    [Theory]
    [InlineData("{\"level\": 2}")]
    [InlineData("{\"type\": 3}")]
    [InlineData("not json")]
    public void InvalidSavesAreRejected(string json) => Assert.Null(SaveGame.Parse(json));

    [Fact]
    public void SavesAreListedAndDeleted()
    {
        var folder = Path.Combine(Path.GetTempPath(), $"mdk-saves-{Guid.NewGuid():N}");
        var saves = new SaveGames(folder);
        saves.Delete(SaveGames.LastGame);
        var state = new GameState { Level = 3 };
        try
        {
            Assert.True(saves.Write("B", state.Save(SaveKind.LevelStart)));
            Assert.True(saves.Write("A", state.Save(SaveKind.BeforeLevel)));

            Assert.Equal(["A", "B"], saves.List());
            Assert.Equal(3, saves.Read("A")?.Level);
            saves.Delete("A");
            Assert.Equal(["B"], saves.List());
        }
        finally
        {
            Directory.Delete(folder, true);
        }
    }

    [Theory]
    [InlineData(1, 36f, true)]
    [InlineData(11, 350f / 11f, true)]
    [InlineData(17, 350f / 17f, false)]
    public void MenuRows(int entries, float row, bool big)
    {
        Assert.Equal(row, MenuItems.RowFor(entries), 3);
        Assert.Equal(big, MenuItems.UsesBigFont(MenuItems.RowFor(entries)));
    }

    [Theory]
    [InlineData(6, 0, 1f, 36f)]
    [InlineData(6, 0, 1.4f, 36f * 1.4f)]
    [InlineData(8, 0, 1.4f, 350f / 8f)]
    [InlineData(6, 2, 1.4f, 350f / 8f)]
    [InlineData(17, 0, 1.4f, 350f / 17f)]
    public void TouchRowsGrowAsFarAsThePageFits(int entries, int firstRow, float enlargement, float row)
    {
        // A desktop's rows (1) are the original's; a full page's can't grow.
        Assert.Equal(row, MenuItems.RowFor(entries, firstRow, enlargement), 3);
    }

    [Theory]
    [InlineData(6.5f, 1.4f)]
    [InlineData(8f, 1.25f)]
    [InlineData(13.7f, 1f)]
    [InlineData(0f, 1f)]
    public void TouchRowsGrowToTenMillimetres(float rowMm, float enlargement)
    {
        // A phone's 108-pixel rows (6.5 mm) grow by the most; a tablet's are big enough.
        Assert.Equal(enlargement, MenuItems.Enlargement(rowMm), 3);
    }

    [Theory]
    [InlineData(22, 0, 1.4f, 350f / 8f, 8)]
    [InlineData(16, 0, 1f, 36f, 9)]
    [InlineData(9, 0, 1.4f, 0f, 0)]
    [InlineData(10, 0, 1.4f, 0f, 0)]
    public void LongTouchPagesScroll(int entries, int firstRow, float enlargement, float row, int shown)
    {
        // The keys' page (22 items) on a phone: 8 grown rows show, the rest scroll; a page that
        // fits with the big font (the options' 10) doesn't scroll.
        Assert.Equal(row, MenuItems.ScrollRow(entries, firstRow, enlargement), 3);
        if (shown > 0)
        {
            Assert.Equal(shown, MenuItems.ShownRows(row, firstRow));
        }
    }

    [Fact]
    public void QuitTitleStandsAtRow3() => Assert.Equal(113f, MenuItems.RowTop(MenuItems.RowFor(3), 3));

    [Theory]
    [InlineData(80, 1, 90)]
    [InlineData(100, 1, 0)]
    [InlineData(0, -1, 100)]
    public void VolumeSteps(int value, int step, int expected) => Assert.Equal(expected, MenuItems.VolumeStep(value, step));

    [Theory]
    [InlineData(1f, 1, 1.25f)]
    [InlineData(3f, 1, 0.25f)]
    [InlineData(1.1f, -1, 0.75f)]
    public void SensitivitySteps(float value, int step, float expected) => Assert.Equal(expected, MenuItems.SensitivityStep(value, step));

    [Theory]
    [InlineData(1f, 1, 1.1f)]
    [InlineData(2f, 1, 0.5f)]
    [InlineData(0.5f, -1, 2f)]
    [InlineData(1.05f, 1, 1.1f)]
    public void GammaSteps(float value, int step, float expected) => Assert.Equal(expected, MenuItems.GammaStep(value, step));

    [Fact]
    public void GammaRoundTripsAndStaysInRange()
    {
        Assert.Equal(1.3f, Settings.Parse(new Settings { Gamma = 1.3f }.Format()).Gamma);
        Assert.Equal(Renderer.MaxGamma, Settings.Parse("gamma=9").Gamma);
        Assert.Equal(1f, Settings.Parse("gamma=bright").Gamma);
    }

    [Fact]
    public void ScoreValues()
    {
        Assert.Equal("8/20", StatsScreen.ValueText(4, 8, 20));
        Assert.Equal("50%", StatsScreen.ValueText(1, 50, 100));
        Assert.Equal("120", StatsScreen.ValueText(0, 120, 120));
        Assert.Equal(33, GameStats.Percent(1, 3));
        Assert.Equal(0, GameStats.Percent(0, 0));
    }

    [Fact]
    public void EnemiesAreCounted()
    {
        var stats = new GameStats();
        stats.CountEnemy("xg", GameStats.Kill.Created);
        stats.CountEnemy("XG", GameStats.Kill.Killed);
        stats.CountEnemy("X7DOOR", GameStats.Kill.Created);

        Assert.Equal((1, 1), (stats.Enemies, stats.Kills));
    }

    [Fact]
    public void TypesetterLines()
    {
        var (lines, count) = Typesetter.Layout(Encoding.ASCII.GetBytes(@"!!!News!!!\20n\cOne two\cThree\x\iFree\d"), 32f);

        Assert.Equal(5, lines.Count);
        Assert.Equal(("!!!News!!!", 0f, 32f, false, 0), Fields(lines[0]));
        Assert.Equal(("", 0f, 88f, false, 10), Fields(lines[1]));
        Assert.Equal(("One two", 300f, 88f, true, 10), Fields(lines[2]));
        Assert.Equal(("Three", 300f, 88f, true, 17), Fields(lines[3]));
        Assert.Equal(("Free", 0f, 88f, false, 22), Fields(lines[4]));
        Assert.Equal(22, count);
    }

    private static (string, float, float, bool, int) Fields(Typesetter.Line line) =>
        (Encoding.ASCII.GetString(line.Text), line.X, line.Y, line.Centred, line.Start);

    [Fact]
    public void SplashRuns()
    {
        byte[] bytes = [3, 7, 254, 1, 2, 0];

        Assert.Equal([7, 7, 7, 1, 2, 0], IntroSplash.Unpack(bytes, 0, 6));
    }

    [Fact]
    public void SplashFadesPalettes()
    {
        Assert.Equal([100], IntroSplash.Blend([0], [200], 0.5f));
        Assert.Equal([200], IntroSplash.Blend([0], [200], 1f));
    }

    [Theory]
    [InlineData(209, 0f)]
    [InlineData(233, 1f)]
    [InlineData(260, 0f)]
    public void EndMovieFlash(int frame, float flash) => Assert.Equal(flash, EndMovie.Flash(frame));
}
