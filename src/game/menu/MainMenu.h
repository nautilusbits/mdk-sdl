#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/DotNet.h"
#include "core/Dictionary.h"
#include "game/flow/Screen.h"

namespace Mdk::Engine::Audio
{
    class Sound;
}

namespace Mdk::Engine::Platform
{
    class Input;
}

namespace Mdk::Game::Flow
{
    class GameState;
    class SaveGames;
}

namespace Mdk::Game::Menu
{
    class Fonts;
    class IntroSplash;
    class MenuItems;
    class PalettedImage;
    class Slideshow;
    class Ui;
    class VideoPlayer;

    // How the main menu opens (command line): its first page.
    enum class MenuPage { Main, Options, Controls, Gamepad, BetaLevels, TouchLayout };

    // The main menu (main_menu.gd): MDK12.FLC plays behind it each time it opens (0x4260e4), its
    // last frame stays, then the slideshow; MDKOPT is the background when the video is missing.
    // MAINSONG plays, but not on the options page (OPTSONG). The INTRO1A splash comes first when
    // GameState.Splash.
    //   Continue      (the level Kurt last died in: LASTGAME)
    //   New Game
    //   Level: n      (where a new game starts, 1-6)
    //   Saved Game    (the list of saves)
    //   Beta Levels   (the 1996 demo's, when it's found)
    //   Options
    //   Quit          (Esc: "Really Quit?")
    class MainMenu : public Flow::IScreen
    {
    public:
        MainMenu(Ui& ui, Flow::GameState& state, Flow::SaveGames& saves, MenuPage first);

        Flow::Event Frame(float elapsed, const std::optional<std::string>& screenshot) override;

        std::vector<std::string> Status() const override;

        void Dispose() override;

    private:
        // The page shown, for Esc.
        enum class Page { Main, Quit, Other };

        static constexpr const char* MenuVideo = "MISC/FLIC/MDK12.FLC";
        static constexpr const char* Archive = "MISC/OPTIONS.BNI";
        static constexpr int PaletteSize = 768;
        // "Really Quit?" (0x403cf8): its title in row 3, Yes and No below; Y, J, O, S or T say
        // yes in the game's languages, N no.
        static constexpr int QuitRow = 3;
        static constexpr const char* YesKeys = "YJOST";
        static constexpr const char* NoKeys = "N";
        // SEETHEWHOLEGAME's keys: LEVEL3-8 at once, D the statistics with random counts below 100.
        static constexpr char DebugFirstLevel = '3';
        static constexpr char DebugLastLevel = '8';
        static constexpr char DebugStatistics = 'D';
        static constexpr char DebugFall = 'F';
        // The debug key F plays the fall before the fifth level of the order (LEVEL8).
        static constexpr int DebugFallIndex = 4;
        // The debug key S plays the stream after the first level of the order (LEVEL7).
        static constexpr char DebugStream = 'S';
        static constexpr int DebugStreamIndex = 0;
        static constexpr int DebugCountLimit = 100;
        // The page of the 1996 demo's levels (ShowBetaLevels): its title and the levels' names.
        static constexpr const char* BetaTitle = "Beta Levels";
        static const Dictionary<int, std::string>& BetaLevelNames();
        // The game's version, in the menu's bottom right corner (bug reports name it).
        static const std::vector<byte>& Version();
        static constexpr float VersionMargin = 6.f;

        Ui& _ui;
        Flow::GameState& _state;
        Flow::SaveGames& _saves;
        std::shared_ptr<Fonts> _fonts;
        std::shared_ptr<MenuItems> _items;
        std::shared_ptr<VideoPlayer> _video;
        std::shared_ptr<Slideshow> _slideshow;
        std::shared_ptr<IntroSplash> _splash;
        std::shared_ptr<PalettedImage> _background;
        std::shared_ptr<Engine::Audio::Sound> _song;
        int _songVoice = 0;
        int _levelIndex = 0;
        Page _page{};
        Flow::Event _next = Flow::Event::None;
        bool _videoStarted = false;

        void DrawVersion();

        void StartVideo();

        // Esc on the main page asks "Really Quit?", and there means no; the debug keys.
        void HandleKeys(Engine::Platform::Input& input);

        // The debug keys of SEETHEWHOLEGAME (0x426574, 0x433b50, 0x431b00).
        bool DebugKey(const std::string& typed);

        void ShowQuit();

        void ShowMain();

        // The saved games (SVOPT1, or SVOPT3 when there are none), with their level.
        void ShowSaves();

        // A save made before a level shows its briefing first; the others start the level.
        void Load(const std::string& name);

        // MAINSONG stops while the options play OPTSONG (0x42bb6c, 0x42bbc0).
        void ShowOptions();

        // The 1996 demo's levels (godot-mdk docs/beta96.md), a page of the port's own.
        void ShowBetaLevels();

        // The demo's levels start at once: it has no briefing or fall.
        void PlayBeta(int level);

        // The briefing, the fall, then the level.
        void NewGame();

        void NextLevel();

        std::string LevelText() const;

        // The page's items, for tests.
        void Print() const;
    };
}
