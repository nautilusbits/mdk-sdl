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
//   --touch-layout          the touch layout editor (Options, Touch), also without a touch screen
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

#define SDL_MAIN_HANDLED

#include <algorithm>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Mdk.Core/Console.h"
#include "Mdk.Core/Dictionary.h"
#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/EnumNames.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/Display.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Formats/BetaDemo.h"
#include "Mdk.Formats/MdkData.h"
#include "Mdk.Game/FrameDump.h"
#include "Mdk.Game/Flow/EnumNames.h"
#include "Mdk.Game/Flow/Game.h"
#include "Mdk.Game/Flow/TestPresses.h"
#include "Mdk.Game/HdTextures/HdGenerator.h"
#include "Mdk.Game/Menu/MainMenu.h"
#include "Mdk.Game/Menu/StatsScreen.h"
#include "Mdk.Game/RideTest.h"
#include "Mdk.Game/Scripts/StrikeScene.h"
#include "Mdk.Game/SniperTest.h"
#include "Mdk.Game/SoakTest.h"
#include "Mdk.Game/Viewer.h"

using namespace Mdk;
using namespace Mdk::Game;
using namespace Mdk::Game::Flow;
using Engine::Platform::Resolution;
using Engine::Render::Stereo;

namespace
{
    constexpr int DefaultLevel = 7;
    // --perf's game seconds before frames are measured.
    constexpr float DefaultWarmup = 2.f;

    // The players' options (the tests' are listed above).
    constexpr std::string_view Help = R"(MDK in C# and SDL3. Needs the original game's data (see README.md).

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
  --help, -h              this text)";

    std::vector<int> ParseInts(const std::string& text)
    {
        std::vector<int> values;
        for (const auto& part : Str::Split(text, ','))
        {
            values.push_back(Str::ParseInt(part));
        }

        return values;
    }
}

// SDL's main isn't used (SDL_MAIN_HANDLED): as the C# program, a plain console entry point.
int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    Dictionary<std::string, std::string> options;
    for (const auto& a : args)
    {
        if (!Str::StartsWith(a, "--"))
        {
            continue;
        }

        auto p = Str::SplitCount(std::string_view(a).substr(2), '=', 2);
        options.Add(p[0], p.size() > 1 ? p[1] : "");
    }

    auto has = [&options](const std::string& name) { return options.ContainsKey(name); };
    auto get = [&options](const std::string& name) -> std::optional<std::string>
    {
        const auto* value = options.Find(name);
        return value != nullptr ? std::optional<std::string>(*value) : std::nullopt;
    };

    // The players' options (the tests' are listed above).
    if (has("help") || std::find(args.begin(), args.end(), "-h") != args.end())
    {
        Console::WriteLine(Help);
        return 0;
    }

    // Before the data: CI runs it without the game.
    if (auto downloadUrl = get("download-test"))
    {
        return Flow::Game::DownloadTest(*downloadUrl);
    }

    auto data = Formats::MdkData::Find();
    if (data == nullptr)
    {
        Console::Error().WriteLine("MDK data not found: install MDK (GOG/Steam), put this folder in it, set MDK_DATA_DIR, or name it as mdk in mdk_paths.cfg next to the program.");
        return 1;
    }

    if (auto upscale = get("upscale-textures"))
    {
        const auto& defaults = HdTextures::HdOptions::Default();
        auto levels = !upscale->empty() ? ParseInts(*upscale)
            : std::vector<int>(HdTextures::HdOptions::AllLevels.begin(), HdTextures::HdOptions::AllLevels.end());
        auto model = defaults.Model;
        if (auto name = get("hd-model"))
        {
            if (!EnumNames::TryParse(*name, Flow::Names::HdModel, model, EnumNames::Case::Ignore))
            {
                throw ArgumentException(std::format("Requested value '{}' was not found.", *name));
            }
        }

        auto factor = get("hd-scale");
        auto scale = factor.has_value() ? Str::ParseInt(*factor) : defaults.Scale;
        const auto& scales = HdTextures::HdOptions::Scales;
        if (std::find(scales.begin(), scales.end(), scale) == scales.end())
        {
            std::vector<std::string> names;
            for (auto s : scales)
            {
                names.push_back(std::to_string(s));
            }

            Console::Error().WriteLine(std::format("--hd-scale: {}", Str::Join(" or ", names)));
            return 1;
        }

        return Flow::Game::UpscaleTextures(data, HdTextures::HdOptions{levels, model, scale});
    }

    if (auto exportTo = get("export-assets"))
    {
        return Flow::Game::ExportAssets(data, *exportTo);
    }

    std::optional<Vector3> position;
    std::optional<float> yaw;
    if (auto at = get("at"))
    {
        std::vector<float> v;
        for (const auto& s : Str::Split(*at, ','))
        {
            v.push_back(Str::ParseFloat(s));
        }

        position = Vector3(v.at(0), v.at(1), v.at(2));
        yaw = v.size() > 3 ? std::optional<float>(v[3]) : std::nullopt;
    }

    auto Number = [&get](const std::string& name) -> std::optional<int>
    {
        auto text = get(name);
        return text.has_value() ? std::optional<int>(Str::ParseInt(*text)) : std::nullopt;
    };
    // The first given of these (C#'s ?? chain).
    auto level = DefaultLevel;
    for (const auto* name : {"level", "stats", "briefing", "stream", "fall"})
    {
        if (auto number = Number(name))
        {
            level = *number;
            break;
        }
    }

    if (Formats::BetaDemo::IsBeta(level) && Formats::BetaDemo::Find(data) == nullptr)
    {
        Console::Error().WriteLine("The 1996 beta demo not found: set MDK_BETA_DIR, or name it as beta in mdk_paths.cfg next to the program.");
        return 1;
    }

    auto pitchText = get("pitch");
    auto pitch = pitchText.has_value() ? Str::ParseFloat(*pitchText) : 0.f;
    std::optional<Stereo> stereo;
    if (auto stereoText = get("stereo"))
    {
        Stereo parsedStereo{};
        if (!Engine::Render::StereoModes::TryParse(*stereoText, parsedStereo))
        {
            Console::Error().WriteLine("--stereo: off, sbs, sbsfull, crossview, int, intr, tab, tabr or vr");
            return 1;
        }

        stereo = parsedStereo;
    }

    auto Real = [&get](const std::string& name) -> std::optional<float>
    {
        auto text = get(name);
        return text.has_value() ? std::optional<float>(Str::ParseFloat(*text)) : std::nullopt;
    };
    auto stereoSeparation = Real("stereo-separation");
    auto stereoConvergence = Real("stereo-convergence");
    auto screenshot = get("screenshot");
    auto sound = has("mute") ? SoundMode::Muted : SoundMode::On;
    auto Seconds = [&Real](const std::string& name) { return Real(name).value_or(0.f); };
    ViewerOptions viewer;
    viewer.Level = level;
    viewer.Position = position;
    viewer.Yaw = yaw;
    viewer.Pitch = pitch;
    viewer.Sound = sound;
    viewer.Screenshot = screenshot;
    viewer.Wait = Seconds("wait");
    viewer.Walk = Seconds("walk");
    viewer.Look = Seconds("look");
    viewer.Delay = Seconds("delay");
    viewer.Jump = has("jump");
    viewer.Fire = has("fire");
    if (auto give = get("give"))
    {
        viewer.Give = Str::Split(*give, ',');
    }

    viewer.Use = has("use");
    viewer.Fly = has("fly");
    viewer.Profile = has("profile");
    viewer.Trace = has("trace");
    if (auto sniper = get("sniper"))
    {
        viewer.Sniper = SniperTest::Setup(*sniper);
    }

    viewer.Zoom = Seconds("zoom");
    viewer.SniperFire = has("sniper-fire");
    if (auto strike = get("strike"))
    {
        viewer.Strike = *strike == "dive" ? Scripts::StrikeScene::Plane::Only : Scripts::StrikeScene::Plane::WithPilot;
    }

    viewer.Die = has("die");
    viewer.Event = Number("event");
    if (auto teleport = get("teleport"))
    {
        viewer.Teleport = TeleportTarget::Parse(*teleport);
    }

    viewer.Kill = get("kill");
    viewer.Ride = get("ride");
    if (auto bomber = get("bomber"))
    {
        viewer.Bomber = *bomber == "drop" ? BomberTest::Drop : BomberTest::Ride;
    }

    viewer.Snapshot = get("snapshot");
    if (auto soak = get("soak"))
    {
        viewer.Soak = !soak->empty() ? Str::ParseInt(*soak) : 1;
    }

    viewer.Route = has("tour") ? SoakRoute::Tour : SoakRoute::Stay;
    viewer.BetaTeleport = Number("beta-teleport");
    if (auto roll = get("roll"))
    {
        viewer.Roll = *roll == "left" ? BetaRoll::Left : BetaRoll::Right;
    }

    viewer.Console = get("console");
    if (auto frames = get("frames"))
    {
        viewer.Frames = FrameDump::Parse(*frames);
    }

    viewer.FrameRate = Number("fps");

    // Test options of a level (and --screenshot without --menu) skip the menu, as in the Godot port.
    auto start = has("end") ? Start::EndMovie
        : has("stats") ? Start::Statistics
        : has("briefing") ? Start::Briefing
        : has("stream") ? Start::Stream
        : has("fall") ? Start::Fall
        : has("level") || (screenshot.has_value() && !has("menu")) ? Start::Level
        : Start::Menu;
    auto page = has("touch-layout") ? Menu::MenuPage::TouchLayout
        : has("controls") ? Menu::MenuPage::Controls
        : has("gamepad") ? Menu::MenuPage::Gamepad
        : has("options") ? Menu::MenuPage::Options
        : has("beta-levels") ? Menu::MenuPage::BetaLevels
        : Menu::MenuPage::Main;
    // Tests run without a window: --hidden, or MDK_HIDDEN set (so nothing takes the focus).
    auto hiddenVariable = GetEnv("MDK_HIDDEN");
    auto hidden = has("hidden") || (hiddenVariable.has_value() && !hiddenVariable->empty());
    GameOptions gameOptions;
    gameOptions.Start = start;
    gameOptions.Level = viewer;
    gameOptions.Display = hidden ? Display::Hidden : Display::Window;
    gameOptions.Page = page;
    gameOptions.Splash = !has("menu") || has("splash");
    if (auto phase = Number("phase"))
    {
        gameOptions.Phase = static_cast<Menu::StatsScreen::Phase>(*phase);
    }

    if (auto counts = get("counts"))
    {
        gameOptions.Counts = ParseInts(*counts);
    }

    gameOptions.Towns = Number("towns");
    gameOptions.Load = get("load");
    gameOptions.Save = get("save");
    gameOptions.Screenshot = screenshot;
    gameOptions.Wait = Seconds("wait");
    gameOptions.Health = Number("health");
    if (has("enhanced") || has("original"))
    {
        gameOptions.Graphics = has("enhanced") ? Graphics::Enhanced : Graphics::Original;
    }

    if (has("bloodyes") || has("nobloodno"))
    {
        gameOptions.Gore = has("bloodyes");
    }

    gameOptions.Stereo = stereo;
    gameOptions.StereoSeparation = stereoSeparation;
    gameOptions.StereoConvergence = stereoConvergence;
    gameOptions.Gamma = Real("gamma");
    if (auto qualityText = get("quality"))
    {
        Engine::Render::EffectQuality quality{};
        if (!EnumNames::TryParse(*qualityText, Flow::Names::EffectQuality, quality, EnumNames::Case::Ignore))
        {
            throw ArgumentException(std::format("Requested value '{}' was not found.", *qualityText));
        }

        gameOptions.Quality = quality;
    }

    if (auto presses = get("press"))
    {
        gameOptions.Presses = std::make_shared<TestPresses>(TestPresses::Parse(*presses));
    }

    if (auto mods = get("mod"))
    {
        gameOptions.Mods = Str::Split(*mods, ',', Str::SplitOptions::RemoveEmpty);
    }

    gameOptions.Gpu = get("gpu");
    gameOptions.RecordAudio = get("record-audio");
    Resolution hiddenSize{};
    auto sizeText = get("size");
    if (sizeText.has_value() && Resolution::TryParse(*sizeText, hiddenSize))
    {
        gameOptions.HiddenSize = hiddenSize;
    }

    gameOptions.HiddenFrames = has("swapchain") ? Engine::Render::HiddenFrames::Swapchain : Engine::Render::HiddenFrames::Offscreen;
    if (auto perf = get("perf"))
    {
        gameOptions.Perf = !perf->empty() ? Str::ParseFloat(*perf) : DefaultWarmup;
    }

    Flow::Game game(data, gameOptions);
    game.Run();
    return 0;
}
