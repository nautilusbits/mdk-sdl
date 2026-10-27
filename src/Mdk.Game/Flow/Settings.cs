using System.Globalization;
using Mdk.Engine.Audio;
using Mdk.Engine.Platform;
using Mdk.Engine.Render;
using Mdk.Engine.Upscale;
using Mdk.Game.HdTextures;
using Mdk.Game.Kurt;
using Mdk.Game.Mods;

namespace Mdk.Game.Flow;

/// <summary>The look of the levels: the original's, or enhanced (lit, filtered, shadows, haze).</summary>
public enum Graphics { Original, Enhanced }

/// <summary>The player's settings (settings.gd): volumes, the music filter, the mouse, the window
/// and the frame (display mode, size, render scale, VSync, frame limit, GPU backend),
/// anti-aliasing, the difficulty, gore, the graphics, the mods switched off and the key bindings (one key or mouse button per action, by name). Saved as
/// <c>name=value</c> lines in the user's folder; applied at start and whenever they change.
/// <code>
///   master_volume=80
///   bind.Forward=W
///   mod.hd-textures=Off
///   touch.Fire=Right,0.0113,Bottom,0.2,9.8,9.8
/// </code></summary>
public sealed class Settings
{
    public const int MaxVolume = 100;
    private const string FileName = "settings.cfg";
    private const string BindPrefix = "bind.";
    private const string PadPrefix = "pad.";
    private const string ModPrefix = "mod.";
    private const string TouchPrefix = "touch.";
    /// <summary>The on-screen controls' opacity (%): never quite invisible.</summary>
    public const int MinOpacity = 25;
    public const int MaxOpacity = 100;
    /// <summary>The window's size until another is chosen (the original's 640 x 480, doubled).</summary>
    public static readonly Resolution DefaultWindow = new(1280, 960);

    /// <summary>The game's actions in the order the controls screen lists them, with their names.</summary>
    public static readonly IReadOnlyList<(Key Key, string Name)> Actions =
    [
        (Key.Forward, "Forward"), (Key.Back, "Back"), (Key.TurnLeft, "Turn left"), (Key.TurnRight, "Turn right"),
        (Key.StrafeLeft, "Strafe left"), (Key.StrafeRight, "Strafe right"), (Key.Jump, "Jump"), (Key.Turbo, "Run"),
        (Key.AlwaysRun, "Always run"), (Key.Fire, "Fire"), (Key.Sniper, "Sniper mode"), (Key.ZoomIn, "Zoom in"), (Key.ZoomOut, "Zoom out"),
        (Key.UseItem, "Use item"), (Key.ItemNext, "Next item"), (Key.ItemPrevious, "Previous item"),
        (Key.QuickSave, "Quick save"), (Key.QuickLoad, "Quick load"),
    ];

    /// <summary>The actions the gamepad's buttons rebind: the sticks walk and look.</summary>
    public static readonly IReadOnlyList<(Key Key, string Name)> PadActions =
        [.. Actions.Where(a => a.Key is not (Key.Forward or Key.Back or Key.TurnLeft or Key.TurnRight or Key.StrafeLeft or Key.StrafeRight))];

    /// <summary>Volumes from 0 to 100.</summary>
    public int MasterVolume = 80;
    public int MusicVolume = 50;
    public int EffectsVolume = 70;
    public bool MusicFilter = true;
    /// <summary>Mouse sensitivity multiplier (0.25-3) and inverted vertical look.</summary>
    public float MouseSensitivity = 1f;
    public bool InvertMouse;
    /// <summary>The gamepad's look: its sensitivity (as the mouse's) and inverted vertical look.</summary>
    public float PadSensitivity = 1f;
    public bool InvertPad;
    /// <summary>The window (Options, Display): windowed at <see cref="WindowSize"/>, fullscreen on
    /// the desktop, or exclusive at <see cref="ExclusiveSize"/> (null: the desktop's mode).</summary>
    public Fullscreen Fullscreen = Fullscreen.Off;
    public Resolution WindowSize = DefaultWindow;
    public Resolution? ExclusiveSize;
    /// <summary>The frame's size, % of the window's (<see cref="Renderer.Scales"/>); Android's
    /// default is Auto (a frame of about 1080p's pixels: big tablets' GPUs can't fill theirs).</summary>
    public int RenderScale = OperatingSystem.IsAndroid() ? Renderer.AutoScale : Renderer.FullScale;
    public VSync VSync = VSync.On;
    /// <summary>Frames a second at most with VSync off or adaptive (<see cref="FrameLimiter.Limits"/>).</summary>
    public int FrameLimit = FrameLimiter.Off;
    /// <summary>SDL_GPU's driver, from the next start.</summary>
    public GpuBackend Backend = GpuBackend.Auto;
    /// <summary>The display's gamma (<see cref="Renderer.Gamma"/>): above 1 brighter.</summary>
    public float Gamma = Renderer.DefaultGamma;
    public Difficulty Difficulty = Difficulty.Normal;
    /// <summary>Gore (0x5742dc, on by default): green sparks, slime, blown off parts, the head shots row.</summary>
    public bool Gore = true;
    /// <summary>The levels' look, from the next level on; the 2D screens are filtered at once.</summary>
    public Graphics Graphics = Graphics.Original;
    public AntiAliasing AntiAliasing = AntiAliasing.Off;
    /// <summary>How the frame holds the two eyes (<see cref="Stereo"/>), the eyes' distance apart
    /// (MDK units) and the distance their images converge at (0: infinity).</summary>
    public Stereo Stereo = Stereo.Off;
    public float StereoSeparation = 0.25f;
    public float StereoConvergence = 10f;
    /// <summary>The phone's gyroscope turns the view (a VR viewer).</summary>
    public HeadTracking HeadTracking = HeadTracking.Off;
    /// <summary>When the touch screen's on-screen controls show (Android).</summary>
    public TouchButtons Touch = TouchButtons.Auto;
    /// <summary>The on-screen controls the player moved or resized (the others keep their
    /// defaults), and the controls' opacity (%).</summary>
    public readonly Dictionary<TouchPart, TouchPlace> TouchPlaces = [];
    public int TouchOpacity = MaxOpacity;
    /// <summary>Rebound actions: action → key or mouse button name (see <see cref="Input.Bind"/>).</summary>
    public readonly Dictionary<Key, string> Bindings = [];
    /// <summary>Rebound actions: action → gamepad button name (see <see cref="Input.BindPad"/>).</summary>
    public readonly Dictionary<Key, string> PadBindings = [];
    /// <summary>Mods by folder; those not listed are on (<see cref="ModCatalog"/>).</summary>
    public readonly Dictionary<string, ModState> Mods = new(StringComparer.OrdinalIgnoreCase);
    /// <summary>The HD textures' upscaler archive and its SHA-256; empty: the platform's default.</summary>
    public string UpscalerUrl = "";
    public string UpscalerSha256 = "";

    public UpscalerSource Upscaler => UpscalerSource.Of(UpscalerUrl, UpscalerSha256);

    public ModState StateOf(string mod) => Mods.GetValueOrDefault(mod, ModState.On);

    /// <summary>The settings file in a user folder.</summary>
    public static string PathIn(string folder) => Path.Combine(folder, FileName);

    public static Settings Load(string path) => File.Exists(path) ? Parse(File.ReadAllText(path)) : new Settings();

    public void Save(string path)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, Format());
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Console.Error.WriteLine($"Settings not saved: {e.Message}");
        }
    }

    /// <summary>Settings from their lines; unknown or bad lines keep the defaults.</summary>
    public static Settings Parse(string text)
    {
        var settings = new Settings();
        foreach (var line in text.Split('\n'))
        {
            var parts = line.Trim().Split('=', 2);
            if (parts.Length != 2)
            {
                continue;
            }

            settings.Set(parts[0].Trim(), parts[1].Trim());
        }

        return settings;
    }

    public string Format()
    {
        var lines = new List<string>
        {
            $"master_volume={MasterVolume}",
            $"music_volume={MusicVolume}",
            $"effects_volume={EffectsVolume}",
            $"music_filter={MusicFilter}",
            $"mouse_sensitivity={MouseSensitivity.ToString(CultureInfo.InvariantCulture)}",
            $"invert_mouse={InvertMouse}",
            $"pad_sensitivity={PadSensitivity.ToString(CultureInfo.InvariantCulture)}",
            $"invert_pad={InvertPad}",
            $"touch_controls={Touch}",
            $"touch_opacity={TouchOpacity}",
            $"fullscreen={Fullscreen}",
            $"window_size={WindowSize}",
            $"exclusive_size={ExclusiveSize}",
            $"render_scale={RenderScale}",
            $"vsync={VSync}",
            $"frame_limit={FrameLimit}",
            $"gpu_backend={Backend}",
            $"gamma={Gamma.ToString(CultureInfo.InvariantCulture)}",
            $"difficulty={Difficulty}",
            $"gore={Gore}",
            $"graphics={Graphics}",
            $"antialiasing={AntiAliasing}",
            $"stereo={Stereo}",
            $"stereo_separation={StereoSeparation.ToString(CultureInfo.InvariantCulture)}",
            $"stereo_convergence={StereoConvergence.ToString(CultureInfo.InvariantCulture)}",
            $"head_tracking={HeadTracking}",
            $"upscaler_url={UpscalerUrl}",
            $"upscaler_sha256={UpscalerSha256}",
        };
        lines.AddRange(Bindings.Select(b => $"{BindPrefix}{b.Key}={b.Value}"));
        lines.AddRange(PadBindings.Select(b => $"{PadPrefix}{b.Key}={b.Value}"));
        lines.AddRange(TouchPlaces.OrderBy(p => p.Key).Select(p => $"{TouchPrefix}{p.Key}={p.Value}"));
        lines.AddRange(Mods.OrderBy(m => m.Key, StringComparer.OrdinalIgnoreCase).Select(m => $"{ModPrefix}{m.Key}={m.Value}"));
        return string.Join('\n', lines) + "\n";
    }

    private void Set(string name, string value)
    {
        if (name.StartsWith(BindPrefix, StringComparison.Ordinal))
        {
            if (Enum.TryParse<Key>(name[BindPrefix.Length..], out var key))
            {
                Bindings[key] = value;
            }

            return;
        }

        if (name.StartsWith(PadPrefix, StringComparison.Ordinal))
        {
            if (Enum.TryParse<Key>(name[PadPrefix.Length..], out var key))
            {
                PadBindings[key] = value;
            }

            return;
        }

        if (name.StartsWith(TouchPrefix, StringComparison.Ordinal))
        {
            if (Enum.TryParse<TouchPart>(name[TouchPrefix.Length..], out var part) && Enum.IsDefined(part) && TouchPlace.TryParse(value, out var place))
            {
                TouchPlaces[part] = place;
            }

            return;
        }

        if (name.StartsWith(ModPrefix, StringComparison.Ordinal))
        {
            if (Enum.TryParse<ModState>(value, out var state) && Enum.IsDefined(state))
            {
                Mods[name[ModPrefix.Length..]] = state;
            }

            return;
        }

        switch (name)
        {
            case "master_volume":
                MasterVolume = Volume(value, MasterVolume);
                break;
            case "music_volume":
                MusicVolume = Volume(value, MusicVolume);
                break;
            case "effects_volume":
                EffectsVolume = Volume(value, EffectsVolume);
                break;
            case "music_filter":
                MusicFilter = bool.TryParse(value, out var filter) ? filter : MusicFilter;
                break;
            case "mouse_sensitivity":
                MouseSensitivity = float.TryParse(value, CultureInfo.InvariantCulture, out var sensitivity) ? sensitivity : MouseSensitivity;
                break;
            case "invert_mouse":
                InvertMouse = bool.TryParse(value, out var invert) ? invert : InvertMouse;
                break;
            case "pad_sensitivity":
                PadSensitivity = float.TryParse(value, CultureInfo.InvariantCulture, out var padSensitivity) ? padSensitivity : PadSensitivity;
                break;
            case "invert_pad":
                InvertPad = bool.TryParse(value, out var invertPad) ? invertPad : InvertPad;
                break;
            case "touch_controls":
                Touch = Enum.TryParse<TouchButtons>(value, out var touch) && Enum.IsDefined(touch) ? touch : Touch;
                break;
            case "touch_opacity":
                TouchOpacity = int.TryParse(value, out var opacity) ? Math.Clamp(opacity, MinOpacity, MaxOpacity) : TouchOpacity;
                break;
            // Older settings: True or False.
            case "fullscreen" when bool.TryParse(value, out var on):
                Fullscreen = on ? Fullscreen.Desktop : Fullscreen.Off;
                break;
            case "fullscreen":
                Fullscreen = Enum.TryParse<Fullscreen>(value, out var fullscreen) && Enum.IsDefined(fullscreen) ? fullscreen : Fullscreen;
                break;
            case "window_size":
                WindowSize = Resolution.TryParse(value, out var size) ? size : WindowSize;
                break;
            case "exclusive_size":
                ExclusiveSize = Resolution.TryParse(value, out var exclusive) ? exclusive : ExclusiveSize;
                break;
            case "render_scale":
                RenderScale = int.TryParse(value, out var scale) && Renderer.Scales.Contains(scale) ? scale : RenderScale;
                break;
            case "vsync":
                VSync = Enum.TryParse<VSync>(value, out var vsync) && Enum.IsDefined(vsync) ? vsync : VSync;
                break;
            case "frame_limit":
                FrameLimit = int.TryParse(value, out var limit) && FrameLimiter.Limits.Contains(limit) ? limit : FrameLimit;
                break;
            case "gpu_backend":
                Backend = Enum.TryParse<GpuBackend>(value, out var backend) && Enum.IsDefined(backend) ? backend : Backend;
                break;
            case "gamma":
                Gamma = float.TryParse(value, CultureInfo.InvariantCulture, out var gamma)
                    ? Math.Clamp(gamma, Renderer.MinGamma, Renderer.MaxGamma) : Gamma;
                break;
            case "difficulty":
                Difficulty = Enum.TryParse<Difficulty>(value, out var difficulty) ? difficulty : Difficulty;
                break;
            case "gore":
                Gore = bool.TryParse(value, out var gore) ? gore : Gore;
                break;
            case "graphics":
                Graphics = Enum.TryParse<Graphics>(value, out var graphics) && Enum.IsDefined(graphics) ? graphics : Graphics;
                break;
            case "antialiasing":
                AntiAliasing = Enum.TryParse<AntiAliasing>(value, out var antiAliasing) && Enum.IsDefined(antiAliasing) ? antiAliasing : AntiAliasing;
                break;
            case "stereo":
                Stereo = Enum.TryParse<Stereo>(value, out var stereo) && Enum.IsDefined(stereo) ? stereo : Stereo;
                break;
            case "stereo_separation":
                StereoSeparation = float.TryParse(value, CultureInfo.InvariantCulture, out var separation)
                    ? Math.Clamp(separation, 0f, StereoModes.MaxSeparation) : StereoSeparation;
                break;
            case "stereo_convergence":
                StereoConvergence = float.TryParse(value, CultureInfo.InvariantCulture, out var convergence)
                    ? Math.Clamp(convergence, 0f, StereoModes.MaxConvergence) : StereoConvergence;
                break;
            case "head_tracking":
                HeadTracking = Enum.TryParse<HeadTracking>(value, out var tracking) && Enum.IsDefined(tracking) ? tracking : HeadTracking;
                break;
            case "upscaler_url":
                UpscalerUrl = value;
                break;
            case "upscaler_sha256":
                UpscalerSha256 = value;
                break;
            // Older settings' HD switch: now the HD textures mod's.
            case "textures" when value is "Hd" or "Original":
                Mods.TryAdd(HdGenerator.ModFolder, value == "Hd" ? ModState.On : ModState.Off);
                break;
        }
    }

    private static int Volume(string value, int fallback) =>
        int.TryParse(value, out var volume) ? Math.Clamp(volume, 0, MaxVolume) : fallback;

    /// <summary>The volumes and filter, the window, the frame's sampling, the mouse and the bindings.</summary>
    public void Apply(AudioDevice audio, Window window, Renderer renderer, Input input)
    {
        renderer.AntiAliasing = AntiAliasing;
        renderer.StereoMode = Stereo;
        renderer.StereoSeparation = StereoSeparation;
        renderer.StereoConvergence = StereoConvergence;
        window.TrackHead(HeadTracking);
        window.ShowTouch(Touch);
        window.ArrangeTouch(TouchPlaces, (float)TouchOpacity / MaxOpacity);
        renderer.CanvasSampling = Graphics == Graphics.Enhanced ? Sampling.Linear : Sampling.Nearest;
        audio.MasterGain = Gain(MasterVolume);
        audio.SetBusGain(Bus.Music, Gain(MusicVolume));
        audio.SetBusGain(Bus.Effects, Gain(EffectsVolume));
        audio.MusicFilter = MusicFilter ? Filter.On : Filter.Off;
        window.Apply(new DisplaySetup(Fullscreen, WindowSize, ExclusiveSize));
        renderer.RenderScale = RenderScale;
        renderer.VSync = VSync;
        renderer.Gamma = Gamma;
        input.MouseScale = MouseSensitivity;
        input.InvertMouse = InvertMouse;
        input.PadSensitivity = PadSensitivity;
        input.InvertPad = InvertPad;
        Bind(input);
    }

    /// <summary>The defaults, then the rebound actions (files without an action keep its default).</summary>
    public void Bind(Input input)
    {
        input.ResetBindings();
        foreach (var (key, control) in Bindings)
        {
            input.Bind(key, control);
        }

        input.ResetPadBindings();
        foreach (var (key, control) in PadBindings)
        {
            input.BindPad(key, control);
        }
    }

    /// <summary>0-100 to a linear gain (the volume as a fraction, silent at 0).</summary>
    private static float Gain(int volume) => (float)volume / MaxVolume;
}
