#include "game/menu/MainMenu.h"

#include <algorithm>
#include <format>
#include <string_view>

#include "core/Console.h"
#include "engine/audio/AudioDevice.h"
#include "engine/platform/Input.h"
#include "formats/BetaDemo.h"
#include "formats/Bni.h"
#include "formats/Fti.h"
#include "formats/MdkData.h"
#include "formats/Texture.h"
#include "game/dev_tools/TechInfo.h"
#include "game/flow/GameState.h"
#include "game/flow/SaveGames.h"
#include "game/hud/FontView.h"
#include "game/menu/IntroSplash.h"
#include "game/menu/MenuItems.h"
#include "game/menu/Slideshow.h"
#include "game/menu/Ui.h"
#include "game/menu/VideoPlayer.h"
#include "game/scripts/GameStats.h"

namespace Mdk::Game::Menu
{
    using Engine::Audio::Bus;
    using Engine::Audio::Looping;
    using Engine::Platform::Input;
    using Engine::Platform::MenuKey;
    using Flow::Event;
    using Flow::GameState;
    using Flow::SaveGames;
    using Flow::SaveKind;

    namespace
    {
        // C#'s Page.ToString() for the status line.
        const char* NameOf(int page)
        {
            static constexpr const char* Names[] = {"Main", "Quit", "Other"};
            return Names[page];
        }

        // C#'s typed.Any(keys.Contains).
        bool AnyOf(const std::string& typed, std::string_view keys)
        {
            return std::any_of(typed.begin(), typed.end(), [keys](char c) { return keys.find(c) != std::string_view::npos; });
        }
    }

    const Dictionary<int, std::string>& MainMenu::BetaLevelNames()
    {
        static const Dictionary<int, std::string> names = []
        {
            Dictionary<int, std::string> d;
            d[1] = "96 Level 1: City";
            d[3] = "96 Level 3: Wheel Boss";
            d[6] = "96 Level 6: Olympus";
            return d;
        }();
        return names;
    }

    const std::vector<byte>& MainMenu::Version()
    {
        static const std::vector<byte> version = []
        {
            auto text = DevTools::BuildInfo::Current().Version;
            return std::vector<byte>(text.begin(), text.end());
        }();
        return version;
    }

    MainMenu::MainMenu(Ui& ui, GameState& state, SaveGames& saves, MenuPage first)
        : _ui(ui), _state(state), _saves(saves)
    {
        _fonts = std::make_shared<Fonts>(ui.Renderer(), *ui.Fti, nullptr, ui.CanvasMods());
        _items = std::make_shared<MenuItems>(ui, _fonts);
        _video = std::make_shared<VideoPlayer>(ui);
        _slideshow = std::make_shared<Slideshow>(ui, *_video);
        _video->Finished = [slideshow = _slideshow.get()] { slideshow->Start(); };
        auto options = Formats::Bni::Load(ui.Data()->PathOf(Archive));

        // MDKOPT: a 768-byte palette, then a 600 x 360 image.
        if (options->Has("MDKOPT"))
        {
            const auto& entry = options->Entries().At("MDKOPT");
            const auto& bytes = *options->Bytes();
            std::vector<byte> palette(bytes.begin() + entry.Offset, bytes.begin() + entry.Offset + PaletteSize);
            _background = PalettedImage::Of(ui.Renderer(), *Formats::Texture::Parse("MDKOPT", bytes, entry.Offset + PaletteSize), palette, ui.CanvasMods());
        }

        if (state.Splash)
        {
            _splash = std::make_shared<IntroSplash>(ui, *options);
        }

        state.Splash = false;
        _song = Ui::SoundOf(*options, "MAINSONG", Looping::Forever);
        _songVoice = ui.Play(_song, Bus::Music);
        _levelIndex = Math::Max(GameState::IndexOf(state.Level), 0);
        ShowMain();
        if (first == MenuPage::Options)
        {
            ShowOptions();
        }
        else if (first == MenuPage::Controls)
        {
            _items->ShowControls([this] { ShowOptions(); });
        }
        else if (first == MenuPage::Gamepad)
        {
            _items->ShowGamepad([this] { ShowOptions(); });
        }
        else if (first == MenuPage::TouchLayout)
        {
            ShowOptions();
            _items->ShowTouchLayout([this] { _items->ShowTouch([this] { ShowOptions(); }); });
        }
        else if (first == MenuPage::BetaLevels && ui.Beta != nullptr)
        {
            ShowBetaLevels();
        }
    }

    Event MainMenu::Frame(float elapsed, const std::optional<std::string>& screenshot)
    {
        auto& input = _ui.Input();
        _ui.View->Layout(ScreenView::Fit::Inside);
        if (_splash != nullptr && !_splash->Done())
        {
            _splash->Update(input, elapsed);
            _splash->Draw();
            _ui.Present(screenshot);
            return Event::None;
        }

        StartVideo();
        _video->Update(elapsed);
        _slideshow->Update(elapsed);
        if (input.AnyPressed() || input.PointerMoved())
        {
            _slideshow->Reset();
        }

        _items->Visible = _slideshow->ItemsShown();
        HandleKeys(input);
        _items->Update(elapsed);
        if (!_video->Playing() && !_video->Still().has_value())
        {
            if (_background != nullptr)
            {
                _background->Draw(*_ui.View, Vector2::Zero());
            }
        }

        _video->Draw();
        _items->Draw();
        DrawVersion();
        _ui.Present(screenshot);
        return _next;
    }

    std::vector<std::string> MainMenu::Status() const
    {
        if (_splash != nullptr && !_splash->Done())
        {
            return {"intro splash"};
        }

        if (!_slideshow->ItemsShown())
        {
            return {"main menu: slideshow"};
        }

        auto page = NameOf(static_cast<int>(_page));
        return {_video->Playing() ? std::format("main menu: {}, video", page) : std::format("main menu: {}", page)};
    }

    void MainMenu::DrawVersion()
    {
        if (!_slideshow->ItemsShown())
        {
            return;
        }

        auto& font = *_fonts->Small;
        auto x = ScreenView::Size.X - VersionMargin - static_cast<float>(font.Width(Version()));
        _ui.View->Text(font, Version(), x, ScreenView::Size.Y - VersionMargin);
    }

    void MainMenu::StartVideo()
    {
        if (_videoStarted)
        {
            return;
        }

        _videoStarted = true;
        _video->Play(MenuVideo);
    }

    void MainMenu::HandleKeys(Input& input)
    {
        if (!_items->Visible)
        {
            return;
        }

        auto typed = Str::ToUpper(input.Typed());
        if (_page == Page::Main && _state.DebugKeys && DebugKey(typed))
        {
            return;
        }

        if (_page == Page::Main && input.WasPressed(MenuKey::Back))
        {
            ShowQuit();
        }
        else if (_page == Page::Quit && AnyOf(typed, YesKeys))
        {
            _next = Event::Quit;
        }
        else if (_page == Page::Quit && (AnyOf(typed, NoKeys) || input.WasPressed(MenuKey::Back)))
        {
            ShowMain();
        }
    }

    bool MainMenu::DebugKey(const std::string& typed)
    {
        for (auto c : typed)
        {
            if (c >= DebugFirstLevel && c <= DebugLastLevel)
            {
                _state.Level = c - '0';
                _next = Event::Play;
                return true;
            }

            if (c == DebugFall)
            {
                _state.Level = GameState::Order[DebugFallIndex];
                _next = Event::Fall;
                return true;
            }

            if (c == DebugStream)
            {
                _state.Level = GameState::Order[DebugStreamIndex];
                _next = Event::Stream;
                return true;
            }

            if (c != DebugStatistics)
            {
                continue;
            }

            auto& stats = *_state.Stats;
            // Random.Shared: any clock-seeded sequence will do.
            static Random random;
            stats.Shots = random.Next(DebugCountLimit);
            stats.ShotHits = random.Next(DebugCountLimit);
            stats.SniperShots = random.Next(DebugCountLimit);
            stats.SniperHits = random.Next(DebugCountLimit);
            stats.HeadShots = random.Next(DebugCountLimit);
            stats.Enemies = random.Next(DebugCountLimit);
            stats.Kills = random.Next(DebugCountLimit);
            _next = Event::Statistics;
            return true;
        }

        return false;
    }

    void MainMenu::ShowQuit()
    {
        _items->Clear();
        _page = Page::Quit;
        _items->FirstRow = QuitRow;
        _items->AddTitle(_ui.Fti->GetText("ABORT1", "Really Quit?"));
        _items->AddItem(_ui.Fti->GetText("ABORT2", "Yes"), [this] { _next = Event::Quit; });
        _items->AddItem(_ui.Fti->GetText("ABORT3", "No"), [this] { ShowMain(); });
        Print();
    }

    void MainMenu::ShowMain()
    {
        _items->Clear();
        _page = Page::Main;
        if (_saves.Read(SaveGames::LastGame).has_value())
        {
            _items->AddItem(_ui.Fti->GetText("OPT0", "Continue"), [this] { Load(SaveGames::LastGame); });
        }

        _items->AddItem(_ui.Fti->GetText("OPT1", "New Game"), [this] { NewGame(); });
        _items->AddItem(LevelText(), [this] { NextLevel(); });
        _items->AddItem(_ui.Fti->GetText("OPT2", "Saved Game"), [this] { ShowSaves(); });
        if (_ui.Beta != nullptr)
        {
            _items->AddItem(BetaTitle, [this] { ShowBetaLevels(); });
        }

        _items->AddItem(_ui.Fti->GetText("OPT3", "Options"), [this] { ShowOptions(); });
        _items->AddItem(_ui.Fti->GetText("OPT4", "Quit"), [this] { _next = Event::Quit; });

        // The main page is a column at the view's left edge (0x4265c0).
        _items->Alignment = MenuItems::Align::Left;
        Print();
    }

    void MainMenu::ShowSaves()
    {
        _items->Clear();
        _page = Page::Other;
        auto names = _saves.List();
        auto title = _ui.Fti->GetText(!names.empty() ? "SVOPT1" : "SVOPT3", "Saved games");
        _items->AddTitle(title.substr(0, title.find('\n')));
        for (const auto& name : names)
        {
            auto save = _saves.Read(name);
            if (!save.has_value())
            {
                _items->AddDisabled(std::format("{}  ({})", name, _ui.Fti->GetText("SVBAD", "Invalid")));
                continue;
            }

            _items->AddItem(std::format("{}  (Level {})", name, GameState::IndexOf(save->Level) + 1), [this, name] { Load(name); });
        }

        _items->AddItem("Back", [this] { ShowMain(); });
        Print();
    }

    void MainMenu::Load(const std::string& name)
    {
        auto save = _saves.Read(name);
        if (!save.has_value())
        {
            return;
        }

        _state.Load(*save);
        Console::WriteLine(std::format("Loaded game {}: level {}", name, save->Level));
        _next = save->Kind == SaveKind::BeforeLevel ? Event::Briefing : Event::Play;
    }

    void MainMenu::ShowOptions()
    {
        _page = Page::Other;
        _ui.Audio().Stop(_songVoice);
        _items->ShowOptions([this]
        {
            _songVoice = _ui.Play(_song, Bus::Music);
            ShowMain();
        });
    }

    void MainMenu::ShowBetaLevels()
    {
        _items->Clear();
        _page = Page::Other;
        _items->AddTitle(BetaTitle);
        for (auto level : Formats::BetaDemo::Levels)
        {
            _items->AddItem(BetaLevelNames().At(level), [this, level] { PlayBeta(level); });
        }

        _items->AddItem("Back", [this] { ShowMain(); });
        Print();
    }

    void MainMenu::PlayBeta(int level)
    {
        _state.NewGame(0);
        _state.Level = Formats::BetaDemo::NumberOf(level);
        _state.Carry = std::nullopt;
        _next = Event::Play;
    }

    void MainMenu::NewGame()
    {
        _state.NewGame(_levelIndex);
        _next = Event::Briefing;
    }

    void MainMenu::NextLevel()
    {
        _levelIndex = (_levelIndex + 1) % static_cast<int>(GameState::Order.size());
        ShowMain();
        _items->Select(_page == Page::Main && _saves.Read(SaveGames::LastGame).has_value() ? 2 : 1);
    }

    std::string MainMenu::LevelText() const
    {
        return std::format("Level: {}", _levelIndex + 1);
    }

    void MainMenu::Print() const
    {
        Console::WriteLine(std::format("Menu: {}", _items->Describe()));
    }

    void MainMenu::Dispose()
    {
        _video->Stop();
        _items->Clear();
    }
}
