#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Mdk.Core/TimeSpan.h"
#include "Mdk.Engine/Platform/Display.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Engine/Platform/Window.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/DevTools/DevConsole.h"
#include "Mdk.Game/DevTools/TechInfo.h"
#include "Mdk.Game/Flow/GameState.h"
#include "Mdk.Game/Flow/SaveGames.h"
#include "Mdk.Game/Flow/Screen.h"
#include "Mdk.Game/Flow/Settings.h"
#include "Mdk.Game/Flow/TestPresses.h"
#include "Mdk.Game/Menu/MainMenu.h"
#include "Mdk.Game/Menu/StatsScreen.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Mdk.Game/Viewer.h"

namespace Mdk::Engine::Audio
{
    class AudioDevice;
}

namespace Mdk::Formats
{
    class MdkData;
}

namespace Mdk::Game::DevTools
{
    class DevUi;
    class DevUiView;
    class GameCommands;
    class PerfLog;
}

namespace Mdk::Game::HdTextures
{
    struct HdOptions;
}

namespace Mdk::Game::Menu
{
    class Fonts;
    class SavePrompt;
    class Ui;
}

namespace Mdk::Game
{
    class SoakKeys;
}

namespace Mdk::Game::Flow
{
    // How the game starts (command line): the screen shown first.
    enum class Start { Menu, Level, Statistics, Briefing, EndMovie, Stream, Fall };

    // Whether the game shows its window.
    enum class Display { Window, Hidden };

    // The command line's choices: where the game starts, the first level's test options, the
    // statistics' test counts, a save to load or write, a screenshot.
    struct GameOptions
    {
        Flow::Start Start{};
        ViewerOptions Level;
        // Hidden: no window is shown or focused, frames are drawn off screen (tests).
        Flow::Display Display{};
        Menu::MenuPage Page{};
        // The menu shows the splash first even when it's started directly.
        bool Splash = false;
        // The statistics start at this page.
        std::optional<Menu::StatsScreen::Phase> Phase;
        // The statistics' counts: shots, hits, sniper rounds, sniper hits, kills, enemies, heads.
        std::vector<int> Counts;
        // The town flags' bits 31-29 for the debriefing.
        std::optional<int> Towns;
        // A save to load first, and a save written when the first level starts.
        std::optional<std::string> Load;
        std::optional<std::string> Save;
        // A screen other than the first level saves this frame after Wait seconds, then the game quits.
        std::optional<std::string> Screenshot;
        float Wait = 0;
        // Kurt's health in a stream started directly (--stream).
        std::optional<int> Health;
        // The look instead of the settings' (--enhanced, --original; tests).
        std::optional<Flow::Graphics> Graphics;
        // Gore instead of the settings' (the original's -bloodyes, -nobloodno).
        std::optional<bool> Gore;
        // The stereo layout instead of the settings' (--stereo; tests), and its eyes'
        // separation and convergence (--stereo-separation, --stereo-convergence; not saved).
        std::optional<Engine::Render::Stereo> Stereo;
        std::optional<float> StereoSeparation;
        std::optional<float> StereoConvergence;
        // The display's gamma instead of the settings' (--gamma; tests).
        std::optional<float> Gamma;
        // The enhanced look's cost instead of the OS's (--quality; tests, measurements).
        std::optional<Engine::Render::EffectQuality> Quality;
        // Keys pressed once at given times (--press; tests).
        std::shared_ptr<TestPresses> Presses;
        // Only these mods on, by folder (--mod; tests; saved only if the options change).
        std::optional<std::vector<std::string>> Mods;
        // Measure the frames after this many seconds of game time, waiting for the GPU each
        // frame, and print their costs at the end (--perf; tests).
        std::optional<float> Perf;
        // The GPU backend instead of the settings' (--gpu=d3d12|vulkan|metal|auto; tests).
        std::optional<std::string> Gpu;
        // A hidden window's frame size instead of 1280 x 960 (--size; tests, videos).
        std::optional<Engine::Platform::Resolution> HiddenSize;
        // A hidden window's frames through its swapchain too (--swapchain; tests).
        Engine::Render::HiddenFrames HiddenFrames{};
        // The sound mixed from the start saved as this WAV (--record-audio; with --mute, in step with --frames).
        std::optional<std::string> RecordAudio;
        // The platform's import of the game files again (Android: the folder picked again,
        // HD textures made on a PC with it); null where the game reads them in place.
        std::function<void()> Import;
    };

    // The game: its screens one after the other, as the original's game states.
    //   splash ─► menu ─new game─► briefing ──fall──► loading ─► level ─tornado─► stream ─► statistics
    //              ▲  └continue/save─► loading                     │                       save prompt
    //              │                                               │ died: LASTGAME           briefing ──fall──► next level
    //              └───────────────────────────────────────────────┘ event 81: end movies ─► splash ─► menu
    class Game
    {
    public:
        Game(const std::shared_ptr<Formats::MdkData>& data, GameOptions options);
        ~Game();

        Game(const Game&) = delete;
        Game& operator=(const Game&) = delete;

        // Runs the screens until the window closes or one quits.
        void Run();

        // --export-assets: the game's textures, 2D images and models for modding (AssetExport).
        static int ExportAssets(const std::shared_ptr<Formats::MdkData>& data, const std::string& folder);

        // --download-test: fetches a URL through the OS's HTTP library, prints its size and
        // SHA-256 (CI proves the native download works on each platform).
        static int DownloadTest(const std::string& url);

        // Makes the HD textures in the user folder without a window (--upscale-textures),
        // printing the progress; returns the exit code.
        static int UpscaleTextures(const std::shared_ptr<Formats::MdkData>& data, const HdTextures::HdOptions& options);

    private:
        // Where older builds kept settings and saves, under the local data folder.
        static constexpr const char* UserFolderName = "mdk-sdl";
        // The saves' folder in the user folder (SaveGames).
        static constexpr const char* SavesFolder = "saves";
        // Overrides the user folder (settings and saves), for tests.
        static constexpr const char* UserFolderVariable = "MDK_USER_DIR";
        static constexpr const char* LogFile = "mdk.log";
        static constexpr int TownShift = 29;
        // The soak test's frames are this many game steps (0.1 s): it runs faster.
        static constexpr int SoakSteps = 6;
        // The overlay's line between screens (the loading screen).
        static constexpr const char* Loading = "loading";
        // How often --upscale-textures prints its progress (ms).
        static constexpr int UpscalePoll = 1000;

        enum class TestRun { No, Yes };

        GameOptions _options;
        std::unique_ptr<Engine::Platform::Window> _window;
        std::unique_ptr<Engine::Render::Renderer> _renderer;
        std::unique_ptr<Engine::Audio::AudioDevice> _audio;
        Engine::Platform::Input _input;
        // The Ui's settings (C#: a local the Ui keeps); outlives the Ui.
        std::unique_ptr<Settings> _settings;
        std::shared_ptr<Menu::Ui> _ui;
        std::shared_ptr<GameState> _state = std::make_shared<GameState>();
        std::optional<SaveGames> _saves;
        Engine::Render::Renderer::Scope _scope{};
        // The console and the overlay, over every screen.
        std::shared_ptr<DevTools::DevUi> _dev;
        Engine::Platform::Typing _typing{};
        // The console as last seen: opening it shows a touch screen's keyboard, closing hides it.
        DevTools::ConsoleState _consoleSeen{};
        // The screen's typing as last seen (a save's name prompt).
        Engine::Platform::Typing _screenSeen{};
        std::shared_ptr<DevTools::GameCommands> _commands;
        // The soak test's random keys, on every screen; menu keys too when it starts in the
        // menus or the flow screens.
        std::shared_ptr<SoakKeys> _soak;
        bool _soakMenus = false;
        std::shared_ptr<IScreen> _screen;
        // The first level gets the command line's test options; later ones don't.
        bool _firstLevel = true;
        // The level shown takes its own screenshot (the first, with --level).
        bool _levelShot = false;
        // The level that ended, for its counts.
        std::shared_ptr<Viewer> _viewer;
        std::shared_ptr<DevTools::PerfLog> _perf;
        std::shared_ptr<DevTools::DevUiView> _devView;
        // The display as last logged: a change is logged again.
        DevTools::ScreenInfo _screenInfo{};

        // --gpu's backend, else the settings'.
        static Engine::Render::GpuBackend Backend(const std::optional<std::string>& name, const Settings& settings);

        // The program, the GPU and the look, in the log (and the console); the display's line
        // follows with the first frame.
        void LogTechInfo(const Settings& settings);

        // The display now; read every frame (no allocation).
        DevTools::ScreenInfo ScreenNow();

        // A changed display (size, mode, frame, VSync) is logged and shown by the overlay.
        void WatchScreen();

        // With VSync off or adaptive, frames wait out the frame limit (not in tests: their
        // frames are game steps).
        void LimitFrame(TimeSpan elapsed, TestRun test);

        // The first screen from the command line (a save to load first).
        Event FirstEvent();

        // The statistics' test counts (--counts, --towns).
        void ApplyTestCounts();

        // What follows a screen's event.
        void Handle(Event next);

        // The loading screen, then the level.
        void Play();

        // The level ended (the tornado is over): its counts are kept, then the stream or the menu.
        void AfterLevel(Scripts::ScriptRuntime::GameOver over);

        // The stream after the level, with Kurt's health. Started directly (--stream) its tube
        // is the one of the C library's first seed, as tests expect.
        void ShowStream(int health);

        // The stream is over: the statistics, or the last level.
        void AfterStream();

        // The next screen, created once the last one's resources are freed.
        void Show(const std::function<std::shared_ptr<IScreen>()>& create);

        // --mod: only these mods on (unknown ones named).
        void UseOnly(const std::vector<std::string>& mods);

        // Settings and saves live next to the executable (a portable install), or in the
        // user's data folder when it's protected (UserFolders). The first portable run
        // moves over what an older build kept in the local data folder.
        static std::string UserFolder();

        // The console a menu asked for opens (the game's options; phones have no Grave key).
        void OpenAskedConsole();

        // A touch screen's keyboard shows when the console or a screen's name prompt opens and
        // hides when both are closed.
        void UpdateTyping();

        // Shows or hides a touch screen's keyboard.
        void SetTyping(Engine::Platform::Typing typing);

        // A touch screen's tap on the open console: its close button or the game below close
        // it (the tap goes no further), its line shows the keyboard, its log hides it.
        void TouchConsole();

        // The output also goes to mdk.log in the user folder (phones have no console to read):
        // made anew each start. A folder it can't write to keeps the output on screen only.
        static void WriteLog(const std::string& folder);
    };

    // The save prompt alone (after LEVEL8), then the level.
    class SavePromptScreen : public IScreen
    {
    public:
        SavePromptScreen(Menu::Ui& ui, const SaveGames& saves, const SaveGame& save, const std::string& name);

        Event Frame(float elapsed, const std::optional<std::string>& screenshot) override;

        std::vector<std::string> Status() const override { return {"save prompt"}; }

        Engine::Platform::Typing Typing() const override;

        void Dispose() override {}

    private:
        Menu::Ui& _ui;
        std::shared_ptr<Menu::SavePrompt> _prompt;
    };
}
