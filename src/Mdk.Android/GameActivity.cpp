// The game: SDL's activity (Java GameActivity: window, input, audio, the Vulkan surface) runs
// SDL_main on SDL's thread with the files SetupActivity found, from the main menu. Settings and
// saves live in the MDK folder played from in place, else in the app's external files folder.
// Options' "Import from folder" and the message why the game stopped show the setup screen
// over the game.
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

#include "Mdk.Android/JavaBridge.h"
#include "Mdk.Android/SetupActivity.h"
#include "Mdk.Core/Console.h"
#include "Mdk.Core/DotNet.h"
#include "Mdk.Engine/Diagnostics/LogWriter.h"
#include "Mdk.Formats/MdkData.h"
#include "Mdk.Game/Flow/Game.h"
#include "Mdk.Game/Viewer.h"

using namespace Mdk;
using namespace Mdk::Game;
using namespace Mdk::Game::Flow;
using Mdk::Android::JavaBridge;
using Mdk::Engine::Diagnostics::Log;

namespace
{
    // SDL's argv ends with getArguments: the data folder, the user folder, the level.
    constexpr int SdlArgs = 3;
    // The level the menu's "new game" would start with otherwise (unused from the menu).
    constexpr int DefaultLevel = 7;
    // The game's user folder (settings, saves): see Game.
    constexpr const char* UserFolderVariable = "MDK_USER_DIR";
    constexpr int Overwrite = 1;

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

    // The game until it quits; an exception is logged (mdk.log still open) before the game is
    // destroyed, and returned.
    std::exception_ptr Play(const GameFiles& found, int level)
    {
        // Played in place, the user folder is the MDK folder: Import picks another one.
        auto inPlace = found.UserFolder == found.Data->Dir() ? std::optional<std::string>(found.UserFolder) : std::nullopt;
        GameOptions options;
        options.Start = StartScreen(level);
        options.Level = StartLevel(level);
        options.Splash = true;
        options.Import = [inPlace] { Mdk::Android::SetupActivity::Reimport(inPlace); };
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
    void Run(const std::string& dataDir, const std::string& userFolder, int level)
    {
        // The log keeps what's printed from here: mdk.log gets it once the user folder is known.
        Log::Capture(Log::Lines);
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
            stopped = Play(GameFiles{data, userFolder}, level);
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

    auto level = 0;
    if (!Str::TryParseInt(argv[argc - 1], level))
    {
        level = 0;
    }

    Run(argv[argc - 3] != nullptr ? argv[argc - 3] : "", argv[argc - 2] != nullptr ? argv[argc - 2] : "", level);
    return 0;
}
