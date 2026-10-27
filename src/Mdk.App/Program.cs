using System.Globalization;
using System.Numerics;
using Mdk.Engine.Platform;
using Mdk.Engine.Render;
using Mdk.Formats;
using Mdk.Game;
using Mdk.Game.Flow;
using Mdk.Game.HdTextures;
using Mdk.Game.Menu;
using Mdk.Game.Scripts;

// MDK in C# and SDL3: the splash, the main menu, then the levels in the order 7, 6, 3, 4, 8, 5 with
// their loading screens, briefings, statistics and saves. In a level: WASD/arrows, mouse, Space
// jumps, Shift runs (Caps Lock: always), Ctrl or the left mouse button fires, Enter uses the item, Tab/[/] and 1-5
// select it, the right mouse button toggles sniper mode (wheel or PageUp/PageDown zoom, Tab/[/]
// select the ammo), F1 flying camera with E/Q up and down, F12 screenshot, Esc the pause menu,
// F2 quick save (names the save), F9 quick load, F3 the debug overlay, the key left of 1 the console.
//
//   --level=N               play level 3-8 at once (no menu); 961, 963, 966: the 1996 demo's
//   --menu                  the main menu without the splash (--splash: with it)
//   --options, --controls, --gamepad   the menu's options, controls or gamepad page (--beta-levels: the demo's levels)
//   --touch-layout          the touch layout editor (Options, Gamepad), also without a touch screen
//   --stats=N               the screens after level N (--phase=1-4 starts at a page,
//                           --counts=shots,hits,sniper,sniper hits,kills,enemies,heads, --towns=bits)
//   --briefing=N            the briefing of level N
//   --fall=N                the fall before level N, then the level (--profile prints it every second,
//                           --walk=seconds holds "forward" from --delay=seconds of fall)
//   --end                   the end movies
//   --stream=N              the stream after level N (--health=N: Kurt's health)
//   --load=NAME             load a saved game
//   --save=NAME             save the first level when it starts (tests)
//   --at=x,y,z[,yaw]        Kurt's feet (MDK coordinates) and yaw in degrees
//   --pitch=degrees         flying camera pitch (positive looks up)
//   --fly                   start with the flying camera
//   --mute                  no sound (tests)
//   --enhanced, --original  the enhanced or the original look instead of the settings' (tests)
//   --stereo=MODE           the stereo layout instead of the settings': off, sbs, sbsfull, crossview, int,
//                           intr, tab, tabr, vr (--stereo-separation=, --stereo-convergence=; tests)
//   --bloodyes, --nobloodno gore on or off instead of the settings' (the original's -bloodyes, -nobloodno)
//   --gamma=0.5-2           the display's gamma instead of the settings' (tests)
//   --quality=full|mobile   the enhanced look's cost instead of the OS's (Android: mobile; tests)
//   --gpu=d3d12|vulkan|metal the GPU backend instead of the settings' (tests; not saved)
//   --hidden                no window, frames drawn off screen (tests; also MDK_HIDDEN=1)
//   --size=WxH              the hidden frame's size instead of 1280x960 (tests, videos)
//   --swapchain             a hidden window's frames through its swapchain too, as when shown (tests)
//   --record-audio=file.wav the sound from the start as a WAV (with --mute, in step with --frames; videos)
//   --screenshot=file.bmp   save a frame after --wait seconds of game time, print Kurt, quit (tests)
//   --wait=seconds          game time before the screenshot
//   --walk=seconds          hold "forward" for this long (tests)
//   --delay=seconds         start the held keys this late (tests; as the Godot port's --delay)
//   --jump                  hold "jump" (tests)
//   --fire                  hold "fire" (tests)
//   --give=SW_HBOMB,...     pickups Kurt starts with (tests)
//   --use                   press "use" after 1 second (tests)
//   --die                   Kurt is hurt to death after 1 second (tests)
//   --event=N               a special_event after 1 second: 1 ends the level (tests)
//   --profile               print the scripted objects once per second of game time (tests)
//   --trace                 print Kurt, the camera and what carries him every frame (tests)
//   --sniper[=zoom[,pitch]] sniper mode once Kurt stands (zoom 1 to 0.25, pitch positive down; tests)
//   --zoom=seconds          hold "zoom in" in sniper mode (tests)
//   --sniper-fire           fire one sniper round once the clip is loaded (tests)
//   --strike[=dive]         Bones' full-screen strike after 1 second (dive: the plane only; tests)
//   --teleport=ARENA,x,y,z  teleport Kurt there after the delay (tests)
//   --kill=TYPE             kill the first object of that type after the delay (tests)
//   --ride=TYPE             put Kurt on the first walker of that type after the delay (tests)
//   --bomber[=drop]         LEVEL7: call the XE of DANT_5 after the delay, board it (drop: and drop a bomb; tests)
//   --snapshot=NAME         at the screenshot, a full save as F2 makes, its hash printed (tests)
//   --beta-teleport=N       the 1996 demo's teleport N after the delay (tests)
//   --roll=left|right       hold a roll of the 1996 demo's levels after the delay (tests)
//   --console="pos;god"     open the console after the delay and run these commands (tests)
//   --press=QuickSave@1,Menu.Accept@1.5  press game or menu keys once at these times (tests)
//   --frames=dir,count[,every] after --wait, save every Nth frame (4: 15 a second) of a level, then quit
//   --fps=N                 test frames a second instead of 60: above, frames drawn between the steps (tests)
//   --perf[=warmup]         after warmup seconds (2) of game time, wait for the GPU each frame and print
//                           the frames' costs at the end: sections, allocations, collections (tests)
//   --soak[=seed]           random seeded keys on every screen, 6 steps a frame, checks in a level
//                           (tests; --tour: every arena in turn during --wait; tests/soak_test.sh)
//   --mod=a,b               only these mods (folders of mods/) on, for this run (tests)
//   --export-assets=dir     write the game's textures, 2D images (PNG) and models (glTF .glb) for
//                           modding into dir, then quit (docs/modding.md)
//   --upscale-textures[=3,7] make the enhanced look's HD textures (all levels, or these), then quit:
//                           --hd-model=general|anime (Real-ESRGAN x4plus or animevideov3), --hd-scale=2|4
//   --download-test=URL     download a URL through the OS's HTTP library, print its size and SHA-256 (CI)

const int DefaultLevel = 7;
// --perf's game seconds before frames are measured.
const float DefaultWarmup = 2f;

var options = args.Where(a => a.StartsWith("--"))
    .Select(a => a[2..].Split('=', 2))
    .ToDictionary(p => p[0], p => p.Length > 1 ? p[1] : "");

// The players' options (the tests' are listed above).
if (options.ContainsKey("help") || args.Contains("-h"))
{
    Console.WriteLine("""
        MDK in C# and SDL3. Needs the original game's data (see README.md).

        Usage: mdk_sdl [options]

          --level=N               play level 3-8 at once (961, 963, 966: the 1996 demo's)
          --menu                  the main menu without the splash
          --options, --controls, --gamepad, --beta-levels   open that menu page
          --load=NAME             load a saved game
          --stats=N, --briefing=N, --fall=N, --stream=N, --end   a screen of the game
          --enhanced, --original  the enhanced or the original look (not saved)
          --stereo=MODE           off, sbs, sbsfull, crossview, int, intr, tab, tabr, vr
                                  (--stereo-separation=, --stereo-convergence=)
          --gpu=d3d12|vulkan|metal  the GPU backend (not saved)
          --bloodyes, --nobloodno gore on or off
          --mute                  no sound
          --mod=a,b               only these mods on, for this run
          --export-assets=dir     write the textures, images and models for modding, then quit
          --upscale-textures[=3,7] make the HD textures, then quit
                                  (--hd-model=general|anime, --hd-scale=2|4)
          --help, -h              this text
        """);
    return 0;
}

// Before the data: CI runs it without the game.
if (options.TryGetValue("download-test", out var downloadUrl))
{
    return Game.DownloadTest(downloadUrl);
}

var data = MdkData.Find();
if (data == null)
{
    Console.Error.WriteLine("MDK data not found: install MDK (GOG/Steam), put this folder in it, set MDK_DATA_DIR, or name it as mdk in mdk_paths.cfg next to the program.");
    return 1;
}

if (options.TryGetValue("upscale-textures", out var upscale))
{
    var defaults = HdOptions.Default;
    var levels = upscale.Length != 0 ? upscale.Split(',').Select(l => int.Parse(l, CultureInfo.InvariantCulture)).ToArray() : HdOptions.AllLevels;
    var model = options.TryGetValue("hd-model", out var name) ? Enum.Parse<HdModel>(name, ignoreCase: true) : defaults.Model;
    var scale = options.TryGetValue("hd-scale", out var factor) ? int.Parse(factor, CultureInfo.InvariantCulture) : defaults.Scale;
    if (!HdOptions.Scales.Contains(scale))
    {
        Console.Error.WriteLine($"--hd-scale: {string.Join(" or ", HdOptions.Scales)}");
        return 1;
    }

    return Game.UpscaleTextures(data, new HdOptions(levels, model, scale));
}

if (options.TryGetValue("export-assets", out var exportTo))
{
    return Game.ExportAssets(data, exportTo);
}

Vector3? position = null;
float? yaw = null;
if (options.TryGetValue("at", out var at))
{
    var v = at.Split(',').Select(s => float.Parse(s, CultureInfo.InvariantCulture)).ToArray();
    position = new Vector3(v[0], v[1], v[2]);
    yaw = v.Length > 3 ? v[3] : null;
}

int? Number(string name) => options.TryGetValue(name, out var text) ? int.Parse(text, CultureInfo.InvariantCulture) : null;
var level = Number("level") ?? Number("stats") ?? Number("briefing") ?? Number("stream") ?? Number("fall") ?? DefaultLevel;
if (BetaDemo.IsBeta(level) && BetaDemo.Find(data) == null)
{
    Console.Error.WriteLine("The 1996 beta demo not found: set MDK_BETA_DIR, or name it as beta in mdk_paths.cfg next to the program.");
    return 1;
}

var pitch = options.TryGetValue("pitch", out var pitchText) ? float.Parse(pitchText, CultureInfo.InvariantCulture) : 0f;
Stereo? stereo = null;
if (options.TryGetValue("stereo", out var stereoText))
{
    if (!StereoModes.TryParse(stereoText, out var parsedStereo))
    {
        Console.Error.WriteLine("--stereo: off, sbs, sbsfull, crossview, int, intr, tab, tabr or vr");
        return 1;
    }

    stereo = parsedStereo;
}

var stereoSeparation = options.TryGetValue("stereo-separation", out var separationText)
    ? float.Parse(separationText, CultureInfo.InvariantCulture) : (float?)null;
var stereoConvergence = options.TryGetValue("stereo-convergence", out var convergenceText)
    ? float.Parse(convergenceText, CultureInfo.InvariantCulture) : (float?)null;
options.TryGetValue("screenshot", out var screenshot);
var sound = options.ContainsKey("mute") ? SoundMode.Muted : SoundMode.On;
float Seconds(string name) => options.TryGetValue(name, out var text) ? float.Parse(text, CultureInfo.InvariantCulture) : 0f;
var viewer = new ViewerOptions(level, position, yaw, pitch, sound)
{
    Screenshot = screenshot,
    Wait = Seconds("wait"),
    Walk = Seconds("walk"),
    Look = Seconds("look"),
    Delay = Seconds("delay"),
    Jump = options.ContainsKey("jump"),
    Fire = options.ContainsKey("fire"),
    Give = options.TryGetValue("give", out var give) ? give.Split(',') : [],
    Use = options.ContainsKey("use"),
    Fly = options.ContainsKey("fly"),
    Profile = options.ContainsKey("profile"),
    Trace = options.ContainsKey("trace"),
    Sniper = options.TryGetValue("sniper", out var sniper) ? SniperTest.Setup(sniper) : null,
    Zoom = Seconds("zoom"),
    SniperFire = options.ContainsKey("sniper-fire"),
    Strike = options.TryGetValue("strike", out var strike) ? (strike == "dive" ? StrikeScene.Plane.Only : StrikeScene.Plane.WithPilot) : null,
    Die = options.ContainsKey("die"),
    Event = Number("event"),
    Teleport = options.TryGetValue("teleport", out var teleport) ? TeleportTarget.Parse(teleport) : null,
    Kill = options.GetValueOrDefault("kill"),
    Ride = options.GetValueOrDefault("ride"),
    Bomber = options.TryGetValue("bomber", out var bomber) ? (bomber == "drop" ? BomberTest.Drop : BomberTest.Ride) : null,
    Snapshot = options.GetValueOrDefault("snapshot"),
    Soak = options.TryGetValue("soak", out var soak) ? (soak.Length != 0 ? int.Parse(soak, CultureInfo.InvariantCulture) : 1) : null,
    Route = options.ContainsKey("tour") ? SoakRoute.Tour : SoakRoute.Stay,
    BetaTeleport = Number("beta-teleport"),
    Roll = options.TryGetValue("roll", out var roll) ? (roll == "left" ? BetaRoll.Left : BetaRoll.Right) : null,
    Console = options.GetValueOrDefault("console"),
    Frames = options.TryGetValue("frames", out var frames) ? FrameDump.Parse(frames) : null,
    FrameRate = Number("fps"),
};

// Test options of a level (and --screenshot without --menu) skip the menu, as in the Godot port.
var start = options.ContainsKey("end") ? Start.EndMovie
    : options.ContainsKey("stats") ? Start.Statistics
    : options.ContainsKey("briefing") ? Start.Briefing
    : options.ContainsKey("stream") ? Start.Stream
    : options.ContainsKey("fall") ? Start.Fall
    : options.ContainsKey("level") || (screenshot != null && !options.ContainsKey("menu")) ? Start.Level
    : Start.Menu;
var page = options.ContainsKey("touch-layout") ? MenuPage.TouchLayout
    : options.ContainsKey("controls") ? MenuPage.Controls
    : options.ContainsKey("gamepad") ? MenuPage.Gamepad
    : options.ContainsKey("options") ? MenuPage.Options
    : options.ContainsKey("beta-levels") ? MenuPage.BetaLevels
    : MenuPage.Main;
// Tests run without a window: --hidden, or MDK_HIDDEN set (so nothing takes the focus).
var hidden = options.ContainsKey("hidden") || !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("MDK_HIDDEN"));
using var game = new Game(data, new GameOptions(start, viewer)
{
    Display = hidden ? Display.Hidden : Display.Window,
    Page = page,
    Splash = !options.ContainsKey("menu") || options.ContainsKey("splash"),
    Phase = Number("phase") is { } phase ? (StatsScreen.Phase)phase : null,
    Counts = options.TryGetValue("counts", out var counts) ? counts.Split(',').Select(c => int.Parse(c, CultureInfo.InvariantCulture)).ToList() : [],
    Towns = Number("towns"),
    Load = options.GetValueOrDefault("load"),
    Save = options.GetValueOrDefault("save"),
    Screenshot = screenshot,
    Wait = Seconds("wait"),
    Health = Number("health"),
    Graphics = options.ContainsKey("enhanced") ? Graphics.Enhanced : options.ContainsKey("original") ? Graphics.Original : null,
    Gore = options.ContainsKey("bloodyes") ? true : options.ContainsKey("nobloodno") ? false : null,
    Stereo = stereo,
    StereoSeparation = stereoSeparation,
    StereoConvergence = stereoConvergence,
    Gamma = options.TryGetValue("gamma", out var gammaText) ? float.Parse(gammaText, CultureInfo.InvariantCulture) : null,
    Quality = options.TryGetValue("quality", out var qualityText) ? Enum.Parse<EffectQuality>(qualityText, ignoreCase: true) : null,
    Presses = options.TryGetValue("press", out var presses) ? TestPresses.Parse(presses) : null,
    Mods = options.TryGetValue("mod", out var mods) ? mods.Split(',', StringSplitOptions.RemoveEmptyEntries) : null,
    Gpu = options.GetValueOrDefault("gpu"),
    RecordAudio = options.GetValueOrDefault("record-audio"),
    HiddenSize = options.TryGetValue("size", out var sizeText) && Resolution.TryParse(sizeText, out var hiddenSize) ? hiddenSize : null,
    HiddenFrames = options.ContainsKey("swapchain") ? HiddenFrames.Swapchain : HiddenFrames.Offscreen,
    Perf = options.TryGetValue("perf", out var perf) ? (perf.Length != 0 ? float.Parse(perf, CultureInfo.InvariantCulture) : DefaultWarmup) : null,
});
game.Run();
return 0;
