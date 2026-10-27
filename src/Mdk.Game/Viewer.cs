using System.Diagnostics;
using System.Globalization;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Text;
using Mdk.Engine.Audio;
using Mdk.Engine.Diagnostics;
using Mdk.Engine.Platform;
using Mdk.Engine.Render;
using Mdk.Formats;
using Mdk.Game.Flow;
using Mdk.Game.Menu;
using Mdk.Game.Audio;
using Mdk.Game.Collision;
using Mdk.Game.DevTools;
using Mdk.Game.HdTextures;
using Mdk.Game.Mods;
using Mdk.Game.Hud;
using Mdk.Game.Kurt;
using Mdk.Game.Level;
using Mdk.Game.Objects;
using Mdk.Game.Scripts;

namespace Mdk.Game;

/// <summary>How a session starts, and what tests make it do: hold keys for some seconds, then save
/// a frame and quit.</summary>
public sealed record ViewerOptions(int Level, Vector3? Position, float? Yaw, float Pitch, SoundMode Sound)
{
    /// <summary>Save a frame after <see cref="Wait"/> seconds of game time, print Kurt's state, and quit.</summary>
    public string? Screenshot { get; init; }
    public float Wait { get; init; }
    /// <summary>Hold "forward" this many seconds from the start.</summary>
    public float Walk { get; init; }
    /// <summary>The follow camera's look down (degrees; negative: up), as the mouse's.</summary>
    public float Look { get; init; }
    /// <summary>Seconds before the held keys start (like the Godot port's --delay).</summary>
    public float Delay { get; init; }
    public bool Jump { get; init; }
    /// <summary>Hold "fire".</summary>
    public bool Fire { get; init; }
    /// <summary>Pickups Kurt starts with (SW_HBOMB...), and press "use" once he has landed.</summary>
    public IReadOnlyList<string> Give { get; init; } = [];
    public bool Use { get; init; }
    public bool Fly { get; init; }
    /// <summary>Print the scripted objects once per second of game time.</summary>
    public bool Profile { get; init; }
    /// <summary>Print every frame where Kurt, the camera and the object carrying him are drawn.</summary>
    public bool Trace { get; init; }
    /// <summary>Sniper mode once Kurt stands, at this zoom (X) and pitch (Y); see <see cref="SniperTest"/>.</summary>
    public Vector2? Sniper { get; init; }
    /// <summary>Hold "zoom in" this many seconds in sniper mode.</summary>
    public float Zoom { get; init; }
    /// <summary>Fire one sniper round once the clip is loaded.</summary>
    public bool SniperFire { get; init; }
    /// <summary>Bones' full-screen strike after a second.</summary>
    public StrikeScene.Plane? Strike { get; init; }
    /// <summary>Kurt is hurt to death after a second.</summary>
    public bool Die { get; init; }
    /// <summary>A special_event after a second (1 ends the level).</summary>
    public int? Event { get; init; }
    /// <summary>After the delay: a teleport, an object killed, a walker ridden, the XE boarded (see <see cref="RideTest"/>).</summary>
    public TeleportTarget? Teleport { get; init; }
    public string? Kill { get; init; }
    public string? Ride { get; init; }
    public BomberTest? Bomber { get; init; }
    /// <summary>At the screenshot, a full save of this name (as F2), its hash printed.</summary>
    public string? Snapshot { get; init; }
    /// <summary>The soak test's seed: random keys (<see cref="SoakKeys"/>) and checks (<see cref="SoakTest"/>) until the screenshot.</summary>
    public int? Soak { get; init; }
    public SoakRoute Route { get; init; }
    /// <summary>After the delay: one of the 1996 demo's teleports, a roll held (tests).</summary>
    public int? BetaTeleport { get; init; }
    public BetaRoll? Roll { get; init; }
    /// <summary>After the delay: the console opens and runs these commands ("pos;god").</summary>
    public string? Console { get; init; }
    /// <summary>After the wait: frames saved, then the game quits (--frames).</summary>
    public FrameDump? Frames { get; init; }
    /// <summary>Test frames a second (--fps): above 60, frames are drawn between the steps.</summary>
    public int? FrameRate { get; init; }
}

/// <summary>A roll of the 1996 demo's levels.</summary>
public enum BetaRoll { Left, Right }

public enum SoundMode { On, Muted }

/// <summary>Plays a level: Kurt walks it (F1 switches to a flying camera), the scripts run. Esc
/// opens the pause menu. It ends when Kurt dies, the level ends (after the tornado) or the game ends.
/// <code>
///   input ──► Kurt (60 steps/s) ──► scripts (30 ticks/s) ──► camera ──► level, objects, sky, Kurt ──► frame
///                └── mixer (listener at the camera)
/// </code></summary>
public sealed class Viewer : IScreen
{
    /// <summary>Kurt moves in fixed steps; the tests' frames are steps too.</summary>
    public const float Step = 1f / 60f;
    /// <summary>Steps caught up at most after a slow frame.</summary>
    private const int MaxSteps = 10;
    /// <summary>The start is slightly below the landing pad (the original lands Kurt by chute).</summary>
    private const float StartDrop = 3f;
    /// <summary>A pickup's name stays this long.</summary>
    private const float PickupMessageSeconds = 2f;
    /// <summary>Quick save and load messages ("Game saved") stay this long.</summary>
    private const float NoticeSeconds = 2f;
    /// <summary>The test's "use" is held from 1 second on, for 0.1 (game time).</summary>
    private const float UseStart = 1f;
    private const float UseTime = 0.1f;
    /// <summary>The tests' --die and --event happen after 1 second (game time).</summary>
    private const float TestStart = 1f;
    private const int FatalDamage = 1000;
    /// <summary>The game goes on this long after the level ended, longer when it's lost (main.gd).</summary>
    private const float EndDelay = 0.5f;
    private const float GameOverDelay = 5f;

    private readonly Ui _ui;
    private readonly ViewerOptions _options;
    private readonly GameState _state;
    private readonly Renderer _renderer;
    private readonly AudioDevice _audio;
    private readonly LevelData _level;
    private readonly LevelView _view;
    private readonly SoundMixer _mixer;
    private readonly LevelMusic _music;
    private readonly ArenaSpace _space;
    private readonly KurtSprite _sprite;
    private readonly Kurt.Kurt _kurt;
    private readonly ScriptRuntime _scripts;
    private readonly HudView _hud;
    private readonly ObjectView _objects;
    private readonly EffectsView _effects;
    private readonly RopeView _ropes;
    private readonly Dictionary<string, ObjectView.Look> _looks = [];
    /// <summary>The HUD's inventory slots, filled again each time (kept: no list per frame).</summary>
    private readonly List<(int Item, int Count)> _hudSlots = [];
    private readonly SniperView _sniper;
    private readonly SniperTest _sniperTest;
    private readonly RideTest _rideTest;
    private readonly SoakTest? _soak;
    /// <summary>F2's name prompt, while it's open.</summary>
    private SavePrompt? _snapshotPrompt;
    private readonly FollowCamera _follow;
    private readonly CutsceneCamera _cutscene = new();
    private readonly FreeCamera _fly;
    private readonly PauseMenu _pause;
    /// <summary>The 1996 demo, in its levels.</summary>
    private readonly BetaDemo? _beta;
    private readonly bool _test;
    /// <summary>Frames between the steps (not the tests': their frames are steps).</summary>
    private readonly FrameBlend _blend;
    private readonly Blending _blending;
    private bool _flying;
    private float _time;
    private float _pending;
    private float _strikeTime;
    /// <summary>Frames drawn since --frames' wait.</summary>
    private int _frameStep;
    private readonly Cheats _cheats;
    private Event _next = Event.None;
    /// <summary>The level ended: the event that follows once the delay is over.</summary>
    private (Event Event, float Delay)? _ending;
    private readonly LevelCommands _commands;
    /// <summary>The enhanced look's light; null in the original look.</summary>
    private readonly LevelLight? _light;
    /// <summary>What the level took from the mods (the enhanced look's).</summary>
    private readonly ModReport _mods = ModReport.None;

    public Viewer(Ui ui, ViewerOptions options, GameState state)
    {
        var loading = Stopwatch.StartNew();
        _ui = ui;
        _cheats = new Cheats(ui.Settings, state);
        // The menus' wheel and mouse don't reach Kurt.
        ui.Input.ClearMouse();
        _options = options;
        _state = state;
        _renderer = ui.Renderer;
        _audio = ui.Audio;
        var data = ui.Data;
        _beta = BetaDemo.IsBeta(options.Level) ? ui.Beta ?? throw new InvalidOperationException("The 1996 demo wasn't found") : null;
        _level = _beta != null ? new LevelData(_beta, options.Level) : new LevelData(data, options.Level);
        var level = _level;
        var renderer = _renderer;
        var groups = new TriangleGroups();
        var graphics = ui.Settings.Graphics;
        MdkObject.ForgetPoses();
        // The enhanced look's mods (HD textures among them); the original look stays as made.
        ui.ScanMods();
        var mods = graphics == Graphics.Enhanced ? ui.ModImages(level.Number) : null;
        var surfaces = new MaterialResolver(renderer, level.Dti, EnhancedLook.Surfaces(graphics), mods);
        Preload(surfaces, level);
        _view = new LevelView(renderer, level, groups, EnhancedLook.Surfaces(graphics), surfaces);
        var bank = _beta != null ? SoundBank.ForBeta(_beta, options.Level) : SoundBank.ForLevel(data, options.Level, level.Arenas);
        bank.Preload();
        _mixer = new SoundMixer(_audio, bank.Get);
        var cmi = _beta != null
            ? _beta.LoadCmi(BetaDemo.LevelOf(options.Level))
            : Cmi.Load(data.PathOf($"TRAVERSE/LEVEL{level.Number}/LEVEL{level.Number}.CMI"));
        _music = new LevelMusic(_mixer, cmi.ArenaMusic) { Ambience = cmi.ArenaAmbience };
        renderer.Panorama = CreatePanorama(renderer, level.Dti) with { Sampling = EnhancedLook.Sky(graphics) };
        renderer.Lighting = null;
        _light = graphics == Graphics.Enhanced ? new LevelLight(renderer, level) : null;

        _space = new ArenaSpace();
        foreach (var arena in level.Arenas.Where(a => level.IsReachable(a.Name)))
        {
            _space.Add(arena);
        }

        // The 1996 demo's levels show its Kurt and its health display.
        var sprites = Bni.Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        _beta?.AddSprites(sprites);
        _sprite = new KurtSprite(renderer, sprites, level.Dti.Palette, EnhancedLook.Sprites(graphics), mods);
        foreach (var name in KurtSprite.LevelAnimations)
        {
            _sprite.Add(bank.Animation(name));
        }

        _kurt = new Kurt.Kurt(_space, _mixer, _sprite.FrameCount)
        {
            Feet = options.Position ?? level.Dti.StartPosition + new Vector3(0f, 0f, StartDrop),
            Yaw = options.Yaw ?? level.Dti.StartAngle,
            BetaMoves = _beta != null,
        };

        // The level starts in Kurt's arena, its music at once (0x41ba68: BSPShow(Kurt's)): the
        // DTI's start arena, or where a test put him.
        var start = level.Dti.Arenas[level.Dti.StartArena].Name;
        _music.Enter(options.Position is { } at ? _space.ArenaAt(at) ?? start : start);

        // The difficulty decides the town's timer: before the scripts.
        _kurt.Inventory.Difficulty = ui.Settings.Difficulty;
        _scripts = new ScriptRuntime(level, cmi, sprites, _space, groups, _mixer, _kurt, options.Soak)
        {
            Option = ui.Settings.Gore ? 1 : 0,
        };
        _scripts.AirStrike.UsedUp = state.StrikeUsed;
        _follow = new FollowCamera(_space) { ClipView = _scripts.ClipView, Look = options.Look };
        _scripts.ShakeRaised += _follow.RaiseShake;
        _kurt.Died += OnDied;
        _scripts.LevelEnded += over => _ending = over == ScriptRuntime.GameOver.Yes ? (Event.GameOver, GameOverDelay) : (Event.LevelEnded, EndDelay);
        _scripts.GameFinished += () => _next = Event.GameFinished;

        // The HUD; pickups show their names (0x46c448).
        _hud = new HudView(renderer, sprites, level.Dti.Palette, ui.Fti, mods);
        _scripts.Messages = _hud.Messages;
        CarryFromFall(state);
        RestoreSnapshot(state);
        ShowNotice(state);
        _kurt.Inventory.PickedUp += name => _hud.Messages.Push(name, Messages.FlagZoom, PickupMessageSeconds);

        foreach (var pickup in options.Give)
        {
            _kurt.Collect(pickup);
        }
        _scripts.ArenaEntered += _view.Enter;
        // The mods' models (the enhanced look's); collisions keep the originals.
        var swaps = mods != null ? ModModels.Load(ui.Mods.Files(ui.Settings), level.Number, _scripts.LevelModels().Concat(level.Arenas.SelectMany(a => a.Models.Values))) : null;
        _objects = new ObjectView(renderer, surfaces) { Swaps = swaps };
        _scripts.PreparePoses();
        PreloadModels(level);

        _effects = new EffectsView(renderer, surfaces, level, EnhancedLook.Sprites(graphics));
        _scripts.TextureStamped += surfaces.Refresh;
        _ropes = new RopeView(renderer, level);
        _sniper = new SniperView(renderer, _objects, level, _hud);
        _sniperTest = new SniperTest(options);
        _rideTest = new RideTest(options);
        _soak = options.Soak != null ? new SoakTest(level, _space, options.Route, options.Wait) : null;
        _fly = new FreeCamera { Pitch = options.Pitch };
        _flying = options.Fly;
        _pause = new PauseMenu(ui);
        Console.WriteLine($"Level {options.Level}: {level.Arenas.Count} arenas, {_view.TriangleCount} triangles, {_view.OutlineCount} outlines, loaded in {loading.ElapsedMilliseconds} ms");
        var enabled = ui.Mods.Enabled(ui.Settings).Count;
        if (mods != null && enabled > 0)
        {
            _mods = new ModReport(enabled, mods.Found, swaps?.Found ?? []);
            Console.WriteLine($"Mods: {_mods.Summary}; textures {surfaces.ModFound} of {surfaces.ModLooked}, colour textures {renderer.ColourBytes >> 20} MB");
        }

        _test = options.Screenshot != null || options.Frames != null;
        _blending = _test && options.FrameRate == null ? Blending.Off : Blending.On;
        _blend = new FrameBlend(_blending);
        ui.Window.CaptureMouse(_test ? Capture.Off : Capture.On);
        _commands = new LevelCommands(level, _kurt, _scripts, _space, SaveSlot) { Mods = _mods };

        // The session's cheats hold in every level.
        _commands.SetGod(ui.Dev.God);
        _commands.SetNoclip(ui.Dev.Noclip);
        _commands.SetOneHit(ui.Dev.OneHit);
        EnterStartArena(state);
    }

    /// <summary>The console's map: Kurt starts on a floor of the arena asked for.</summary>
    private void EnterStartArena(GameState state)
    {
        if (state.StartArena is not { } arena)
        {
            return;
        }

        state.StartArena = null;
        if (ArenaStops.Find(_level, _space, arena) is not { } stop)
        {
            Console.WriteLine($"No arena {arena}");
            return;
        }

        _scripts.TeleportKurt(arena, stop, _kurt.Yaw);
    }

    /// <summary>The console's save: a full save of that name, as F2 makes.</summary>
    private bool SaveSlot(string name) => SaveGames.In(_ui.UserFolder).Write(name, SnapshotSave(_scripts.Capture()));

    /// <summary>The console's commands on the level.</summary>
    public ILevelTarget Commands => _commands;

    /// <summary>The tests' --console runs after the delay, once the scripts have set Kurt's arena.</summary>
    public bool ConsoleReady => _time >= _options.Delay && _scripts.CurrentArena.Length != 0;

    /// <summary>The debug overlay's lines: the pause menu or F2's prompt, Kurt and the level.</summary>
    public IReadOnlyList<string> Status
    {
        get
        {
            IReadOnlyList<string> level = [.. OverlayText.Level(_commands.Where(), _scripts.Objects.Count, _kurt.Current.ToString())];
            if (_mods.Mods > 0)
            {
                level = [.. level, _mods.Summary];
            }

            if (_pause.Open)
            {
                return ["pause menu", .. level];
            }

            return _snapshotPrompt != null ? ["save prompt", .. level] : level;
        }
    }

    /// <summary>The counts of the level (for the statistics).</summary>
    public GameStats Stats => _scripts.Stats;

    /// <summary>Kurt's health when the level ended (the save after LEVEL8 keeps it).</summary>
    public int Health => _kurt.Health;

    /// <summary>Kurt keeps the health and the pickups of the fall (taken again, without their messages).</summary>
    private void CarryFromFall(GameState state)
    {
        if (state.Carry is not { } carry)
        {
            return;
        }

        state.Carry = null;
        foreach (var pickup in carry.Pickups)
        {
            _kurt.Collect(pickup);
        }

        _kurt.SetHealth(carry.Health);
        Console.WriteLine($"Level starts with the fall's health {carry.Health} and pickups {string.Join(",", carry.Pickups)}");
    }

    public Event Frame(float elapsed, string? screenshot)
    {
        var input = _ui.Input;

        // While the game waits, the mouse and the wheel don't pile up for Kurt.
        if (_snapshotPrompt != null || _pause.Open)
        {
            input.ClearMouse();
        }

        // F2's name prompt: the game waits under it.
        if (_snapshotPrompt != null)
        {
            return AskSnapshot(elapsed, screenshot);
        }

        // The pause menu: the game waits under it.
        if (_pause.Open)
        {
            DrawScene(elapsed);
            var paused = _pause.Update(elapsed);
            _renderer.Present(SceneView(), Background(), screenshot);
            CaptureMouse();
            return paused;
        }

        if (input.WasPressed(MenuKey.Back) && _scripts.Strike is not { Active: true })
        {
            _pause.Show();
            _ui.Window.CaptureMouse(Capture.Off);
            return Event.None;
        }

        // The full-screen strike: the game waits (Esc skips it); tests count its time.
        var skip = input.WasPressed(Key.Escape) ? SniperView.StrikeSkip.Now : SniperView.StrikeSkip.No;
        if (_sniper.DrawStrike(_scripts, skip, elapsed) is { } strikeView)
        {
            _strikeTime += elapsed;
            var shot = SavePath(_options, input, _time + _strikeTime) ?? screenshot;
            _renderer.Present(strikeView, Background(), shot);
            if (shot != null && _test)
            {
                Console.WriteLine($"Saved {shot} (strike)");
                return Event.Quit;
            }

            return Event.None;
        }

        if (input.WasPressed(Key.AlwaysRun))
        {
            input.ToggleRun();
        }

        if (input.WasPressed(Key.Fly))
        {
            _flying = !_flying;
            _fly.Position = _follow.Position;
            _fly.Yaw = _kurt.Yaw;
        }

        // Tests ignore the real mouse.
        if (_test)
        {
            input.ClearMouse();
        }

        TypeCheats(input.Typed);
        if (input.Digit != Input.NoDigit && input.IsDown(Key.Teleport))
        {
            BetaTeleport(input.Digit);
        }

        if (input.WasPressed(Key.QuickSave))
        {
            OpenSnapshot();
        }

        if (input.WasPressed(Key.QuickLoad))
        {
            QuickLoad();
        }

        MouseLook(input);
        RunSteps(input, elapsed);

        if (_ending is { } ending)
        {
            _ending = ending with { Delay = ending.Delay - elapsed };
            if (_ending.Value.Delay <= 0f)
            {
                _next = ending.Event;
            }
        }

        var camera = DrawScene(elapsed);
        if (_options.Trace)
        {
            Trace(camera.Position);
        }

        var save = SavePath(_options, input, _time) ?? FramePath() ?? screenshot;
        using (_ui.Dev.Profiler.Measure(Section.Render))
        {
            _renderer.Present(camera, Background(), save);
        }

        if (save == null)
        {
            return _next;
        }

        Console.WriteLine($"Saved {save}");
        if (!_test || _options.Frames is { } frames && !frames.IsLast(_frameStep - 1))
        {
            return _next;
        }

        if (_options.Snapshot is { } name)
        {
            SaveSnapshot(name);
        }

        Report(_kurt, _space);
        _soak?.Report(_scripts, _kurt, _time);
        return Event.Quit;
    }

    /// <summary>Kurt in fixed steps; the tests' keys by game time.</summary>
    private void RunSteps(Input input, float elapsed)
    {
        var options = _options;
        _pending = MathF.Min(_pending + elapsed, Step * MaxSteps);
        while (_pending >= Step)
        {
            _pending -= Step;
            _time += Step;
            if (_soak == null)
            {
                HoldTestKeys(input);
            }

            RunTests();
            _sniperTest.Step(_kurt, _scripts, input, _time);
            _rideTest.Step(_kurt, _scripts, input, _time);
            UpdateKurt(input);
            using (_ui.Dev.Profiler.Measure(Section.Scripts))
            {
                _scripts.Update(Step);
            }

            // The camera follows Kurt where his platform carried him (game_frame: objects, then
            // camera); before, it lagged a step behind on each tick: a ghost.
            UpdateCamera(input);
            input.ClearMouse();
            _blend.Record(_kurt, Carrier(), Driven(), _scripts.TickCount);

            // The music follows Kurt's arena, which the scripts set (BSPShow).
            using (_ui.Dev.Profiler.Measure(Section.Audio))
            {
                _music.Enter(_scripts.CurrentArena);
                _music.Update(Step);
            }

            if (_level.ArenaNamed(_scripts.CurrentArena) is { } arena)
            {
                _hud.Enter(_level.PaletteOf(arena));
            }

            if (CutsceneCamera.Active(_scripts))
            {
                _cutscene.Update(_scripts);
            }

            _space.SetSolid(_scripts.SolidArenas);
            _soak?.Step(_kurt, _scripts, _space, _time);
            if (MathF.Floor(_time * Kurt.Kurt.Ticks) > MathF.Floor((_time - Step) * Kurt.Kurt.Ticks))
            {
                _hud.Tick(HudStateOf(_kurt, _scripts, _hudSlots));
            }

            if (options.Profile && MathF.Floor(_time) > MathF.Floor(_time - Step))
            {
                Profile(_scripts, _time);
                Console.WriteLine($"  sky {_scripts.SkyMode} ({_renderer.Backdrop})");
                Console.WriteLine($"  music {_music.Playing} {Decibels(_music.Volume)}dB, fading {_music.Fading ?? "-"}");
            }
        }
    }

    /// <summary>A step of Kurt, or of the flying camera (timed as physics).</summary>
    private void UpdateKurt(Input input)
    {
        using var measure = _ui.Dev.Profiler.Measure(Section.Physics);
        if (_flying)
        {
            _fly.Update(input, Step);
            return;
        }

        _kurt.Update(input, Step);
    }

    /// <summary>A step of Kurt's camera, unless the flying one is on (timed as physics).</summary>
    private void UpdateCamera(Input input)
    {
        using var measure = _ui.Dev.Profiler.Measure(Section.Physics);
        if (_flying)
        {
            return;
        }

        _follow.Update(_kurt, PitchGoal(), input, Step);
    }

    /// <summary>The mouse turns Kurt and tilts the camera every frame, not with the steps (screens
    /// above 60 Hz); not the flying camera's, nor in tests (they have no mouse).</summary>
    private void MouseLook(Input input)
    {
        if (_flying || _test)
        {
            return;
        }

        _blend.Turn(_kurt.MouseLook(input));
        _follow.MouseLook(_kurt, input);
    }

    /// <summary>What carries Kurt, drawn with him: the XE he flies, the platform he stands on.</summary>
    private MdkObject? Carrier()
    {
        var rides = _scripts.Rides;
        if (rides.Bomber != null)
        {
            return rides.Ridden;
        }

        return _kurt.OnFloor ? _kurt.Platform as MdkObject : null;
    }

    /// <summary>What Kurt rides that follows him each step: the XD2, the board.</summary>
    private MdkObject? Driven() => _scripts.Rides.Bomber == null ? _scripts.Rides.Ridden : null;

    /// <summary>This frame between the last step and the next: Kurt, the objects and the cameras
    /// as drawn.</summary>
    private void BlendFrame()
    {
        var step = _pending / Step;
        var tick = _scripts.TickBlend(_pending);
        _blend.Update(_kurt, step, tick);
        if (_blending == Blending.Off || _flying)
        {
            return;
        }

        if (CutsceneCamera.Active(_scripts))
        {
            _cutscene.Blend(tick);
        }

        _follow.Blend(_kurt, _blend.Feet, _blend.Yaw, step);
    }

    /// <summary>The tests' held keys: forward for --walk seconds, jump, fire, and use at 1 second.</summary>
    private void HoldTestKeys(Input input)
    {
        var options = _options;
        var started = _time >= options.Delay;
        input.Hold(Key.Forward, started && _time <= options.Delay + options.Walk ? Input.State.Down : Input.State.Up);
        input.Hold(Key.Jump, started && options.Jump ? Input.State.Down : Input.State.Up);
        input.Hold(Key.Fire, started && options.Fire ? Input.State.Down : Input.State.Up);
        input.Hold(Key.UseItem, options.Use && _time >= UseStart && _time <= UseStart + UseTime ? Input.State.Down : Input.State.Up);
        if (options.Roll is { } roll)
        {
            input.Hold(roll == BetaRoll.Left ? Key.RollLeft : Key.RollRight, started ? Input.State.Down : Input.State.Up);
        }

        if (options.BetaTeleport is { } teleport && started && _time - Step < options.Delay)
        {
            BetaTeleport(teleport);
        }
    }

    /// <summary>The 1996 demo's teleports (<see cref="BetaDemo.LoadTeleports"/>), on a digit while T or
    /// Alt is held (the digits alone pick items): the lines of this level's arenas, in order. The city
    /// has no other way up to the top of <c>ARENA_4</c>.</summary>
    private void BetaTeleport(int index)
    {
        if (_beta == null)
        {
            return;
        }

        var teleports = _beta.LoadTeleports().Where(t => _level.Arenas.Any(a => a.Name == t.Arena)).ToList();
        if (index >= teleports.Count)
        {
            return;
        }

        var (arena, position) = teleports[index];
        _scripts.TeleportKurt(arena, position, _kurt.Yaw);
        Console.WriteLine(string.Create(CultureInfo.InvariantCulture, $"Teleport {index}: {arena} {position.X} {position.Y} {position.Z}"));
    }

    /// <summary>The tests' --die (Kurt is hurt to death) and --event (a special_event: 1 ends the level).</summary>
    private void RunTests()
    {
        if (_time < TestStart || _time - Step >= TestStart)
        {
            return;
        }

        if (_options.Die)
        {
            _kurt.Hurt(FatalDamage);
        }

        if (_options.Event is { } value)
        {
            _scripts.SpecialEvent(_scripts.GetArenaState(_scripts.CurrentArena).Controller, value);
        }
    }

    /// <summary>The camera, the scene, the HUD and the sounds' listener.</summary>
    private View DrawScene(float elapsed)
    {
        BlendFrame();
        var camera = SceneView();
        var eye = Eye();
        _mixer.ListenerPosition = camera.Position;
        _mixer.ListenerRight = eye.Right;
        // In sniper mode the sounds are heard through the scope.
        _mixer.ScopeZoom = _kurt.Sniping && !_flying ? _kurt.Scope.Zoom : 0f;
        _mixer.ListenerForward = _kurt.SniperForward;
        _mixer.ListenerUp = FollowCamera.UpOf(_kurt.SniperForward);
        using (_ui.Dev.Profiler.Measure(Section.Audio))
        {
            _mixer.Update(elapsed);
        }

        _scripts.Eye = camera.Position;
        _light?.Update(_scripts, _kurt);
        using var render = _ui.Dev.Profiler.Measure(Section.Scene);
        _view.Draw(_scripts.DrawnArenas, _scripts.AnimatedTextures);
        _view.DrawEnd(_scripts.EndLevel);
        DrawObjects(_objects, _scripts, _level, _looks, _blend);
        DrawBoard(camera.Position, eye.Forward);
        _ropes.Draw(_scripts.Objects, _blend);
        _sniper.Draw(_kurt, _scripts, _blend, elapsed);
        _effects.Draw(_scripts, camera, _blend.Tick);
        _sprite.Draw(_kurt, _blend.Feet, camera.Position, eye.Forward, eye.Up, eye.FieldOfView, _space);

        _hud.Messages.Update(elapsed);
        _hud.Draw(HudStateOf(_kurt, _scripts, _hudSlots));
        return camera;
    }

    private View SceneView()
    {
        var aspect = _renderer.AspectRatio;
        if (_flying)
        {
            return _fly.View(aspect);
        }

        if (_kurt.Sniping)
        {
            return _blending == Blending.Off ? FollowCamera.SniperView(_kurt, aspect) : ScopeView(aspect);
        }

        return CutsceneCamera.Active(_scripts) ? _cutscene.View(aspect) : _follow.View(aspect);
    }

    /// <summary>Sniper mode's view as drawn: from Kurt between steps, the scope's tilt and zoom too.</summary>
    private View ScopeView(float aspect)
    {
        var (yaw, pitch) = (_blend.Yaw, _blend.ScopePitch);
        var eye = Scope.Eye(_blend.Feet, Scope.Direction(yaw, 0f), pitch);
        return FollowCamera.SniperView(eye, Scope.Direction(yaw, pitch), _blend.Zoom, aspect);
    }

    /// <summary>The camera's axes and field of view outside sniper mode: flying, a cutscene's or Kurt's.</summary>
    private (Vector3 Forward, Vector3 Up, Vector3 Right, float FieldOfView) Eye()
    {
        if (_flying)
        {
            return (_fly.Forward, Vector3.UnitZ, _fly.Right, FreeCamera.FieldOfView);
        }

        return CutsceneCamera.Active(_scripts)
            ? (_cutscene.Forward, _cutscene.Up, _cutscene.Right, FollowCamera.FieldOfView)
            : (_follow.Forward, _follow.Up, _follow.Right, FollowCamera.FieldOfView);
    }

    /// <summary>The pitch the follow camera eases to: camera_track's (opcode 203) while a script
    /// asks for it, else the arena's.</summary>
    private float PitchGoal() =>
        _scripts.CameraTrackTicks > 0 ? _scripts.CameraTrackPitch : ArenaPitch(_scripts.CurrentArena);

    /// <summary>A full save being loaded: the level and Kurt as they were.</summary>
    private void RestoreSnapshot(GameState state)
    {
        if (state.Snapshot is not { } json)
        {
            return;
        }

        state.Snapshot = null;
        _scripts.Load(json);
        Console.WriteLine($"restored hash {Snapshot.Hash(_scripts.Capture())}, objects {_scripts.Objects.Count}");
    }

    /// <summary>F2 (0x42b520(0)): the game stops and the name is asked, the level's number offered.</summary>
    private void OpenSnapshot()
    {
        if (!_scripts.CanSnapshot() || _kurt.Sniping)
        {
            Notify("Can't save now");
            return;
        }

        var name = (GameState.IndexOf(_level.Number) + 1).ToString(CultureInfo.InvariantCulture);
        var json = _scripts.Capture();
        Console.WriteLine($"snapshot hash {Snapshot.Hash(json)}, objects {_scripts.Objects.Count}");
        _snapshotPrompt = new SavePrompt(_ui, new Fonts(_renderer, _ui.Fti, mods: _ui.CanvasMods()), SaveGames.In(_ui.UserFolder), SnapshotSave(json), name);
        _ui.Window.CaptureMouse(Capture.Off);
    }

    /// <summary>A frame of F2's prompt (on black); Enter saves the level as it was, Esc gives up.</summary>
    private Event AskSnapshot(float elapsed, string? screenshot)
    {
        _ui.View.Layout(ScreenView.Fit.Inside);
        _snapshotPrompt!.Update(_ui.Input, elapsed);
        _snapshotPrompt.Draw();
        _ui.Present(screenshot);
        if (!_snapshotPrompt.Closed)
        {
            return Event.None;
        }

        if (_snapshotPrompt.Saved)
        {
            _state.QuickSlot = _snapshotPrompt.Name;
            Notify("Game saved");
        }

        _snapshotPrompt = null;
        CaptureMouse();
        return Event.None;
    }

    /// <summary>Quick load: this session's last full save, else the newest save; the level
    /// reloads and says so (<see cref="GameState.Notice"/>).</summary>
    private void QuickLoad()
    {
        var saves = SaveGames.In(_ui.UserFolder);
        if (saves.QuickSlot(_state.QuickSlot) is not { } name || saves.Read(name) is not { } save)
        {
            Notify("Nothing to load");
            return;
        }

        _state.Load(save);
        _state.Notice = "Game loaded";
        Console.WriteLine($"Loaded game {name}: level {save.Level}");
        _next = save.Kind == SaveKind.BeforeLevel ? Event.Briefing : Event.Play;
    }

    /// <summary>A message of the port's own on the HUD (zooming, ahead of the others), also printed.</summary>
    private void Notify(string text)
    {
        _hud.Messages.PushText(Encoding.ASCII.GetBytes(text), Messages.FlagZoom | Messages.FlagFront, NoticeSeconds);
        Console.WriteLine(text);
    }

    /// <summary>The message the last level left for this one ("Game loaded").</summary>
    private void ShowNotice(GameState state)
    {
        if (state.Notice is not { } notice)
        {
            return;
        }

        state.Notice = null;
        Notify(notice);
    }

    /// <summary>The tests' full save (--snapshot): written at once, its hash printed.</summary>
    private void SaveSnapshot(string name)
    {
        var json = _scripts.Capture();
        SaveGames.In(_ui.UserFolder).Write(name, SnapshotSave(json));
        Console.WriteLine($"snapshot hash {Snapshot.Hash(json)}, objects {_scripts.Objects.Count}");
    }

    private SaveGame SnapshotSave(string json)
    {
        _state.Level = _level.Number;
        return _state.Save(SaveKind.Snapshot) with { Health = _kurt.Health, State = json };
    }

    /// <summary>The mouse looks around again once the pause menu closes.</summary>
    private void CaptureMouse()
    {
        if (!_pause.Open)
        {
            _ui.Window.CaptureMouse(_test ? Capture.Off : Capture.On);
        }
    }

    /// <summary>Letters typed in a level: <c>TOOSCARYFORME</c> turns gore on or off (not saved),
    /// <c>SEETHEWHOLEGAME</c> the main menu's debug keys (0x5742bc).</summary>
    private void TypeCheats(string typed)
    {
        if (typed.Length == 0)
        {
            return;
        }

        foreach (var c in typed.ToUpperInvariant().Where(char.IsAsciiLetterUpper))
        {
            var cheat = _cheats.Type(c);
            if (cheat == Cheats.Cheat.Gore)
            {
                _scripts.Option = _ui.Settings.Gore ? 1 : 0;
                Console.WriteLine($"Gore {(_ui.Settings.Gore ? "on" : "off")}");
            }
            else if (cheat == Cheats.Cheat.DebugKeys)
            {
                Console.WriteLine($"Debug keys {(_state.DebugKeys ? "on" : "off")}");
            }
        }
    }

    /// <summary>Kurt died (damp_control 0x466b40): the death is counted and the game goes back to the
    /// main menu, whose "Continue" starts the level again.</summary>
    private void OnDied()
    {
        Console.WriteLine("Kurt died");
        _state.Level = _level.Number;
        _state.Deaths++;
        _state.StrikeUsed = _scripts.AirStrike.UsedUp;
        _next = Event.KurtDied;
    }

    public void Dispose()
    {
        _audio.StopAll();
        _renderer.Backdrop = Backdrop.Sky;
        _ui.Window.CaptureMouse(Capture.Off);
    }

    /// <summary>What the HUD shows of Kurt and the object he shoots at.</summary>
    private static HudState HudStateOf(Kurt.Kurt kurt, ScriptRuntime scripts, List<(int Item, int Count)> slots)
    {
        var inventory = kurt.Inventory;
        slots.Clear();
        for (var i = 0; i < inventory.Slots.Count; i++)
        {
            slots.Add(((int)inventory.Slots[i].Item, inventory.Slots[i].Count));
        }

        var (barHealth, barMax) = scripts.GetBar();
        return new HudState(kurt.Health, kurt.HurtFlash, kurt.WhiteFlash, kurt.Current == Kurt.Kurt.State.Dead,
            slots, inventory.Selected, inventory.SuperChainGun, barHealth, barMax, scripts.Rides.Bomber?.Shown, scripts.TownTimeLeft);
    }

    /// <summary>Lays out the models before they're drawn: each arena's own, and those of the objects
    /// there at the start. (Every level model in every arena would make the enhanced look's colour
    /// textures for every palette: hundreds of MB.)</summary>
    private void PreloadModels(LevelData level)
    {
        foreach (var arena in level.Arenas)
        {
            foreach (var model in arena.Models.Values)
            {
                _objects.Preload(model, ObjectLook(level, _looks, arena.Name));
            }
        }

        foreach (var obj in _scripts.Objects)
        {
            if (obj.Model != null)
            {
                _objects.Preload(obj.Model, ObjectLook(level, _looks, obj.Arena));
            }
        }
    }

    /// <summary>Every texture of the level with each arena's palette (and the level's), on the GPU.</summary>
    private static void Preload(MaterialResolver surfaces, LevelData level)
    {
        surfaces.Preload(level.Dti.Palette, [level.LevelTextures]);
        foreach (var arena in level.Arenas)
        {
            surfaces.Preload(level.PaletteOf(arena), level.ArchivesOf(arena));
        }
    }

    /// <summary>The visible objects, each with its arena's palette and textures.</summary>
    private static void DrawObjects(ObjectView view, ScriptRuntime scripts, LevelData level, Dictionary<string, ObjectView.Look> looks, FrameBlend blend)
    {
        foreach (var obj in scripts.Objects)
        {
            if (!obj.Visible || IsBoard(scripts, obj))
            {
                continue;
            }

            view.Draw(obj, ObjectLook(level, looks, obj.Arena), blend.TransformOf(obj), blend.PoseOf(obj));
        }
    }

    private static bool IsBoard(ScriptRuntime scripts, MdkObject obj) => scripts.Rides.OnBoard() && obj == scripts.Rides.Ridden;

    /// <summary>The ridden board, in front of the floors it stands above (<see cref="FloorPull"/>).</summary>
    private void DrawBoard(Vector3 eye, Vector3 forward)
    {
        if (_scripts.Rides.Ridden is not { Visible: true } board || !IsBoard(_scripts, board))
        {
            return;
        }

        var transform = FloorPull.Of(board, _blend.TransformOf(board), eye, forward, _space);
        _objects.Draw(board, ObjectLook(_level, _looks, board.Arena), transform, _blend.PoseOf(board));
    }

    /// <summary>An arena's palette and textures for its objects (the level's outside the arenas).</summary>
    private static ObjectView.Look ObjectLook(LevelData level, Dictionary<string, ObjectView.Look> looks, string name)
    {
        if (looks.TryGetValue(name, out var look))
        {
            return look;
        }

        var arena = level.ArenaNamed(name);
        return looks[name] = arena != null
            ? new ObjectView.Look(level.PaletteOf(arena), level.ArchivesOf(arena))
            : new ObjectView.Look(level.Dti.Palette, [level.LevelTextures]);
    }

    /// <summary>For tests: the objects of Kurt's arena (like the Godot port's --profile).</summary>
    private static void Profile(ScriptRuntime scripts, float time)
    {
        var second = scripts.SecondArena + (scripts.SecondActive ? " (active)" : "");
        Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
            $"Profile {time:0}s: objects {scripts.Objects.Count}, arena {scripts.CurrentArena}, second {second}"));
        Console.WriteLine($"  effects {scripts.Effects.All.Count}, debris pieces {scripts.Debris.PieceCount()}, fans {scripts.Fans.Count}");
        foreach (var obj in scripts.Objects.Where(o => o.Arena == scripts.CurrentArena))
        {
            var p = obj.Position;
            var door = (obj.Flags & MdkObject.FlagDoor) != 0 ? $" door {obj.DoorState:x}" : "";
            Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
                $"  {obj.TypeName}_{obj.InstanceId} {obj.Arena} ({Rounded(p.X)}, {Rounded(p.Y)}, {Rounded(p.Z)}) yaw {(int)obj.Yaw} " +
                $"move {obj.MoveCommand} path {obj.Path} anim {obj.Animation?.Name ?? "-"} frame {obj.AnimationFrame} " +
                $"speed {obj.Speed:0.0} health {obj.Health} flags {obj.Flags:x}{door}"));
        }

        if (CutsceneCamera.Active(scripts))
        {
            var c = scripts.CameraPoint;
            Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
                $"  cutscene {scripts.Cutscene:x} shot {scripts.CameraMode} camera ({Rounded(c.X)}, {Rounded(c.Y)}, {Rounded(c.Z)}) yaw {scripts.CameraYaw:0} pitch {scripts.CameraPitch:0}"));
        }

        if (scripts.Items.Twisters.Count != 0)
        {
            Console.WriteLine($"  twisters {scripts.Items.Twisters.Count}, ribbon triangles {scripts.Items.Twisters.Sum(t => t.Ribbon.Triangles().Count / 3)}");
        }

        foreach (var obj in scripts.Objects.Where(o => o.RopeMask != 0 && !o.Dead))
        {
            Console.WriteLine($"  rope {obj.TypeName}_{obj.InstanceId} lines {RopeView.LinesOf(obj).Count / 2} colour {obj.RopeColor}");
        }

        foreach (var ((arena, texture), frame) in scripts.AnimatedTextures.Frames)
        {
            Console.WriteLine(string.Create(CultureInfo.InvariantCulture, $"  texture {arena} {texture} frame {frame:0.00}"));
        }

        RideTest.Report(scripts);
        if (scripts.EndLevel is { } end)
        {
            Console.WriteLine(end.Describe());
        }

        if (scripts.Vm.Unimplemented.Count != 0)
        {
            Console.WriteLine($"  unimplemented opcodes: {string.Join(", ", scripts.Vm.Unimplemented.Select(u => $"{u.Key}x{u.Value}"))}");
        }
    }

    /// <summary>For tests: where this frame draws Kurt, the camera and the platform or ride carrying him.</summary>
    private void Trace(Vector3 camera)
    {
        var carrier = _scripts.Rides.Ridden ?? _kurt.Platform as MdkObject;
        var at = carrier is { } drawn ? _blend.PositionOf(drawn) : Vector3.Zero;
        var on = carrier is { } c
            ? string.Create(CultureInfo.InvariantCulture, $" on {c.TypeName} {at.X:0.00} {at.Y:0.00} {at.Z:0.00}")
            : "";
        var f = _blend.Feet;
        Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
            $"trace {_time:0.000} kurt {f.X:0.00} {f.Y:0.00} {f.Z:0.00} camera {camera.X:0.00} {camera.Y:0.00} {camera.Z:0.00}{on}"));
        TracePoses();
        TraceEffects();
        TraceScope();
    }

    /// <summary>For tests: in sniper mode, the scope and the round cameras as drawn ("blend scope t
    /// scope zoom pitch clip", "blend round t slot x y z").</summary>
    private void TraceScope()
    {
        if (!_kurt.Sniping)
        {
            return;
        }

        Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
            $"blend scope {_time:0.000} scope {_blend.Zoom:0.0000} {_blend.ScopePitch:0.000} {_blend.ClipTime:0.0000}"));
        for (var i = 0; i < SniperRounds.Slots; i++)
        {
            var camera = _scripts.SniperRounds.Camera(i, _blend.Tick);
            if (camera.Shot != SniperRounds.Shot.Watching)
            {
                continue;
            }

            var p = camera.Position;
            Console.WriteLine(string.Create(CultureInfo.InvariantCulture, $"blend round {_time:0.000} {i} {p.X:0.00} {p.Y:0.00} {p.Z:0.00}"));
        }
    }

    /// <summary>For tests: each flying piece and sprite effect as drawn ("blend piece t id x y z",
    /// "blend effect t id x y z").</summary>
    private void TraceEffects()
    {
        foreach (var piece in _scripts.Debris.Pieces)
        {
            var c = piece.CenterAt(_blend.Tick);
            Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
                $"blend piece {_time:0.000} {RuntimeHelpers.GetHashCode(piece)} {c.X:0.000} {c.Y:0.000} {c.Z:0.000}"));
        }

        foreach (var effect in _scripts.Effects.All)
        {
            var p = effect.PositionAt(_blend.Tick);
            Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
                $"blend effect {_time:0.000} {RuntimeHelpers.GetHashCode(effect)} {p.X:0.000} {p.Y:0.000} {p.Z:0.000}"));
        }
    }

    /// <summary>For tests: the middle of each animated object's pose in Kurt's arena as drawn
    /// ("blend pose t TYPE_ID x y z", model space).</summary>
    private void TracePoses()
    {
        foreach (var obj in _scripts.Objects)
        {
            if (obj.Animation == null || obj.Dead || !obj.Visible || obj.Arena != _scripts.CurrentArena)
            {
                continue;
            }

            var sum = Vector3.Zero;
            var count = 0;
            foreach (var part in _blend.PoseOf(obj))
            {
                foreach (var vertex in part)
                {
                    sum += vertex;
                    count++;
                }
            }

            var m = count == 0 ? Vector3.Zero : sum / count;
            Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
                $"blend pose {_time:0.000} {obj.TypeName}_{obj.InstanceId} {m.X:0.000} {m.Y:0.000} {m.Z:0.000}"));
        }
    }

    /// <summary>A music volume (0-0x7FFF) in whole decibels, as the Godot port's profile prints it.</summary>
    private static int Decibels(float volume) =>
        (int)MathF.Round(20f * MathF.Log10(SoundMixer.Gain(volume)));

    /// <summary>A coordinate rounded like Godot's Vector3.round().</summary>
    private static string Rounded(float value) =>
        (MathF.Round(value, MidpointRounding.AwayFromZero) + 0f).ToString("0.0", CultureInfo.InvariantCulture);

    /// <summary>The test's screenshot once its wait is over, or F12's.</summary>
    private static string? SavePath(ViewerOptions options, Input input, float time)
    {
        if (options.Screenshot != null)
        {
            return time >= options.Wait ? options.Screenshot : null;
        }

        return input.WasPressed(Key.Screenshot) ? $"mdk-{DateTime.Now:yyyyMMdd-HHmmss}.bmp" : null;
    }

    /// <summary>--frames' file for this frame, once the wait is over.</summary>
    private string? FramePath()
    {
        if (_options.Frames is not { } frames || _time < _options.Wait)
        {
            return null;
        }

        return frames.PathOf(_frameStep++);
    }

    /// <summary>For tests: where Kurt is and what he does.</summary>
    private static void Report(Kurt.Kurt kurt, ArenaSpace space)
    {
        var f = kurt.Feet;
        Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
            $"Kurt at {f.X:0.00} {f.Y:0.00} {f.Z:0.00} yaw {kurt.Yaw:0} {kurt.Current} floor {kurt.OnFloor} arena {space.ArenaAt(f)} health {kurt.Health}"));
    }

    /// <summary>The camera pitch of Kurt's arena (DTI, degrees; camera_update 0x4174d0).</summary>
    private float ArenaPitch(string name)
    {
        const float DefaultPitch = 4f;
        if (!_pitches.TryGetValue(name, out var pitch))
        {
            pitch = _pitches[name] = _level.EntryNamed(name)?.Pitch ?? DefaultPitch;
        }

        return pitch;
    }

    /// <summary>The arenas' camera pitches, found once.</summary>
    private readonly Dictionary<string, float> _pitches = [];

    /// <summary>The level's sky and the panorama its mirrors show, through the level's palette.</summary>
    private static Panorama CreatePanorama(Renderer renderer, Dti dti)
    {
        var sky = renderer.CreateIndexTexture(dti.Sky.Width, dti.Sky.Height, dti.Sky.Indices);
        var mirrorSky = dti.MirrorSky == dti.Sky ? sky : renderer.CreateIndexTexture(dti.MirrorSky.Width, dti.MirrorSky.Height, dti.MirrorSky.Indices);
        var palette = renderer.CreatePalette(dti.Palette.Rgba);
        return new Panorama(sky, mirrorSky, palette, dti.SkyWrapWidth, dti.SkyHorizonRow, dti.SkyOffset, dti.Sky.Height,
            dti.SkyTopColor, dti.SkyBottomColor);
    }

    /// <summary>The background the scripts want (sky, black or the last frame) and its clear colour.</summary>
    private Vector4 Background()
    {
        _renderer.Backdrop = SkyModes.BackdropOf(_scripts.SkyMode);
        return SkyModes.ClearOf(_scripts.SkyMode, SkyColour(_level.Dti));
    }

    /// <summary>The colour above the sky panorama.</summary>
    private static Vector4 SkyColour(Dti dti)
    {
        var at = dti.SkyTopColor * 4;
        var rgba = dti.Palette.Rgba;
        return new Vector4(rgba[at], rgba[at + 1], rgba[at + 2], byte.MaxValue) / byte.MaxValue;
    }
}
