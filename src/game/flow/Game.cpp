#include "game/flow/Game.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <future>

#include "core/Cancellation.h"
#include "core/Console.h"
#include "core/DotNet.h"
#include "core/Files.h"
#include "core/Sha256.h"
#include "core/SpecialFolder.h"
#include "engine/audio/AudioDevice.h"
#include "engine/diagnostics/LogWriter.h"
#include "engine/diagnostics/Profiler.h"
#include "engine/platform/HttpDownload.h"
#include "engine/render/GpuBackend.h"
#include "formats/BetaDemo.h"
#include "formats/MdkData.h"
#include "game/dev_tools/CommandRegistry.h"
#include "game/dev_tools/ConsoleCommands.h"
#include "game/dev_tools/ConsoleView.h"
#include "game/dev_tools/DevSession.h"
#include "game/dev_tools/DevUi.h"
#include "game/dev_tools/GameCommands.h"
#include "game/dev_tools/PerfLog.h"
#include "game/fall/FallScreen.h"
#include "game/flow/LevelFlow.h"
#include "game/flow/LoadProgress.h"
#include "game/flow/UserFolders.h"
#include "game/hd_textures/HdGenerator.h"
#include "game/hd_textures/HdMenu.h"
#include "game/hud/TouchView.h"
#include "game/menu/EndMovie.h"
#include "game/menu/LoadingScreen.h"
#include "game/menu/SavePrompt.h"
#include "game/menu/Ui.h"
#include "game/mods/AssetExport.h"
#include "game/mods/ModCatalog.h"
#include "game/scripts/GameStats.h"
#include "game/SoakKeys.h"
#include "game/stream/StreamScreen.h"
#include "game/stream/WatcomRandom.h"

namespace Mdk::Game::Flow
{
    using Engine::Platform::Typing;
    using Engine::Platform::Visibility;

    namespace
    {
        using Clock = std::chrono::steady_clock;

        TimeSpan Since(Clock::time_point start)
        {
            return std::chrono::duration_cast<TimeSpan>(Clock::now() - start);
        }

        // Path.GetTempFileName: a new empty file of a unique name in the temp folder.
        std::string TempFileName()
        {
            static constexpr int Attempts = 100;
            auto seed = static_cast<int>(TickCount64());
            Mdk::Random random(seed);
            for (int i = 0; i < Attempts; i++)
            {
                auto path = Path::Combine(Path::GetTempPath(), std::format("tmp{:08X}.tmp", static_cast<uint>(random.Next())));
                if (File::Exists(path))
                {
                    continue;
                }

                File::WriteAllBytes(path, {});
                return path;
            }

            throw IOException("No temporary file name left");
        }
    }

    Engine::Render::GpuBackend Game::Backend(const std::optional<std::string>& name, const Settings& settings)
    {
        if (!name.has_value())
        {
            return settings.Backend;
        }

        if (auto backend = Engine::Render::GpuBackends::Parse(*name))
        {
            return *backend;
        }

        Console::Error().WriteLine(std::format("--gpu={}: d3d12, vulkan, metal or auto; using the settings'", *name));
        return settings.Backend;
    }

    void Game::LogTechInfo(const Settings& settings)
    {
        Console::WriteLine(DevTools::TechInfo::Build(DevTools::BuildInfo::Current()));
        Console::WriteLine(DevTools::TechInfo::Gpu(_renderer->Gpu()));
        std::vector<std::string> mods;
        for (const auto& mod : _ui->Mods->Enabled(settings))
        {
            mods.push_back(mod.Folder);
        }

        Console::WriteLine(DevTools::TechInfo::Look(settings.Graphics, mods));
    }

    DevTools::ScreenInfo Game::ScreenNow()
    {
        auto [width, height] = _window->Size();
        return DevTools::ScreenInfo{_window->Desktop(), Engine::Platform::Resolution{width, height}, _window->Fullscreen(),
            _window->Visibility(), _renderer->Target(), _renderer->SwapchainFormat(), _renderer->DepthFormat(),
            _renderer->Samples(), _renderer->VSync()};
    }

    void Game::WatchScreen()
    {
        auto now = ScreenNow();
        if (now == _screenInfo)
        {
            return;
        }

        _screenInfo = now;
        Console::WriteLine(DevTools::TechInfo::Display(now));
        Console::WriteLine(DevTools::TechInfo::Layout(_window->UnitsPerMm(), _window->SafeArea(), _renderer->RenderScale, now.Window, _renderer->Quality()));
        _devView->System = DevTools::TechInfo::Overlay(_renderer->Gpu(), now);
    }

    void Game::LimitFrame(TimeSpan elapsed, TestRun test)
    {
        if (test == TestRun::Yes || _renderer->VSync() == Engine::Render::VSync::On)
        {
            return;
        }

        Engine::Platform::FrameLimiter::Wait(elapsed, _ui->Settings().FrameLimit);
    }

    Game::Game(const std::shared_ptr<Formats::MdkData>& data, GameOptions options) : _options(std::move(options))
    {
        auto variable = GetEnv(UserFolderVariable);
        auto folder = variable.has_value() ? *variable : UserFolder();
        WriteLog(folder);
        _saves = SaveGames::In(folder);

        // The original deletes LASTGAME.SAV when it quits.
        _saves->Delete(SaveGames::LastGame);
        _settings = std::make_unique<Settings>(Settings::Load(Settings::PathIn(folder)));
        auto& settings = *_settings;
        auto visibility = _options.Display == Display::Hidden ? Visibility::Hidden : Visibility::Shown;
        auto size = visibility == Visibility::Shown ? settings.WindowSize : Settings::DefaultWindow;
        _window = std::make_unique<Engine::Platform::Window>("MDK", size.Width, size.Height, visibility);
        _renderer = std::make_unique<Engine::Render::Renderer>(*_window, Backend(_options.Gpu, settings));
        if (_options.HiddenSize.has_value())
        {
            _renderer->HiddenSize = *_options.HiddenSize;
        }

        _renderer->HiddenFrames = _options.HiddenFrames;

        _audio = std::make_unique<Engine::Audio::AudioDevice>(_options.Level.Sound == SoundMode::Muted ? Engine::Audio::Output::Muted : Engine::Audio::Output::Speakers);
        if (_options.RecordAudio.has_value())
        {
            _audio->Record(*_options.RecordAudio);
        }

        // Tests choose the look (saved only if the options change).
        if (_options.Graphics.has_value())
        {
            settings.Graphics = *_options.Graphics;
        }

        if (_options.Gore.has_value())
        {
            settings.Gore = *_options.Gore;
        }

        if (_options.Stereo.has_value())
        {
            settings.Stereo = *_options.Stereo;
        }

        if (_options.StereoSeparation.has_value())
        {
            settings.StereoSeparation = *_options.StereoSeparation;
        }

        if (_options.StereoConvergence.has_value())
        {
            settings.StereoConvergence = *_options.StereoConvergence;
        }

        if (_options.Gamma.has_value())
        {
            settings.Gamma = *_options.Gamma;
        }

        if (_options.Quality.has_value())
        {
            _renderer->SetQuality(*_options.Quality);
        }

        settings.Apply(*_audio, *_window, *_renderer, _input);
        // Older builds' HD textures become the HD textures mod.
        if (auto moved = HdTextures::HdGenerator::Migrate(folder); moved > 0)
        {
            Console::WriteLine(std::format("HD textures: {} images moved into mods/{}", moved, HdTextures::HdGenerator::ModFolder));
        }

        _ui = std::make_shared<Menu::Ui>(data, *_window, *_renderer, *_audio, _input, settings, folder);
        _ui->Import = _options.Import;
        _ui->TouchEditor = _options.Page == Menu::MenuPage::TouchLayout;
        if (_options.Mods.has_value())
        {
            UseOnly(*_options.Mods);
        }

        // The developer tools draw over whatever screen presents; their font outlives the screens.
        _commands = std::make_shared<DevTools::GameCommands>(*_ui, *_state, [this]() -> std::shared_ptr<DevTools::ILevelTarget>
            {
                auto viewer = std::dynamic_pointer_cast<Viewer>(_screen);
                return viewer != nullptr ? viewer->Commands() : nullptr;
            });
        auto registry = std::make_shared<DevTools::CommandRegistry>();
        DevTools::ConsoleCommands::Register(*registry, *_commands);
        _dev = std::make_shared<DevTools::DevUi>(
            std::make_shared<DevTools::DevConsole>(registry, _ui->Dev->History, _ui->Dev->Log, [](const std::string& line) { Console::WriteLine(line); }),
            [this]() { _ui->Dev->ToggleOverlay(); });
        auto small = Menu::Fonts(*_renderer, *_ui->Fti, nullptr, _ui->CanvasMods()).Small;
        _devView = std::make_shared<DevTools::DevUiView>(*_renderer, small);
        auto devView = _devView;
        devView->Console().CloseButton = _window->HasTouchScreen() ? DevTools::CloseButton::Shown : DevTools::CloseButton::Hidden;
        // The on-screen controls of a touch screen (Android), under the developer tools.
        auto touchView = std::make_shared<Hud::TouchView>(*_renderer, small);
        // Made once: a lambda made in the overlay's would be made every frame.
        std::function<std::vector<std::string>()> status = [this]()
        {
            return _screen != nullptr ? _screen->Status() : std::vector<std::string>{Loading};
        };
        _renderer->Overlay = [this, touchView, devView, status]()
        {
            touchView->Draw(*_window);
            devView->Draw(*_dev, *_ui->Dev, status);
        };
        _scope = _renderer->Mark();
        LogTechInfo(settings);
        _soak = _options.Level.Soak.has_value() ? std::make_shared<SoakKeys>(*_options.Level.Soak) : nullptr;
        _soakMenus = _options.Start == Start::Menu || _options.Start == Start::Statistics || _options.Start == Start::Briefing
            || _options.Start == Start::EndMovie;
        if (_options.Perf.has_value())
        {
            _perf = std::make_shared<DevTools::PerfLog>(*_options.Perf);
            _renderer->Sync = Engine::Render::GpuSync::Wait;
        }
    }

    void Game::Run()
    {
        auto first = FirstEvent();
        if (first == Event::Quit)
        {
            return;
        }

        Handle(first);
        auto test = _options.Screenshot.has_value() || _options.Level.Frames.has_value();
        auto step = _soak != nullptr ? Viewer::Step * SoakSteps
            : _options.Level.FrameRate.has_value() ? 1.f / static_cast<float>(*_options.Level.FrameRate)
            : Viewer::Step;
        auto clock = Clock::now();
        auto time = 0.f;
        auto frameStep = 0;
        while (_screen != nullptr && _window->PumpEvents(_input))
        {
            auto elapsed = test ? step : static_cast<float>(std::chrono::duration<double>(Clock::now() - clock).count());
            clock = Clock::now();
            time += elapsed;
            if (_soak != nullptr)
            {
                _soak->Hold(_input, time);
            }

            if (_soakMenus && _soak != nullptr)
            {
                _soak->PressMenu(_input, time);
            }

            if (_options.Presses != nullptr)
            {
                _options.Presses->Apply(_input, time);
            }

            // The open console has the keys: the screen runs on without them.
            OpenAskedConsole();
            TouchConsole();
            _dev->Update(_input, elapsed);
            UpdateTyping();
            auto viewer = dynamic_cast<Viewer*>(_screen.get());
            if (viewer == nullptr || viewer->ConsoleReady())
            {
                _dev->RunOnce(_options.Level.Console);
            }

            auto shot = test && !_levelShot && time >= _options.Wait ? _options.Screenshot : std::nullopt;

            // --frames of a screen before the level (the fall): the level's own are the viewer's.
            auto frames = _options.Start != Start::Level && time >= _options.Wait ? _options.Level.Frames : std::nullopt;
            auto frame = frames.has_value() ? frames->PathOf(frameStep++) : std::nullopt;
            auto next = _screen->Frame(elapsed * _commands->TimeScale(), shot.has_value() ? shot : frame);

            // A console command leaves the screen (map, load, quit).
            auto command = _commands->TakeNext();
            if (command != Event::None)
            {
                next = command;
            }

            {
                auto measure = _ui->Dev->Profiler.Measure(Engine::Diagnostics::Section::Audio);
                _audio->Update(elapsed);
            }

            _ui->Dev->Profiler.EndFrame();
            WatchScreen();
            LimitFrame(Since(clock), test ? TestRun::Yes : TestRun::No);
            if (_perf != nullptr)
            {
                _perf->Frame(time, _ui->Dev->Profiler, _renderer->Stats());
            }

            if (shot.has_value())
            {
                Console::WriteLine(std::format("Saved {}", *shot));
                break;
            }

            if (frames.has_value() && frames->IsLast(frameStep - 1))
            {
                break;
            }

            if (next != Event::None)
            {
                Handle(next);
            }
        }

        if (_perf == nullptr)
        {
            return;
        }

        for (const auto& line : _perf->Report())
        {
            Console::WriteLine(line);
        }
    }

    Event Game::FirstEvent()
    {
        ApplyTestCounts();
        if (_options.Load.has_value())
        {
            const auto& name = *_options.Load;
            auto save = _saves->Read(name);
            if (!save.has_value())
            {
                Console::Error().WriteLine(std::format("No saved game {}", name));
                return Event::Quit;
            }

            _state->Load(*save);
            Console::WriteLine(std::format("Loaded game {}: level {}", name, save->Level));
            return save->Kind == SaveKind::BeforeLevel ? Event::Briefing : Event::Play;
        }

        _state->Level = _options.Level.Level;
        switch (_options.Start)
        {
            case Start::Level:
                return Event::Play;
            case Start::Statistics:
                return Event::Statistics;
            case Start::Briefing:
                return Event::Briefing;
            case Start::EndMovie:
                return Event::GameFinished;
            case Start::Stream:
                return Event::Stream;
            case Start::Fall:
                return Event::Fall;
            default:
                break;
        }

        _state->Splash = _options.Splash;
        return Event::Menu;
    }

    void Game::ApplyTestCounts()
    {
        static constexpr int CountsLength = 7;
        auto& stats = *_state->Stats;
        auto counts = _options.Counts;
        counts.resize(counts.size() + CountsLength, 0);
        if (!_options.Counts.empty())
        {
            stats.Shots = counts[0];
            stats.ShotHits = counts[1];
            stats.SniperShots = counts[2];
            stats.SniperHits = counts[3];
            stats.Kills = counts[4];
            stats.Enemies = counts[5];
            stats.HeadShots = counts[6];
        }

        if (_options.Towns.has_value())
        {
            stats.TownFlags = *_options.Towns << TownShift;
        }
    }

    void Game::Handle(Event next)
    {
        switch (next)
        {
            case Event::Quit:
                Show([]() { return nullptr; });
                break;
            case Event::Menu:
                Show([this]() { return std::make_shared<Menu::MainMenu>(*_ui, *_state, *_saves, _firstLevel ? _options.Page : Menu::MenuPage::Main); });
                break;
            case Event::Play:
                Play();
                break;
            case Event::Briefing:
                Show([this]() { return std::make_shared<Menu::StatsScreen>(*_ui, *_state, *_saves, Menu::StatsPages::Briefing, std::nullopt); });
                break;
            case Event::Statistics:
                Show([this]()
                    {
                        return std::make_shared<Menu::StatsScreen>(*_ui, *_state, *_saves, Menu::StatsPages::All,
                            _firstLevel ? _options.Phase : std::nullopt);
                    });
                break;
            case Event::LevelEnded:
                AfterLevel(Scripts::ScriptRuntime::GameOver::No);
                break;
            case Event::GameOver:
                AfterLevel(Scripts::ScriptRuntime::GameOver::Yes);
                break;
            case Event::KurtDied:
                // The level is saved as LASTGAME; the menu's "Continue" starts it again (not the 1996 demo's).
                if (!Formats::BetaDemo::IsBeta(_state->Level))
                {
                    _saves->Write(SaveGames::LastGame, _state->Save(SaveKind::LevelStart));
                }

                _state->Splash = true;
                Handle(Event::Menu);
                break;
            case Event::GameFinished:
                Show([this]() { return std::make_shared<Menu::EndMovie>(*_ui); });
                _state->Splash = true;
                break;
            case Event::Stream:
                ShowStream(_options.Health.value_or(SaveGame::FullHealth));
                break;
            case Event::Fall:
                Show([this]()
                    {
                        ViewerOptions later;
                        later.Sound = _options.Level.Sound;
                        return std::make_shared<Fall::FallScreen>(*_ui, *_state, _firstLevel ? _options.Level : later);
                    });
                break;
            case Event::StreamEnded:
                AfterStream();
                break;
            default:
                break;
        }

        _firstLevel = false;
    }

    void Game::Play()
    {
        // The loading screen stays (freed with the level) to show how far the level got.
        Show([]() { return nullptr; });
        Menu::LoadingScreen screen(*_ui, _state->Level);
        screen.Show(0.f);
        ViewerOptions later;
        later.Sound = _options.Level.Sound;
        auto test = _firstLevel ? _options.Level : later;
        _levelShot = _firstLevel && _options.Start == Start::Level && test.Screenshot.has_value();
        auto loading = Clock::now();
        test.Level = _state->Level;
        LoadProgress progress([&screen](float done) { screen.Show(done); });
        _viewer = std::make_shared<Viewer>(*_ui, test, *_state, progress);
        if (_perf != nullptr)
        {
            _perf->Loaded(Since(loading));
        }

        _screen = _viewer;
        if (_firstLevel && _options.Save.has_value() && _saves->Write(*_options.Save, _state->Save(SaveKind::LevelStart)))
        {
            Console::WriteLine(std::format("Saved game {}: level {}", *_options.Save, _state->Level));
        }
    }

    void Game::AfterLevel(Scripts::ScriptRuntime::GameOver over)
    {
        if (_viewer != nullptr)
        {
            _state->Stats = _viewer->Stats();
        }

        if (LevelFlow::AfterLevel(_state->Level, over) == LevelFlow::After::Menu)
        {
            Handle(Event::Menu);
            return;
        }

        ShowStream(_viewer != nullptr ? _viewer->Health() : SaveGame::FullHealth);
    }

    void Game::ShowStream(int health)
    {
        auto direct = _firstLevel && _options.Start == Start::Stream;
        auto seed = direct ? Stream::WatcomRandom::DefaultSeed : static_cast<uint>(TickCount64());
        Show([this, health, seed]()
            {
                return std::make_shared<Stream::StreamScreen>(*_ui, _state->Level, health, std::make_shared<Stream::WatcomRandom>(seed));
            });
    }

    void Game::AfterStream()
    {
        auto level = _state->Level;
        switch (LevelFlow::AfterLevel(level, Scripts::ScriptRuntime::GameOver::No))
        {
            case LevelFlow::After::Statistics:
                Handle(Event::Statistics);
                break;
            case LevelFlow::After::LastLevel:
            {
                // LEVEL5 follows at once, after a save prompt (kind 3, "6"), with the stream's health.
                auto stream = std::dynamic_pointer_cast<Stream::StreamScreen>(_screen);
                _state->EnterLastLevel(stream != nullptr ? stream->Health() : SaveGame::FullHealth);
                Show([this]()
                    {
                        return std::make_shared<SavePromptScreen>(*_ui, *_saves, _state->Save(SaveKind::LevelStart), LevelFlow::SaveName(_state->Level));
                    });
                break;
            }
            default:
                Handle(Event::Menu);
                break;
        }
    }

    void Game::Show(const std::function<std::shared_ptr<IScreen>()>& create)
    {
        if (_screen != nullptr)
        {
            _screen->Dispose();
        }

        _screen = nullptr;
        // Only a level's Kurt snipes: the scope's bindings don't carry over to the next screen.
        _input.Scoped = false;
        _levelShot = false;
        _audio->StopAll();
        _renderer->Release(_scope);
        _screen = create();
    }

    int Game::ExportAssets(const std::shared_ptr<Formats::MdkData>& data, const std::string& folder)
    {
        Console::WriteLine(std::format("Exported to {}: {}", folder, Mods::AssetExport::Run(data, folder)));
        return 0;
    }

    int Game::DownloadTest(const std::string& url)
    {
        auto path = TempFileName();
        try
        {
            Engine::Platform::HttpDownload::Save(url, path, nullptr, CancellationToken::None());
            std::ifstream file(path, std::ios::binary);
            auto length = File::Length(path);
            Console::WriteLine(std::format("Downloaded {} bytes, SHA-256 {}", length, Convert::ToHexStringLower(Sha256::HashData(file))));
            file.close();
            File::Delete(path);
            return 0;
        }
        catch (const IOException& e)
        {
            Console::Error().WriteLine(std::format("Download failed: {}", e.what()));
            File::Delete(path);
            return 1;
        }
        catch (...)
        {
            File::Delete(path);
            throw;
        }
    }

    int Game::UpscaleTextures(const std::shared_ptr<Formats::MdkData>& data, const HdTextures::HdOptions& options)
    {
        // The upscaler's builds are desktop-only.
        if (!HdTextures::HdMenu::CanMake(HdTextures::HdMenu::Current()))
        {
            Console::Error().WriteLine("HD textures can't be made on this platform");
            return 1;
        }

        auto variable = GetEnv(UserFolderVariable);
        auto folder = variable.has_value() ? *variable : UserFolder();
        auto source = Settings::Load(Settings::PathIn(folder)).Upscaler();
        HdTextures::HdProgress progress;
        auto withSource = options;
        withSource.Source = source;
        auto run = std::async(std::launch::async, [&data, &folder, &withSource, &progress]()
            {
                return HdTextures::HdGenerator::Run(data, folder, withSource, progress, CancellationToken::None());
            });
        std::string last;
        while (run.wait_for(std::chrono::milliseconds(UpscalePoll)) != std::future_status::ready)
        {
            auto line = progress.Describe();
            if (line != last)
            {
                Console::WriteLine(line);
                last = line;
            }
        }

        std::string result;
        try
        {
            result = run.get();
        }
        catch (const std::exception& e)
        {
            Console::Error().WriteLine(std::format("HD textures failed: {}", e.what()));
            return 1;
        }

        Console::WriteLine(std::format("HD textures: {}", result));
        return 0;
    }

    void Game::UseOnly(const std::vector<std::string>& mods)
    {
        for (const auto& mod : mods)
        {
            auto known = std::any_of(_ui->Mods->Mods.begin(), _ui->Mods->Mods.end(),
                [&mod](const Mods::ModInfo& f) { return Str::EqualsIgnoreCase(f.Folder, mod); });
            if (known)
            {
                continue;
            }

            Console::Error().WriteLine(std::format("No mod {} in {}", mod, Mods::ModCatalog::FolderIn(_ui->UserFolder())));
        }

        _ui->Mods->Only(_ui->Settings(), mods);
    }

    std::string Game::UserFolder()
    {
        auto folder = UserFolders::Choose(Path::BaseDirectory(), UserFolders::Personal());
        if (folder != Path::BaseDirectory())
        {
            return folder;
        }

        auto old = Path::Combine(Environment::GetFolderPath(Environment::SpecialFolder::LocalApplicationData), UserFolderName);
        if (File::Exists(Settings::PathIn(folder)) || !File::Exists(Settings::PathIn(old)))
        {
            return folder;
        }

        File::Copy(Settings::PathIn(old), Settings::PathIn(folder));
        auto oldSaves = Path::Combine(old, SavesFolder);
        if (!Directory::Exists(oldSaves))
        {
            return folder;
        }

        Directory::CreateDirectory(Path::Combine(folder, SavesFolder));
        auto saves = Path::GetFullPath(Path::Combine(folder, SavesFolder));
        for (const auto& file : Directory::GetFiles(oldSaves))
        {
            File::Copy(file, Path::Combine(saves, Path::GetFileName(file)), false);
        }

        return folder;
    }

    void Game::OpenAskedConsole()
    {
        if (!_ui->Dev->ConsoleAsked)
        {
            return;
        }

        _ui->Dev->ConsoleAsked = false;
        if (_dev->Console().State() == DevTools::ConsoleState::Closed)
        {
            _dev->Console().Toggle();
        }
    }

    void Game::UpdateTyping()
    {
        auto state = _dev->Console().State();
        auto screen = _screen != nullptr ? _screen->Typing() : Typing::Off;
        if (state == _consoleSeen && screen == _screenSeen)
        {
            return;
        }

        _consoleSeen = state;
        _screenSeen = screen;
        SetTyping(state == DevTools::ConsoleState::Open || screen == Typing::On ? Typing::On : Typing::Off);
    }

    void Game::SetTyping(Typing typing)
    {
        if (typing == _typing)
        {
            return;
        }

        _typing = typing;
        _window->SetTyping(typing);
    }

    void Game::TouchConsole()
    {
        if (!_window->HasTouchScreen() || _dev->Console().State() != DevTools::ConsoleState::Open
            || !_input.WasClicked(Engine::Platform::Pointer::Left))
        {
            return;
        }

        auto point = _renderer->CanvasPoint(_input.PointerX(), _input.PointerY());
        switch (_devView->Console().Spot(_dev->Console(), point))
        {
            case DevTools::ConsoleSpot::Close:
            case DevTools::ConsoleSpot::Below:
                _dev->Console().Press(DevTools::EditKey::Close);
                _input.Withhold();
                break;
            case DevTools::ConsoleSpot::Line:
                SetTyping(Typing::On);
                break;
            case DevTools::ConsoleSpot::Log:
                SetTyping(Typing::Off);
                break;
        }
    }

    void Game::WriteLog(const std::string& folder)
    {
        try
        {
            Directory::CreateDirectory(folder);
            Engine::Diagnostics::Log::Capture(Engine::Diagnostics::Log::Lines).WriteTo(Path::Combine(folder, LogFile));
        }
        catch (const IOException& e)
        {
            Console::Error().WriteLine(std::format("No log file in {}: {}", folder, e.what()));
        }
    }

    Game::~Game()
    {
        Engine::Diagnostics::Log::Capture(Engine::Diagnostics::Log::Lines).Close();
        if (_screen != nullptr)
        {
            _screen->Dispose();
        }

        // C#'s Dispose order: the screen, then the audio, the renderer and the window; what holds
        // GPU resources or the Ui goes before them.
        _renderer->Overlay = nullptr;
        _screen.reset();
        _viewer.reset();
        _devView.reset();
        _dev.reset();
        _commands.reset();
        _ui.reset();
        _audio.reset();
        _renderer.reset();
        _window.reset();
    }

    SavePromptScreen::SavePromptScreen(Menu::Ui& ui, const SaveGames& saves, const SaveGame& save, const std::string& name)
        : _ui(ui),
          _prompt(std::make_shared<Menu::SavePrompt>(ui, std::make_shared<Menu::Fonts>(ui.Renderer(), *ui.Fti, nullptr, ui.CanvasMods()),
              saves, save, name))
    {
    }

    Event SavePromptScreen::Frame(float elapsed, const std::optional<std::string>& screenshot)
    {
        _ui.View->Layout(Menu::ScreenView::Fit::Inside);
        _prompt->Update(_ui.Input(), elapsed);
        _prompt->Draw();
        _ui.Present(screenshot);
        return _prompt->Closed() ? Event::Play : Event::None;
    }

    Typing SavePromptScreen::Typing() const
    {
        return _prompt->Typing();
    }
}
