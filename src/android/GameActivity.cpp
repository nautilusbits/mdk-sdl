// The game: SDL's activity (Java GameActivity: window, input, audio, the Vulkan surface) runs
// SDL_main on SDL's thread with the files SetupActivity found, from the main menu. Settings and
// saves live in the MDK folder played from in place, else in the app's external files folder.
// Options' "Import from folder" and the message why the game stopped show the setup screen
// over the game. A bench build's intent extra "options" (the desktop's, e.g. "--level=7
// --perf=10 --wait=40") plays with those, its output copied to logcat (tag MDK).
//
//   SetupActivity ─files─► GameActivity.java ─SDL_main─► Game.Run ─Import─► SetupActivity ─┐
//                                                          ▲    └stopped─► SetupActivity   │
//                                                          └───────────────────────────────┘

#include <sys/system_properties.h>

#include <cstdlib>
#include <exception>
#include <format>
#include <memory>
#include <optional>
#include <string>

#include "android/JavaBridge.h"
#include "android/Logcat.h"
#include "android/SetupActivity.h"
#include "core/Console.h"
#include "core/DotNet.h"
#include "core/Files.h"
#include "engine/diagnostics/LogWriter.h"
#include "formats/MdkData.h"
#include "game/flow/CommandLine.h"
#include "game/flow/Game.h"
#include "game/Viewer.h"

using namespace Mdk;
using namespace Mdk::Game;
using namespace Mdk::Game::Flow;
using Mdk::Android::JavaBridge;
using Mdk::Engine::Diagnostics::Log;

namespace
{
    // SDL's argv ends with getArguments: the data folder, the user folder, the level, the
    // bench's options ("" without).
    constexpr int SdlArgs = 4;
    // The level the menu's "new game" would start with otherwise (unused from the menu).
    constexpr int DefaultLevel = 7;
    // The game's user folder (settings, saves): see Game.
    constexpr const char* UserFolderVariable = "MDK_USER_DIR";
    constexpr int Overwrite = 1;
    // A bench run's --wait quits through a screenshot there (the user folder), as desktop tests.
    constexpr const char* BenchShot = "bench.bmp";

    // android.os.Build's fields: the system properties they read.
    constexpr const char* ManufacturerProperty = "ro.product.manufacturer";
    constexpr const char* ModelProperty = "ro.product.model";
    constexpr const char* ReleaseProperty = "ro.build.version.release";
    constexpr const char* SdkProperty = "ro.build.version.sdk";

    // The game's files and the user folder (settings, saves, mods, mdk.log).
    struct GameFiles
    {
        std::shared_ptr<Formats::MdkData> Data;
        std::string UserFolder;
    };

    std::string Property(const char* name)
    {
        char value[PROP_VALUE_MAX] = {};
        __system_property_get(name, value);
        return value;
    }

    // C#'s "{e.GetType().Name}: {e.Message}".
    std::string Describe(const std::exception_ptr& error)
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch (const std::exception& e)
        {
            return std::format("{}: {}", JavaBridge::TypeName(e), e.what());
        }
        catch (...)
        {
            return "Exception: unknown";
        }
    }

    Start StartScreen(int level)
    {
        return level > 0 ? Start::Level : Start::Menu;
    }

    ViewerOptions StartLevel(int level)
    {
        ViewerOptions options;
        options.Level = level > 0 ? level : DefaultLevel;
        options.Pitch = 0.f;
        options.Sound = SoundMode::On;
        return options;
    }

    // The bench's options ("--level=7 --perf=10 --wait=40"): the desktop's, --wait quits.
    std::optional<GameOptions> BenchOptions(const std::string& bench, const std::string& userFolder)
    {
        auto options = CommandLine::Parse(Str::Split(bench, ' ', Str::SplitOptions::RemoveEmpty));
        if (options.ContainsKey("wait") && !options.ContainsKey("screenshot"))
        {
            options.Add("screenshot", Path::Combine(userFolder, BenchShot));
        }

        return CommandLine::Options(options);
    }

    // The game until it quits; an exception is logged (mdk.log still open) before the game is
    // destroyed, and returned.
    std::exception_ptr Play(const GameFiles& found, int level, const std::string& bench)
    {
        // Played in place, the user folder is the MDK folder: Import picks another one.
        auto inPlace = found.UserFolder == found.Data->Dir() ? std::optional<std::string>(found.UserFolder) : std::nullopt;
        GameOptions options;
        options.Start = StartScreen(level);
        options.Level = StartLevel(level);
        options.Splash = true;
        options.Import = [inPlace] { Mdk::Android::SetupActivity::Reimport(inPlace); };
        if (!bench.empty())
        {
            auto asked = BenchOptions(bench, found.UserFolder);
            if (!asked.has_value())
            {
                throw ArgumentException(std::format("Bad options: {}", bench));
            }

            asked->Import = options.Import;
            options = *asked;
        }

        Flow::Game game(found.Data, options);
        try
        {
            game.Run();
            return nullptr;
        }
        catch (...)
        {
            auto error = std::current_exception();
            Console::Error().WriteLine(std::format("MDK stopped: {}", Describe(error)));
            return error;
        }
    }

    // The game until it quits; why it stopped, if it did, on the setup screen.
    void Run(const std::string& dataDir, const std::string& userFolder, int level, const std::string& bench)
    {
        // The log keeps what's printed from here: mdk.log gets it once the user folder is known.
        Log::Capture(Log::Lines);
        if (!bench.empty())
        {
            Mdk::Android::Logcat::Install();
        }

        std::exception_ptr stopped;
        try
        {
            Console::WriteLine(std::format("Device: {} {}, Android {} (API {})", Property(ManufacturerProperty),
                Property(ModelProperty), Property(ReleaseProperty), Property(SdkProperty)));
            auto data = Formats::MdkData::At(dataDir);
            if (data == nullptr)
            {
                throw DirectoryNotFoundException(std::format("No MDK game files in {}", dataDir));
            }

            setenv(UserFolderVariable, userFolder.c_str(), Overwrite);
            stopped = Play(GameFiles{data, userFolder}, level, bench);
        }
        catch (...)
        {
            stopped = std::current_exception();
            Console::Error().WriteLine(Describe(stopped));
        }

        // No console on a phone: say why the game stopped (e.g. no Vulkan for SDL_GPU).
        if (stopped != nullptr)
        {
            Mdk::Android::SetupActivity::Stopped(std::format("{}\n\nThe details are in mdk.log.", Describe(stopped)));
        }
    }
}

// SDL's entry: argv ends with GameActivity.java's getArguments.
extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char* argv[])
{
    // An exception here aborts the process without a log: a missing argument quits instead.
    if (argc < SdlArgs)
    {
        return 1;
    }

    auto arg = [argc, argv](int fromEnd) { return std::string(argv[argc - fromEnd] != nullptr ? argv[argc - fromEnd] : ""); };
    auto level = 0;
    if (!Str::TryParseInt(arg(2), level))
    {
        level = 0;
    }

    Run(arg(4), arg(3), level, arg(1));
    return 0;
}
