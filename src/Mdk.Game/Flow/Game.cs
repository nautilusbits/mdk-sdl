using System.Diagnostics;
using System.Security.Cryptography;
using Mdk.Engine.Audio;
using Mdk.Engine.Diagnostics;
using Mdk.Engine.Platform;
using Mdk.Engine.Render;
using Mdk.Formats;
using Mdk.Game.DevTools;
using Mdk.Game.HdTextures;
using Mdk.Game.Hud;
using Mdk.Game.Menu;
using Mdk.Game.Mods;

namespace Mdk.Game.Flow;

/// <summary>How the game starts (command line): the screen shown first.</summary>
public enum Start { Menu, Level, Statistics, Briefing, EndMovie, Stream, Fall }

/// <summary>The command line's choices: where the game starts, the first level's test options, the
/// statistics' test counts, a save to load or write, a screenshot.</summary>
/// <summary>Whether the game shows its window.</summary>
public enum Display { Window, Hidden }

public sealed record GameOptions(Start Start, ViewerOptions Level)
{
    /// <summary>Hidden: no window is shown or focused, frames are drawn off screen (tests).</summary>
    public Display Display { get; init; }
    public MenuPage Page { get; init; }
    /// <summary>The menu shows the splash first even when it's started directly.</summary>
    public bool Splash { get; init; }
    /// <summary>The statistics start at this page.</summary>
    public StatsScreen.Phase? Phase { get; init; }
    /// <summary>The statistics' counts: shots, hits, sniper rounds, sniper hits, kills, enemies, heads.</summary>
    public IReadOnlyList<int> Counts { get; init; } = [];
    /// <summary>The town flags' bits 31-29 for the debriefing.</summary>
    public int? Towns { get; init; }
    /// <summary>A save to load first, and a save written when the first level starts.</summary>
    public string? Load { get; init; }
    public string? Save { get; init; }
    /// <summary>A screen other than the first level saves this frame after <see cref="Wait"/> seconds, then the game quits.</summary>
    public string? Screenshot { get; init; }
    public float Wait { get; init; }
    /// <summary>Kurt's health in a stream started directly (--stream).</summary>
    public int? Health { get; init; }
    /// <summary>The look instead of the settings' (--enhanced, --original; tests).</summary>
    public Graphics? Graphics { get; init; }
    /// <summary>Gore instead of the settings' (the original's -bloodyes, -nobloodno).</summary>
    public bool? Gore { get; init; }
    /// <summary>The stereo layout instead of the settings' (--stereo; tests), and its eyes'
    /// separation and convergence (--stereo-separation, --stereo-convergence; not saved).</summary>
    public Stereo? Stereo { get; init; }
    public float? StereoSeparation { get; init; }
    public float? StereoConvergence { get; init; }
    /// <summary>The display's gamma instead of the settings' (--gamma; tests).</summary>
    public float? Gamma { get; init; }
    /// <summary>The enhanced look's cost instead of the OS's (--quality; tests, measurements).</summary>
    public EffectQuality? Quality { get; init; }
    /// <summary>Keys pressed once at given times (--press; tests).</summary>
    public TestPresses? Presses { get; init; }
    /// <summary>Only these mods on, by folder (--mod; tests; saved only if the options change).</summary>
    public IReadOnlyList<string>? Mods { get; init; }
    /// <summary>Measure the frames after this many seconds of game time, waiting for the GPU each
    /// frame, and print their costs at the end (--perf; tests).</summary>
    public float? Perf { get; init; }
    /// <summary>The GPU backend instead of the settings' (--gpu=d3d12|vulkan|metal|auto; tests).</summary>
    public string? Gpu { get; init; }
    /// <summary>A hidden window's frame size instead of 1280 x 960 (--size; tests, videos).</summary>
    public Resolution? HiddenSize { get; init; }
    /// <summary>A hidden window's frames through its swapchain too (--swapchain; tests).</summary>
    public HiddenFrames HiddenFrames { get; init; }
    /// <summary>The sound mixed from the start saved as this WAV (--record-audio; with --mute, in step with --frames).</summary>
    public string? RecordAudio { get; init; }
    /// <summary>The platform's import of the game files again (Android: the folder picked again,
    /// HD textures made on a PC with it); null where the game reads them in place.</summary>
    public Action? Import { get; init; }
}

/// <summary>The game: its screens one after the other, as the original's game states.
/// <code>
///   splash ─► menu ─new game─► briefing ──fall──► loading ─► level ─tornado─► stream ─► statistics
///              ▲  └continue/save─► loading                     │                       save prompt
///              │                                               │ died: LASTGAME           briefing ──fall──► next level
///              └───────────────────────────────────────────────┘ event 81: end movies ─► splash ─► menu
/// </code></summary>
public sealed class Game : IDisposable
{
    /// <summary>Where older builds kept settings and saves, under the local data folder.</summary>
    private const string UserFolderName = "mdk-sdl";
    /// <summary>The saves' folder in the user folder (<see cref="SaveGames"/>).</summary>
    private const string SavesFolder = "saves";
    /// <summary>Overrides the user folder (settings and saves), for tests.</summary>
    private const string UserFolderVariable = "MDK_USER_DIR";
    private const string LogFile = "mdk.log";
    private const int TownShift = 29;
    /// <summary>The soak test's frames are this many game steps (0.1 s): it runs faster.</summary>
    private const int SoakSteps = 6;
    /// <summary>The overlay's line between screens (the loading screen).</summary>
    private const string Loading = "loading";
    /// <summary>How often --upscale-textures prints its progress (ms).</summary>
    private const int UpscalePoll = 1000;

    private readonly GameOptions _options;
    private readonly Window _window;
    private readonly Renderer _renderer;
    private readonly AudioDevice _audio;
    private readonly Input _input = new();
    private readonly Ui _ui;
    private readonly GameState _state = new();
    private readonly SaveGames _saves;
    private readonly Renderer.Scope _scope;
    /// <summary>The console and the overlay, over every screen.</summary>
    private readonly DevUi _dev;
    private Typing _typing;
    /// <summary>The console as last seen: opening it shows a touch screen's keyboard, closing hides it.</summary>
    private ConsoleState _consoleSeen;
    private readonly GameCommands _commands;
    /// <summary>The soak test's random keys, on every screen; menu keys too when it starts in the
    /// menus or the flow screens.</summary>
    private readonly SoakKeys? _soak;
    private readonly bool _soakMenus;
    private IScreen? _screen;
    /// <summary>The first level gets the command line's test options; later ones don't.</summary>
    private bool _firstLevel = true;
    /// <summary>The level shown takes its own screenshot (the first, with --level).</summary>
    private bool _levelShot;
    /// <summary>The level that ended, for its counts.</summary>
    private Viewer? _viewer;
    private readonly PerfLog? _perf;
    private readonly DevUiView _devView;
    /// <summary>The display as last logged: a change is logged again.</summary>
    private ScreenInfo _screenInfo;

    /// <summary>--gpu's backend, else the settings'.</summary>
    private static GpuBackend Backend(string? name, Settings settings)
    {
        if (name == null)
        {
            return settings.Backend;
        }

        if (GpuBackends.Parse(name) is { } backend)
        {
            return backend;
        }

        Console.Error.WriteLine($"--gpu={name}: d3d12, vulkan, metal or auto; using the settings'");
        return settings.Backend;
    }

    /// <summary>The program, the GPU and the look, in the log (and the console); the display's line
    /// follows with the first frame.</summary>
    private void LogTechInfo(Settings settings)
    {
        Console.WriteLine(TechInfo.Build(BuildInfo.Current()));
        Console.WriteLine(TechInfo.Gpu(_renderer.Gpu));
        var mods = _ui.Mods.Enabled(settings).Select(m => m.Folder).ToList();
        Console.WriteLine(TechInfo.Look(settings.Graphics, mods));
    }

    /// <summary>The display now; read every frame (no allocation).</summary>
    private ScreenInfo ScreenNow()
    {
        var (width, height) = _window.Size;
        return new ScreenInfo(_window.Desktop, new Resolution(width, height), _window.Fullscreen, _window.Visibility,
            _renderer.Target, _renderer.SwapchainFormat, _renderer.DepthFormat, _renderer.Samples, _renderer.VSync);
    }

    /// <summary>A changed display (size, mode, frame, VSync) is logged and shown by the overlay.</summary>
    private void WatchScreen()
    {
        var now = ScreenNow();
        if (now == _screenInfo)
        {
            return;
        }

        _screenInfo = now;
        Console.WriteLine(TechInfo.Display(now));
        Console.WriteLine(TechInfo.Layout(_window.UnitsPerMm, _window.SafeArea, _renderer.RenderScale, now.Window, _renderer.Quality));
        _devView.System = TechInfo.Overlay(_renderer.Gpu, now);
    }

    /// <summary>With VSync off or adaptive, frames wait out the frame limit (not in tests: their
    /// frames are game steps).</summary>
    private void LimitFrame(TimeSpan elapsed, TestRun test)
    {
        if (test == TestRun.Yes || _renderer.VSync == VSync.On)
        {
            return;
        }

        FrameLimiter.Wait(elapsed, _ui.Settings.FrameLimit);
    }

    private enum TestRun { No, Yes }

    public Game(MdkData data, GameOptions options)
    {
        _options = options;
        var folder = Environment.GetEnvironmentVariable(UserFolderVariable) ?? UserFolder();
        WriteLog(folder);
        _saves = SaveGames.In(folder);

        // The original deletes LASTGAME.SAV when it quits.
        _saves.Delete(SaveGames.LastGame);
        var settings = Settings.Load(Settings.PathIn(folder));
        var visibility = options.Display == Display.Hidden ? Visibility.Hidden : Visibility.Shown;
        var size = visibility == Visibility.Shown ? settings.WindowSize : Settings.DefaultWindow;
        _window = new Window("MDK", size.Width, size.Height, visibility);
        _renderer = new Renderer(_window, Backend(options.Gpu, settings));
        if (options.HiddenSize is { } hiddenSize)
        {
            _renderer.HiddenSize = hiddenSize;
        }

        _renderer.HiddenFrames = options.HiddenFrames;

        _audio = new AudioDevice(options.Level.Sound == SoundMode.Muted ? Output.Muted : Output.Speakers);
        if (options.RecordAudio is { } recording)
        {
            _audio.Record(recording);
        }

        // Tests choose the look (saved only if the options change).
        if (options.Graphics is { } graphics)
        {
            settings.Graphics = graphics;
        }

        if (options.Gore is { } gore)
        {
            settings.Gore = gore;
        }

        if (options.Stereo is { } stereo)
        {
            settings.Stereo = stereo;
        }

        if (options.StereoSeparation is { } separation)
        {
            settings.StereoSeparation = separation;
        }

        if (options.StereoConvergence is { } convergence)
        {
            settings.StereoConvergence = convergence;
        }

        if (options.Gamma is { } gamma)
        {
            settings.Gamma = gamma;
        }

        if (options.Quality is { } quality)
        {
            _renderer.Quality = quality;
        }

        settings.Apply(_audio, _window, _renderer, _input);
        // Older builds' HD textures become the HD textures mod.
        if (HdGenerator.Migrate(folder) is > 0 and var moved)
        {
            Console.WriteLine($"HD textures: {moved} images moved into mods/{HdGenerator.ModFolder}");
        }

        _ui = new Ui(data, _window, _renderer, _audio, _input, settings, folder) { Import = options.Import, TouchEditor = options.Page == MenuPage.TouchLayout };
        if (options.Mods is { } mods)
        {
            UseOnly(mods);
        }

        // The developer tools draw over whatever screen presents; their font outlives the screens.
        _commands = new GameCommands(_ui, _state, () => (_screen as Viewer)?.Commands);
        var registry = new CommandRegistry();
        ConsoleCommands.Register(registry, _commands);
        _dev = new DevUi(new DevConsole(registry, _ui.Dev.History, _ui.Dev.Log, Console.WriteLine), () => _ui.Dev.ToggleOverlay());
        var small = new Fonts(_renderer, _ui.Fti, mods: _ui.CanvasMods()).Small;
        var devView = _devView = new DevUiView(_renderer, small);
        devView.Console.CloseButton = _window.HasTouchScreen ? CloseButton.Shown : CloseButton.Hidden;
        // The on-screen controls of a touch screen (Android), under the developer tools.
        var touchView = new TouchView(_renderer, small);
        // Made once: a lambda made in the overlay's would be made every frame.
        Func<IReadOnlyList<string>> status = () => _screen?.Status ?? [Loading];
        _renderer.Overlay = () =>
        {
            touchView.Draw(_window);
            devView.Draw(_dev, _ui.Dev, status);
        };
        _scope = _renderer.Mark();
        LogTechInfo(settings);
        _soak = options.Level.Soak is { } seed ? new SoakKeys(seed) : null;
        _soakMenus = options.Start is Start.Menu or Start.Statistics or Start.Briefing or Start.EndMovie;
        if (options.Perf is { } warmup)
        {
            _perf = new PerfLog(warmup);
            _renderer.Sync = GpuSync.Wait;
        }
    }

    /// <summary>Runs the screens until the window closes or one quits.</summary>
    public void Run()
    {
        var first = FirstEvent();
        if (first == Event.Quit)
        {
            return;
        }

        Handle(first);
        var test = _options.Screenshot != null || _options.Level.Frames != null;
        var step = _soak != null ? Viewer.Step * SoakSteps
            : _options.Level.FrameRate is { } fps ? 1f / fps
            : Viewer.Step;
        var clock = Stopwatch.StartNew();
        var time = 0f;
        var frameStep = 0;
        while (_screen != null && _window.PumpEvents(_input))
        {
            var elapsed = test ? step : (float)clock.Elapsed.TotalSeconds;
            clock.Restart();
            time += elapsed;
            _soak?.Hold(_input, time);
            if (_soakMenus)
            {
                _soak?.PressMenu(_input, time);
            }

            _options.Presses?.Apply(_input, time);

            // The open console has the keys: the screen runs on without them.
            OpenAskedConsole();
            TouchConsole();
            _dev.Update(_input, elapsed);
            UpdateTyping();
            if (_screen is not Viewer { ConsoleReady: false })
            {
                _dev.RunOnce(_options.Level.Console);
            }

            var shot = test && !_levelShot && time >= _options.Wait ? _options.Screenshot : null;

            // --frames of a screen before the level (the fall): the level's own are the viewer's.
            var frames = _options.Start != Start.Level && time >= _options.Wait ? _options.Level.Frames : null;
            var frame = frames?.PathOf(frameStep++);
            var next = _screen.Frame(elapsed * _commands.TimeScale, shot ?? frame);

            // A console command leaves the screen (map, load, quit).
            var command = _commands.TakeNext();
            if (command != Event.None)
            {
                next = command;
            }

            using (_ui.Dev.Profiler.Measure(Section.Audio))
            {
                _audio.Update(elapsed);
            }

            _ui.Dev.Profiler.EndFrame();
            WatchScreen();
            LimitFrame(clock.Elapsed, test ? TestRun.Yes : TestRun.No);
            _perf?.Frame(time, _ui.Dev.Profiler, _renderer.Stats);
            if (shot != null)
            {
                Console.WriteLine($"Saved {shot}");
                break;
            }

            if (frames != null && frames.IsLast(frameStep - 1))
            {
                break;
            }

            if (next != Event.None)
            {
                Handle(next);
            }
        }

        foreach (var line in _perf?.Report() ?? [])
        {
            Console.WriteLine(line);
        }
    }

    /// <summary>The first screen from the command line (a save to load first).</summary>
    private Event FirstEvent()
    {
        ApplyTestCounts();
        if (_options.Load is { } name)
        {
            var save = _saves.Read(name);
            if (save == null)
            {
                Console.Error.WriteLine($"No saved game {name}");
                return Event.Quit;
            }

            _state.Load(save);
            Console.WriteLine($"Loaded game {name}: level {save.Level}");
            return save.Kind == SaveKind.BeforeLevel ? Event.Briefing : Event.Play;
        }

        _state.Level = _options.Level.Level;
        switch (_options.Start)
        {
            case Start.Level:
                return Event.Play;
            case Start.Statistics:
                return Event.Statistics;
            case Start.Briefing:
                return Event.Briefing;
            case Start.EndMovie:
                return Event.GameFinished;
            case Start.Stream:
                return Event.Stream;
            case Start.Fall:
                return Event.Fall;
        }

        _state.Splash = _options.Splash;
        return Event.Menu;
    }

    /// <summary>The statistics' test counts (--counts, --towns).</summary>
    private void ApplyTestCounts()
    {
        var stats = _state.Stats;
        int[] counts = [.. _options.Counts, .. Enumerable.Repeat(0, 7)];
        if (_options.Counts.Count != 0)
        {
            (stats.Shots, stats.ShotHits, stats.SniperShots, stats.SniperHits) = (counts[0], counts[1], counts[2], counts[3]);
            (stats.Kills, stats.Enemies, stats.HeadShots) = (counts[4], counts[5], counts[6]);
        }

        if (_options.Towns is { } towns)
        {
            stats.TownFlags = towns << TownShift;
        }
    }

    /// <summary>What follows a screen's event.</summary>
    private void Handle(Event next)
    {
        switch (next)
        {
            case Event.Quit:
                Show(() => null);
                break;
            case Event.Menu:
                Show(() => new MainMenu(_ui, _state, _saves, _firstLevel ? _options.Page : MenuPage.Main));
                break;
            case Event.Play:
                Play();
                break;
            case Event.Briefing:
                Show(() => new StatsScreen(_ui, _state, _saves, StatsPages.Briefing, null));
                break;
            case Event.Statistics:
                Show(() => new StatsScreen(_ui, _state, _saves, StatsPages.All, _firstLevel ? _options.Phase : null));
                break;
            case Event.LevelEnded:
                AfterLevel(Scripts.ScriptRuntime.GameOver.No);
                break;
            case Event.GameOver:
                AfterLevel(Scripts.ScriptRuntime.GameOver.Yes);
                break;
            case Event.KurtDied:
                // The level is saved as LASTGAME; the menu's "Continue" starts it again (not the 1996 demo's).
                if (!BetaDemo.IsBeta(_state.Level))
                {
                    _saves.Write(SaveGames.LastGame, _state.Save(SaveKind.LevelStart));
                }

                _state.Splash = true;
                Handle(Event.Menu);
                break;
            case Event.GameFinished:
                Show(() => new EndMovie(_ui));
                _state.Splash = true;
                break;
            case Event.Stream:
                ShowStream(_options.Health ?? SaveGame.FullHealth);
                break;
            case Event.Fall:
                Show(() => new Fall.FallScreen(_ui, _state, _firstLevel ? _options.Level : new ViewerOptions(0, null, null, 0f, _options.Level.Sound)));
                break;
            case Event.StreamEnded:
                AfterStream();
                break;
        }

        _firstLevel = false;
    }

    /// <summary>The loading screen, then the level.</summary>
    private void Play()
    {
        Show(() => null);
        LoadingScreen.Show(_ui, _state.Level, 0f);
        Show(() => null);
        var test = _firstLevel ? _options.Level : new ViewerOptions(0, null, null, 0f, _options.Level.Sound);
        _levelShot = _firstLevel && _options.Start == Start.Level && test.Screenshot != null;
        var loading = Stopwatch.StartNew();
        _viewer = new Viewer(_ui, test with { Level = _state.Level }, _state);
        _perf?.Loaded(loading.Elapsed);
        _screen = _viewer;
        if (_firstLevel && _options.Save is { } name && _saves.Write(name, _state.Save(SaveKind.LevelStart)))
        {
            Console.WriteLine($"Saved game {name}: level {_state.Level}");
        }
    }

    /// <summary>The level ended (the tornado is over): its counts are kept, then the stream or the menu.</summary>
    private void AfterLevel(Scripts.ScriptRuntime.GameOver over)
    {
        if (_viewer != null)
        {
            _state.Stats = _viewer.Stats;
        }

        if (LevelFlow.AfterLevel(_state.Level, over) == LevelFlow.After.Menu)
        {
            Handle(Event.Menu);
            return;
        }

        ShowStream(_viewer?.Health ?? SaveGame.FullHealth);
    }

    /// <summary>The stream after the level, with Kurt's health. Started directly (--stream) its tube
    /// is the one of the C library's first seed, as tests expect.</summary>
    private void ShowStream(int health)
    {
        var direct = _firstLevel && _options.Start == Start.Stream;
        var seed = direct ? Stream.WatcomRandom.DefaultSeed : (uint)Environment.TickCount;
        Show(() => new Stream.StreamScreen(_ui, _state.Level, health, new Stream.WatcomRandom(seed)));
    }

    /// <summary>The stream is over: the statistics, or the last level.</summary>
    private void AfterStream()
    {
        var level = _state.Level;
        switch (LevelFlow.AfterLevel(level, Scripts.ScriptRuntime.GameOver.No))
        {
            case LevelFlow.After.Statistics:
                Handle(Event.Statistics);
                break;
            case LevelFlow.After.LastLevel:
                // LEVEL5 follows at once, after a save prompt (kind 3, "6"), with the stream's health.
                _state.EnterLastLevel((_screen as Stream.StreamScreen)?.Health ?? SaveGame.FullHealth);
                Show(() => new SavePromptScreen(_ui, _saves, _state.Save(SaveKind.LevelStart), LevelFlow.SaveName(_state.Level)));
                break;
            default:
                Handle(Event.Menu);
                break;
        }
    }

    /// <summary>The next screen, created once the last one's resources are freed.</summary>
    private void Show(Func<IScreen?> create)
    {
        _screen?.Dispose();
        _screen = null;
        // Only a level's Kurt snipes: the scope's bindings don't carry over to the next screen.
        _input.Scoped = false;
        _levelShot = false;
        _audio.StopAll();
        _renderer.Release(_scope);
        _screen = create();
    }

    /// <summary>Makes the HD textures in the user folder without a window (--upscale-textures),
    /// printing the progress; returns the exit code.</summary>
    /// <summary>--export-assets: the game's textures, 2D images and models for modding (<see cref="AssetExport"/>).</summary>
    public static int ExportAssets(MdkData data, string folder)
    {
        Console.WriteLine($"Exported to {folder}: {AssetExport.Run(data, folder)}");
        return 0;
    }

    /// <summary>--download-test: fetches a URL through the OS's HTTP library, prints its size and
    /// SHA-256 (CI proves the native download works on each platform).</summary>
    public static int DownloadTest(string url)
    {
        var path = Path.GetTempFileName();
        try
        {
            HttpDownload.Save(url, path, null, CancellationToken.None);
            using var file = File.OpenRead(path);
            Console.WriteLine($"Downloaded {file.Length} bytes, SHA-256 {Convert.ToHexStringLower(SHA256.HashData(file))}");
            return 0;
        }
        catch (IOException e)
        {
            Console.Error.WriteLine($"Download failed: {e.Message}");
            return 1;
        }
        finally
        {
            File.Delete(path);
        }
    }

    public static int UpscaleTextures(MdkData data, HdOptions options)
    {
        // The upscaler's builds are desktop-only.
        if (!HdMenu.CanMake(HdMenu.Current))
        {
            Console.Error.WriteLine("HD textures can't be made on this platform");
            return 1;
        }

        var folder = Environment.GetEnvironmentVariable(UserFolderVariable) ?? UserFolder();
        var source = Settings.Load(Settings.PathIn(folder)).Upscaler;
        var progress = new HdProgress();
        var run = Task.Run(() => HdGenerator.Run(data, folder, options with { Source = source }, progress, CancellationToken.None));
        var last = "";
        while (!run.Wait(UpscalePoll))
        {
            var line = progress.Describe();
            if (line != last)
            {
                Console.WriteLine(line);
                last = line;
            }
        }

        if (run.IsFaulted)
        {
            Console.Error.WriteLine($"HD textures failed: {run.Exception!.GetBaseException().Message}");
            return 1;
        }

        Console.WriteLine($"HD textures: {run.Result}");
        return 0;
    }

    /// <summary>--mod: only these mods on (unknown ones named).</summary>
    private void UseOnly(IReadOnlyList<string> mods)
    {
        foreach (var unknown in mods.Where(m => !_ui.Mods.Mods.Any(f => string.Equals(f.Folder, m, StringComparison.OrdinalIgnoreCase))))
        {
            Console.Error.WriteLine($"No mod {unknown} in {ModCatalog.FolderIn(_ui.UserFolder)}");
        }

        _ui.Mods.Only(_ui.Settings, mods);
    }

    /// <summary>Settings and saves live next to the executable (a portable install), or in the
    /// user's data folder when it's protected (<see cref="UserFolders"/>). The first portable run
    /// moves over what an older build kept in the local data folder.</summary>
    private static string UserFolder()
    {
        var folder = UserFolders.Choose(AppContext.BaseDirectory, UserFolders.Personal());
        if (folder != AppContext.BaseDirectory)
        {
            return folder;
        }

        var old = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), UserFolderName);
        if (File.Exists(Settings.PathIn(folder)) || !File.Exists(Settings.PathIn(old)))
        {
            return folder;
        }

        File.Copy(Settings.PathIn(old), Settings.PathIn(folder));
        var oldSaves = Path.Combine(old, SavesFolder);
        if (!Directory.Exists(oldSaves))
        {
            return folder;
        }

        var saves = Directory.CreateDirectory(Path.Combine(folder, SavesFolder)).FullName;
        foreach (var file in Directory.GetFiles(oldSaves))
        {
            File.Copy(file, Path.Combine(saves, Path.GetFileName(file)), overwrite: false);
        }

        return folder;
    }

    /// <summary>The console a menu asked for opens (the game's options; phones have no Grave key).</summary>
    private void OpenAskedConsole()
    {
        if (!_ui.Dev.ConsoleAsked)
        {
            return;
        }

        _ui.Dev.ConsoleAsked = false;
        if (_dev.Console.State == ConsoleState.Closed)
        {
            _dev.Console.Toggle();
        }
    }

    /// <summary>A touch screen's keyboard shows when the console opens and hides when it closes.</summary>
    private void UpdateTyping()
    {
        var state = _dev.Console.State;
        if (state == _consoleSeen)
        {
            return;
        }

        _consoleSeen = state;
        SetTyping(state == ConsoleState.Open ? Typing.On : Typing.Off);
    }

    /// <summary>Shows or hides a touch screen's keyboard.</summary>
    private void SetTyping(Typing typing)
    {
        if (typing == _typing)
        {
            return;
        }

        _typing = typing;
        _window.SetTyping(typing);
    }

    /// <summary>A touch screen's tap on the open console: its close button or the game below close
    /// it (the tap goes no further), its line shows the keyboard, its log hides it.</summary>
    private void TouchConsole()
    {
        if (!_window.HasTouchScreen || _dev.Console.State != ConsoleState.Open || !_input.WasClicked(Pointer.Left))
        {
            return;
        }

        var point = _renderer.CanvasPoint(_input.PointerX, _input.PointerY);
        switch (_devView.Console.Spot(_dev.Console, point))
        {
            case ConsoleSpot.Close:
            case ConsoleSpot.Below:
                _dev.Console.Press(EditKey.Close);
                _input.Withhold();
                break;
            case ConsoleSpot.Line:
                SetTyping(Typing.On);
                break;
            case ConsoleSpot.Log:
                SetTyping(Typing.Off);
                break;
        }
    }

    /// <summary>The output also goes to mdk.log in the user folder (phones have no console to read):
    /// made anew each start. A folder it can't write to keeps the output on screen only.</summary>
    private static void WriteLog(string folder)
    {
        try
        {
            Directory.CreateDirectory(folder);
            Log.Capture(Log.Lines).WriteTo(Path.Combine(folder, LogFile));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Console.Error.WriteLine($"No log file in {folder}: {e.Message}");
        }
    }

    public void Dispose()
    {
        Log.Capture(Log.Lines).Close();
        _screen?.Dispose();
        _audio.Dispose();
        _renderer.Dispose();
        _window.Dispose();
    }
}

/// <summary>The save prompt alone (after LEVEL8), then the level.</summary>
public sealed class SavePromptScreen(Ui ui, SaveGames saves, SaveGame save, string name) : IScreen
{
    private readonly SavePrompt _prompt = new(ui, new Fonts(ui.Renderer, ui.Fti, mods: ui.CanvasMods()), saves, save, name);

    public Event Frame(float elapsed, string? screenshot)
    {
        ui.View.Layout(ScreenView.Fit.Inside);
        _prompt.Update(ui.Input, elapsed);
        _prompt.Draw();
        ui.Present(screenshot);
        return _prompt.Closed ? Event.Play : Event.None;
    }

    public IReadOnlyList<string> Status => ["save prompt"];

    public void Dispose()
    {
    }
}
