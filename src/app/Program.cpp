// MDK in C++ and SDL3: the splash, the main menu, then the levels in the order 7, 6, 3, 4, 8, 5 with
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

#include "core/Console.h"
#include "core/Dictionary.h"
#include "core/DotNet.h"
#include "core/EnumNames.h"
#include "formats/BetaDemo.h"
#include "formats/MdkData.h"
#include "game/flow/CommandLine.h"
#include "game/flow/EnumNames.h"
#include "game/flow/Game.h"
#include "game/hd_textures/HdGenerator.h"

using namespace Mdk;
using namespace Mdk::Game;
using namespace Mdk::Game::Flow;

namespace
{
    // The players' options (the tests' are listed above).
    constexpr std::string_view Help = R"(MDK in C++ and SDL3. Needs the original game's data (see README.md).

Usage: mdk-sdl [options]

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
    auto options = CommandLine::Parse(args);

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

    auto gameOptions = CommandLine::Options(options);
    if (!gameOptions.has_value())
    {
        return 1;
    }

    if (Formats::BetaDemo::IsBeta(gameOptions->Level.Level) && Formats::BetaDemo::Find(data) == nullptr)
    {
        Console::Error().WriteLine("The 1996 beta demo not found: set MDK_BETA_DIR, or name it as beta in mdk_paths.cfg next to the program.");
        return 1;
    }

    Flow::Game game(data, *gameOptions);
    game.Run();
    return 0;
}
