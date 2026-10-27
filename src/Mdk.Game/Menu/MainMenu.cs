using System.Numerics;
using Mdk.Engine.Audio;
using Mdk.Engine.Platform;
using Mdk.Formats;
using Mdk.Game.Flow;

namespace Mdk.Game.Menu;

/// <summary>How the main menu opens (command line): its first page.</summary>
public enum MenuPage { Main, Options, Controls, Gamepad, BetaLevels, TouchLayout }

/// <summary>The main menu (main_menu.gd): <c>MDK12.FLC</c> plays behind it each time it opens
/// (0x4260e4), its last frame stays, then the slideshow; <c>MDKOPT</c> is the background when the
/// video is missing. <c>MAINSONG</c> plays, but not on the options page (<c>OPTSONG</c>). The
/// <c>INTRO1A</c> splash comes first when <see cref="GameState.Splash"/>.
/// <code>
///   Continue      (the level Kurt last died in: LASTGAME)
///   New Game
///   Level: n      (where a new game starts, 1-6)
///   Saved Game    (the list of saves)
///   Beta Levels   (the 1996 demo's, when it's found)
///   Options
///   Quit          (Esc: "Really Quit?")
/// </code></summary>
public sealed class MainMenu : IScreen
{
    private const string MenuVideo = "MISC/FLIC/MDK12.FLC";
    private const string Archive = "MISC/OPTIONS.BNI";
    private const int PaletteSize = 768;
    /// <summary>"Really Quit?" (0x403cf8): its title in row 3, Yes and No below; Y, J, O, S or T say
    /// yes in the game's languages, N no.</summary>
    private const int QuitRow = 3;
    private const string YesKeys = "YJOST";
    private const string NoKeys = "N";
    /// <summary><c>SEETHEWHOLEGAME</c>'s keys: LEVEL3-8 at once, D the statistics with random counts
    /// below 100.</summary>
    private const char DebugFirstLevel = '3';
    private const char DebugLastLevel = '8';
    private const char DebugStatistics = 'D';
    private const char DebugFall = 'F';
    /// <summary>The debug key F plays the fall before the fifth level of the order (LEVEL8).</summary>
    private const int DebugFallIndex = 4;
    /// <summary>The debug key S plays the stream after the first level of the order (LEVEL7).</summary>
    private const char DebugStream = 'S';
    private const int DebugStreamIndex = 0;
    private const int DebugCountLimit = 100;
    /// <summary>The page of the 1996 demo's levels (<see cref="ShowBetaLevels"/>): its title and the
    /// levels' names.</summary>
    private const string BetaTitle = "Beta Levels";
    private static readonly Dictionary<int, string> BetaLevelNames = new()
    {
        [1] = "96 Level 1: City", [3] = "96 Level 3: Wheel Boss", [6] = "96 Level 6: Olympus",
    };

    /// <summary>The page shown, for Esc.</summary>
    private enum Page { Main, Quit, Other }

    private readonly Ui _ui;
    private readonly GameState _state;
    private readonly SaveGames _saves;
    private readonly Fonts _fonts;
    private readonly MenuItems _items;
    private readonly VideoPlayer _video;
    private readonly Slideshow _slideshow;
    private readonly IntroSplash? _splash;
    private readonly PalettedImage? _background;
    private readonly Sound? _song;
    private int _songVoice;
    private int _levelIndex;
    private Page _page;
    private Event _next = Event.None;
    private bool _videoStarted;

    public MainMenu(Ui ui, GameState state, SaveGames saves, MenuPage first)
    {
        _ui = ui;
        _state = state;
        _saves = saves;
        _fonts = new Fonts(ui.Renderer, ui.Fti, mods: ui.CanvasMods());
        _items = new MenuItems(ui, _fonts);
        _video = new VideoPlayer(ui);
        _slideshow = new Slideshow(ui, _video);
        _video.Finished += _slideshow.Start;
        var options = Bni.Load(ui.Data.PathOf(Archive));

        // MDKOPT: a 768-byte palette, then a 600 x 360 image.
        if (options.Has("MDKOPT"))
        {
            var entry = options.Entries["MDKOPT"];
            var palette = options.Bytes.AsSpan(entry.Offset, PaletteSize).ToArray();
            _background = PalettedImage.Of(ui.Renderer, Texture.Parse("MDKOPT", options.Bytes, entry.Offset + PaletteSize), palette, ui.CanvasMods());
        }

        if (state.Splash)
        {
            _splash = new IntroSplash(ui, options);
        }

        state.Splash = false;
        _song = Ui.SoundOf(options, "MAINSONG", Looping.Forever);
        _songVoice = ui.Play(_song, Bus.Music);
        _levelIndex = Math.Max(GameState.IndexOf(state.Level), 0);
        ShowMain();
        if (first == MenuPage.Options)
        {
            ShowOptions();
        }
        else if (first == MenuPage.Controls)
        {
            _items.ShowControls(ShowOptions);
        }
        else if (first == MenuPage.Gamepad)
        {
            _items.ShowGamepad(ShowOptions);
        }
        else if (first == MenuPage.TouchLayout)
        {
            ShowOptions();
            _items.ShowTouchLayout(() => _items.ShowTouch(ShowOptions));
        }
        else if (first == MenuPage.BetaLevels && ui.Beta != null)
        {
            ShowBetaLevels();
        }
    }

    public Event Frame(float elapsed, string? screenshot)
    {
        var input = _ui.Input;
        _ui.View.Layout(ScreenView.Fit.Inside);
        if (_splash is { Done: false })
        {
            _splash.Update(input, elapsed);
            _splash.Draw();
            _ui.Present(screenshot);
            return Event.None;
        }

        StartVideo();
        _video.Update(elapsed);
        _slideshow.Update(elapsed);
        if (input.AnyPressed || input.PointerMoved)
        {
            _slideshow.Reset();
        }

        _items.Visible = _slideshow.ItemsShown;
        HandleKeys(input);
        _items.Update(elapsed);
        if (!_video.Playing && _video.Still == null)
        {
            _background?.Draw(_ui.View, Vector2.Zero);
        }

        _video.Draw();
        _items.Draw();
        _ui.Present(screenshot);
        return _next;
    }

    public IReadOnlyList<string> Status =>
    [
        _splash is { Done: false } ? "intro splash"
            : !_slideshow.ItemsShown ? "main menu: slideshow"
            : _video.Playing ? $"main menu: {_page}, video" : $"main menu: {_page}",
    ];

    private void StartVideo()
    {
        if (_videoStarted)
        {
            return;
        }

        _videoStarted = true;
        _video.Play(MenuVideo);
    }

    /// <summary>Esc on the main page asks "Really Quit?", and there means no; the debug keys.</summary>
    private void HandleKeys(Input input)
    {
        if (!_items.Visible)
        {
            return;
        }

        var typed = input.Typed.ToUpperInvariant();
        if (_page == Page.Main && _state.DebugKeys && DebugKey(typed))
        {
            return;
        }

        if (_page == Page.Main && input.WasPressed(MenuKey.Back))
        {
            ShowQuit();
        }
        else if (_page == Page.Quit && typed.Any(YesKeys.Contains))
        {
            _next = Event.Quit;
        }
        else if (_page == Page.Quit && (typed.Any(NoKeys.Contains) || input.WasPressed(MenuKey.Back)))
        {
            ShowMain();
        }
    }

    /// <summary>The debug keys of <c>SEETHEWHOLEGAME</c> (0x426574, 0x433b50, 0x431b00).</summary>
    private bool DebugKey(string typed)
    {
        foreach (var c in typed)
        {
            if (c is >= DebugFirstLevel and <= DebugLastLevel)
            {
                _state.Level = c - '0';
                _next = Event.Play;
                return true;
            }

            if (c == DebugFall)
            {
                _state.Level = GameState.Order[DebugFallIndex];
                _next = Event.Fall;
                return true;
            }

            if (c == DebugStream)
            {
                _state.Level = GameState.Order[DebugStreamIndex];
                _next = Event.Stream;
                return true;
            }

            if (c != DebugStatistics)
            {
                continue;
            }

            var stats = _state.Stats;
            var random = Random.Shared;
            (stats.Shots, stats.ShotHits, stats.SniperShots, stats.SniperHits) =
                (random.Next(DebugCountLimit), random.Next(DebugCountLimit), random.Next(DebugCountLimit), random.Next(DebugCountLimit));
            (stats.HeadShots, stats.Enemies, stats.Kills) = (random.Next(DebugCountLimit), random.Next(DebugCountLimit), random.Next(DebugCountLimit));
            _next = Event.Statistics;
            return true;
        }

        return false;
    }

    private void ShowQuit()
    {
        _items.Clear();
        _page = Page.Quit;
        _items.FirstRow = QuitRow;
        _items.AddTitle(_ui.Fti.GetText("ABORT1", "Really Quit?"));
        _items.AddItem(_ui.Fti.GetText("ABORT2", "Yes"), () => _next = Event.Quit);
        _items.AddItem(_ui.Fti.GetText("ABORT3", "No"), ShowMain);
        Print();
    }

    private void ShowMain()
    {
        _items.Clear();
        _page = Page.Main;
        if (_saves.Read(SaveGames.LastGame) != null)
        {
            _items.AddItem(_ui.Fti.GetText("OPT0", "Continue"), () => Load(SaveGames.LastGame));
        }

        _items.AddItem(_ui.Fti.GetText("OPT1", "New Game"), NewGame);
        _items.AddItem(LevelText(), NextLevel);
        _items.AddItem(_ui.Fti.GetText("OPT2", "Saved Game"), ShowSaves);
        if (_ui.Beta != null)
        {
            _items.AddItem(BetaTitle, ShowBetaLevels);
        }

        _items.AddItem(_ui.Fti.GetText("OPT3", "Options"), ShowOptions);
        _items.AddItem(_ui.Fti.GetText("OPT4", "Quit"), () => _next = Event.Quit);

        // The main page is a column at the view's left edge (0x4265c0).
        _items.Alignment = MenuItems.Align.Left;
        Print();
    }

    /// <summary>The saved games (<c>SVOPT1</c>, or <c>SVOPT3</c> when there are none), with their level.</summary>
    private void ShowSaves()
    {
        _items.Clear();
        _page = Page.Other;
        var names = _saves.List();
        _items.AddTitle(_ui.Fti.GetText(names.Count != 0 ? "SVOPT1" : "SVOPT3", "Saved games").Split('\n')[0]);
        foreach (var name in names)
        {
            var save = _saves.Read(name);
            if (save == null)
            {
                _items.AddDisabled($"{name}  ({_ui.Fti.GetText("SVBAD", "Invalid")})");
                continue;
            }

            _items.AddItem($"{name}  (Level {GameState.IndexOf(save.Level) + 1})", () => Load(name));
        }

        _items.AddItem("Back", ShowMain);
        Print();
    }

    /// <summary>A save made before a level shows its briefing first; the others start the level.</summary>
    private void Load(string name)
    {
        var save = _saves.Read(name);
        if (save == null)
        {
            return;
        }

        _state.Load(save);
        Console.WriteLine($"Loaded game {name}: level {save.Level}");
        _next = save.Kind == SaveKind.BeforeLevel ? Event.Briefing : Event.Play;
    }

    /// <summary><c>MAINSONG</c> stops while the options play <c>OPTSONG</c> (0x42bb6c, 0x42bbc0).</summary>
    private void ShowOptions()
    {
        _page = Page.Other;
        _ui.Audio.Stop(_songVoice);
        _items.ShowOptions(() =>
        {
            _songVoice = _ui.Play(_song, Bus.Music);
            ShowMain();
        });
    }

    /// <summary>The 1996 demo's levels (godot-mdk docs/beta96.md), a page of the port's own.</summary>
    private void ShowBetaLevels()
    {
        _items.Clear();
        _page = Page.Other;
        _items.AddTitle(BetaTitle);
        foreach (var level in BetaDemo.Levels)
        {
            _items.AddItem(BetaLevelNames[level], () => PlayBeta(level));
        }

        _items.AddItem("Back", ShowMain);
        Print();
    }

    /// <summary>The demo's levels start at once: it has no briefing or fall.</summary>
    private void PlayBeta(int level)
    {
        _state.NewGame(0);
        _state.Level = BetaDemo.NumberOf(level);
        _state.Carry = null;
        _next = Event.Play;
    }

    /// <summary>The briefing, the fall, then the level.</summary>
    private void NewGame()
    {
        _state.NewGame(_levelIndex);
        _next = Event.Briefing;
    }

    private void NextLevel()
    {
        _levelIndex = (_levelIndex + 1) % GameState.Order.Length;
        ShowMain();
        _items.Select(_page == Page.Main && _saves.Read(SaveGames.LastGame) != null ? 2 : 1);
    }

    private string LevelText() => $"Level: {_levelIndex + 1}";

    /// <summary>The page's items, for tests.</summary>
    private void Print() => Console.WriteLine($"Menu: {_items.Describe()}");

    public void Dispose()
    {
        _video.Dispose();
        _items.Clear();
    }
}
