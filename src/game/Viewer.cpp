#include "game/Viewer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <stdexcept>

#include "core/Console.h"
#include "core/DotNet.h"
#include "engine/audio/AudioDevice.h"
#include "engine/diagnostics/Profiler.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"
#include "formats/BetaDemo.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "formats/Dti.h"
#include "formats/MdkData.h"
#include "game/audio/LevelMusic.h"
#include "game/audio/SoundBank.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/CutsceneCamera.h"
#include "game/dev_tools/DevSession.h"
#include "game/dev_tools/LevelCommands.h"
#include "game/dev_tools/OverlayText.h"
#include "game/flow/Cheats.h"
#include "game/flow/GameState.h"
#include "game/flow/LoadProgress.h"
#include "game/flow/Settings.h"
#include "game/FreeCamera.h"
#include "game/hud/FontView.h"
#include "game/hud/Messages.h"
#include "game/kurt/FollowCamera.h"
#include "game/kurt/Kurt.h"
#include "game/kurt/KurtSprite.h"
#include "game/kurt/Scope.h"
#include "game/kurt/StateName.h"
#include "game/level/ArenaBand.h"
#include "game/level/ArenaStops.h"
#include "game/level/EnhancedLook.h"
#include "game/level/LevelData.h"
#include "game/level/LevelLight.h"
#include "game/level/LevelView.h"
#include "game/level/MaterialResolver.h"
#include "game/level/SkyModes.h"
#include "game/level/TriangleGroups.h"
#include "game/menu/PauseMenu.h"
#include "game/menu/SavePrompt.h"
#include "game/menu/Ui.h"
#include "game/mods/ModCatalog.h"
#include "game/mods/ModImages.h"
#include "game/mods/ModModels.h"
#include "game/objects/EffectsView.h"
#include "game/objects/FloorPull.h"
#include "game/objects/MdkObject.h"
#include "game/objects/RopeView.h"
#include "game/scripts/AirStrike.h"
#include "game/scripts/AnimatedTextures.h"
#include "game/scripts/Bomber.h"
#include "game/scripts/Debris.h"
#include "game/scripts/Effects.h"
#include "game/scripts/Fans.h"
#include "game/scripts/Items.h"
#include "game/scripts/Rides.h"
#include "game/scripts/ScriptRuntime.h"
#include "game/scripts/ScriptVm.h"
#include "game/scripts/Snapshot.h"
#include "game/scripts/SniperRounds.h"
#include "game/scripts/Twister.h"
#include "game/SniperTest.h"
#include "game/SniperView.h"

namespace Mdk::Game
{
    using Collision::ArenaSpace;
    using DevTools::LevelCommands;
    using Engine::Diagnostics::Section;
    using Engine::Platform::Capture;
    using Engine::Platform::Input;
    using Engine::Platform::Key;
    using Engine::Platform::MenuKey;
    using Engine::Render::Renderer;
    using Engine::Render::View;
    using Flow::Event;
    using Flow::GameState;
    using Flow::SaveGame;
    using Flow::SaveGames;
    using Flow::SaveKind;
    using Hud::HudSlot;
    using Hud::HudState;
    using Hud::Messages;
    using Kurt::FollowCamera;
    using Kurt::KurtSprite;
    using Kurt::Scope;
    using Level::EnhancedLook;
    using Level::LevelData;
    using Objects::MdkObject;
    using Objects::ObjectView;
    using Objects::RopeView;
    using Scripts::ScriptRuntime;
    using Scripts::SniperRounds;
    using KurtState = Kurt::Kurt::State;

    namespace
    {
        // A bool as C# prints it.
        const char* BoolText(bool value)
        {
            return value ? "True" : "False";
        }

        // Backdrop's name, as C# prints the enum.
        const char* BackdropName(Engine::Render::Backdrop backdrop)
        {
            switch (backdrop)
            {
            case Engine::Render::Backdrop::Sky: return "Sky";
            case Engine::Render::Backdrop::Clear: return "Clear";
            case Engine::Render::Backdrop::Keep: return "Keep";
            }

            return "";
        }

        // Milliseconds since `start` (Stopwatch.ElapsedMilliseconds).
        long long ElapsedMilliseconds(double start)
        {
            return static_cast<long long>((StopwatchSeconds() - start) * 1000.0);
        }
    }

    Viewer::Viewer(Menu::Ui& ui, const ViewerOptions& options, GameState& state, Flow::LoadProgress& progress)
        : _ui(ui), _options(options), _state(state), _renderer(ui.Renderer()), _audio(ui.Audio())
    {
        auto loading = StopwatchSeconds();
        _cheats = std::make_shared<Flow::Cheats>(ui.Settings(), state);
        // The menus' wheel and mouse don't reach Kurt.
        ui.Input().ClearMouse();
        const auto& data = *ui.Data();
        if (Formats::BetaDemo::IsBeta(options.Level))
        {
            if (ui.Beta == nullptr)
            {
                throw InvalidOperationException("The 1996 demo wasn't found");
            }

            _beta = ui.Beta;
        }

        _level = _beta != nullptr ? std::make_shared<LevelData>(*_beta, options.Level) : std::make_shared<LevelData>(data, options.Level);
        auto& level = *_level;
        auto& renderer = _renderer;
        auto groups = std::make_shared<Level::TriangleGroups>();
        auto graphics = ui.Settings().Graphics;
        MdkObject::ForgetPoses();
        // The enhanced look's mods (HD textures among them); the original look stays as made.
        ui.ScanMods();
        auto mods = graphics == Flow::Graphics::Enhanced ? ui.ModImages(level.Number) : nullptr;
        auto surfaces = std::make_shared<Level::MaterialResolver>(renderer, level.Dti, EnhancedLook::Surfaces(graphics), mods);
        progress.Report(Loaded::Data);
        Preload(*surfaces, level);
        _view = std::make_shared<Level::LevelView>(renderer, level, groups, EnhancedLook::Surfaces(graphics), surfaces);
        progress.Report(Loaded::Geometry);
        auto bank = _beta != nullptr ? Audio::SoundBank::ForBeta(*_beta, options.Level) : Audio::SoundBank::ForLevel(data, options.Level, level.Arenas);
        bank->Preload();
        _mixer = std::make_shared<Audio::SoundMixer>(_audio, [bank](const std::string& name) { return bank->Get(name); });
        auto cmi = _beta != nullptr
            ? _beta->LoadCmi(Formats::BetaDemo::LevelOf(options.Level))
            : Formats::Cmi::Load(data.PathOf(std::format("TRAVERSE/LEVEL{}/LEVEL{}.CMI", level.Number, level.Number)));
        _music = std::make_shared<Audio::LevelMusic>(*_mixer, cmi->ArenaMusic);
        _music->Ambience = cmi->ArenaAmbience;
        auto panorama = CreatePanorama(renderer, *level.Dti);
        panorama.Sampling = EnhancedLook::Sky(graphics);
        renderer.Panorama = panorama;
        renderer.Lighting = std::nullopt;
        _light = graphics == Flow::Graphics::Enhanced ? std::make_shared<Level::LevelLight>(renderer, level) : nullptr;
        progress.Report(Loaded::Light);

        _space = std::make_shared<ArenaSpace>();
        for (const auto& arena : level.Arenas)
        {
            if (!level.IsReachable(arena->Name))
            {
                continue;
            }

            _space->Add(arena);
        }

        // The 1996 demo's levels show its Kurt and its health display.
        auto sprites = Formats::Bni::Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        if (_beta != nullptr)
        {
            _beta->AddSprites(*sprites);
        }

        _sprite = std::make_shared<KurtSprite>(renderer, sprites, level.Dti->Palette, EnhancedLook::Sprites(graphics), mods);
        progress.Report(Loaded::Kurt);
        for (const auto& name : KurtSprite::LevelAnimations())
        {
            _sprite->Add(bank->Animation(name));
        }

        auto sprite = _sprite;
        _kurt = std::make_shared<Kurt::Kurt>(*_space, *_mixer, [sprite](KurtState current) { return sprite->FrameCount(current); });
        _kurt->Feet = options.Position.has_value() ? *options.Position : level.Dti->StartPosition + Vector3(0.f, 0.f, StartDrop);
        _kurt->Yaw = options.Yaw.has_value() ? *options.Yaw : level.Dti->StartAngle;
        _kurt->BetaMoves = _beta != nullptr;

        // The level starts in Kurt's arena, its music at once (0x41ba68: BSPShow(Kurt's)): the
        // DTI's start arena, or where a test put him.
        auto start = level.Dti->Arenas[level.Dti->StartArena].Name;
        _music->Enter(options.Position.has_value() ? _space->ArenaAt(*options.Position).value_or(start) : start);

        // The difficulty decides the town's timer: before the scripts.
        _kurt->Inventory->Difficulty = ui.Settings().Difficulty;
        _scripts = std::make_shared<ScriptRuntime>(_level, cmi, sprites, _space, groups, _mixer, _kurt, options.Soak);
        _scripts->Option = ui.Settings().Gore ? 1 : 0;
        _scripts->AirStrike->UsedUp = state.StrikeUsed;
        _follow = std::make_shared<FollowCamera>(_space.get());
        auto scripts = _scripts;
        _follow->ClipView = [scripts](Vector3 from, Vector3 to) { return scripts->ClipView(from, to); };
        _follow->SetLook(options.Look);
        auto follow = _follow;
        _scripts->ShakeRaised = [follow](float amount) { follow->RaiseShake(amount); };
        _kurt->Died = [this] { OnDied(); };
        _scripts->LevelEnded = [this](ScriptRuntime::GameOver over)
        {
            _ending = over == ScriptRuntime::GameOver::Yes ? Ending{ Event::GameOver, GameOverDelay } : Ending{ Event::LevelEnded, EndDelay };
        };
        _scripts->GameFinished = [this] { _next = Event::GameFinished; };

        // The HUD; pickups show their names (0x46c448).
        _hud = std::make_shared<Hud::HudView>(renderer, *sprites, level.Dti->Palette, ui.Fti, mods);
        progress.Report(Loaded::Scripts);
        _scripts->Messages = _hud->Messages;
        CarryFromFall(state);
        RestoreSnapshot(state);
        ShowNotice(state);
        auto hud = _hud;
        _kurt->Inventory->PickedUp = [hud](const std::string& name) { hud->Messages->Push(name, Messages::FlagZoom, PickupMessageSeconds); };

        for (const auto& pickup : options.Give)
        {
            _kurt->Collect(pickup);
        }
        auto view = _view;
        _scripts->ArenaEntered = [view](const std::string& arena) { view->Enter(arena); };
        // The mods' models (the enhanced look's); collisions keep the originals.
        std::shared_ptr<Mods::ModModels> swaps;
        if (mods != nullptr)
        {
            auto models = _scripts->LevelModels();
            for (const auto& arena : level.Arenas)
            {
                for (const auto& [name, model] : arena->Models)
                {
                    models.push_back(model);
                }
            }

            swaps = Mods::ModModels::Load(ui.Mods->Files(ui.Settings()), level.Number, models);
        }

        _objects = std::make_shared<ObjectView>(renderer, surfaces);
        _objects->Swaps = swaps;
        _scripts->PreparePoses();
        PreloadModels(level);

        _effects = std::make_shared<Objects::EffectsView>(renderer, surfaces, level, EnhancedLook::Sprites(graphics));
        _scripts->TextureStamped = [surfaces](Formats::Texture& texture) { surfaces->Refresh(texture); };
        _ropes = std::make_shared<RopeView>(renderer, level);
        _sniper = std::make_shared<SniperView>(renderer, *_objects, level, *_hud);
        _sniperTest = std::make_shared<SniperTest>(_options);
        _rideTest = std::make_shared<RideTest>(_options);
        _soak = options.Soak.has_value() ? std::make_shared<SoakTest>(level, *_space, options.Route, options.Wait) : nullptr;
        _cutscene = std::make_shared<CutsceneCamera>();
        _fly = std::make_shared<FreeCamera>();
        _fly->Pitch = options.Pitch;
        _flying = options.Fly;
        _pause = std::make_shared<Menu::PauseMenu>(ui, Menu::PauseMenu::Quick{ [this] { OpenSnapshot(); }, [this] { QuickLoad(); },
            [this] { return CanQuickLoad(); } });
        Console::WriteLine(std::format("Level {}: {} arenas, {} triangles, {} outlines, loaded in {} ms", options.Level, level.Arenas.size(),
            _view->TriangleCount, _view->OutlineCount, ElapsedMilliseconds(loading)));
        auto enabled = static_cast<int>(ui.Mods->Enabled(ui.Settings()).size());
        if (mods != nullptr && enabled > 0)
        {
            _mods = Mods::ModReport{ enabled, mods->Found(), swaps != nullptr ? swaps->Found() : std::vector<std::string>{} };
            Console::WriteLine(std::format("Mods: {}; textures {} of {}, colour textures {} MB", _mods.Summary(), surfaces->ModFound, surfaces->ModLooked(),
                renderer.ColourBytes() >> 20));
        }

        // The HD images are the renderer's now: hundreds of MB the level's lookups no longer need.
        if (mods != nullptr)
        {
            mods->Trim();
        }

        _test = options.Screenshot.has_value() || options.Frames.has_value();
        _blending = _test && !options.FrameRate.has_value() ? Blending::Off : Blending::On;
        _blend = std::make_shared<FrameBlend>(_blending);
        ui.Window().CaptureMouse(_test ? Capture::Off : Capture::On);
        _commands = std::make_shared<LevelCommands>(level, *_kurt, *_scripts, *_space, [this](const std::string& name) { return SaveSlot(name); });
        _commands->ModsReport = _mods;

        // The session's cheats hold in every level.
        _commands->SetGod(ui.Dev->God);
        _commands->SetNoclip(ui.Dev->Noclip);
        _commands->SetOneHit(ui.Dev->OneHit);
        EnterStartArena(state);
        progress.Report(Loaded::All);
    }

    Viewer::~Viewer() = default;

    void Viewer::EnterStartArena(GameState& state)
    {
        if (!state.StartArena.has_value())
        {
            return;
        }

        auto arena = *state.StartArena;
        state.StartArena = std::nullopt;
        auto stop = Level::ArenaStops::Find(*_level, *_space, arena);
        if (!stop.has_value())
        {
            Console::WriteLine(std::format("No arena {}", arena));
            return;
        }

        _scripts->TeleportKurt(arena, *stop, _kurt->Yaw);
    }

    bool Viewer::SaveSlot(const std::string& name)
    {
        return SaveGames::In(_ui.UserFolder()).Write(name, SnapshotSave(_scripts->Capture()));
    }

    std::shared_ptr<DevTools::ILevelTarget> Viewer::Commands() const
    {
        return _commands;
    }

    bool Viewer::ConsoleReady() const
    {
        return _time >= _options.Delay && _scripts->CurrentArena.size() != 0;
    }

    std::vector<std::string> Viewer::Status() const
    {
        auto level = DevTools::OverlayText::Level(_commands->Where(), static_cast<int>(_scripts->Objects.size()), std::string(Kurt::StateName(_kurt->Current)));
        if (_mods.Mods > 0)
        {
            level.push_back(_mods.Summary());
        }

        if (_pause->Open())
        {
            level.insert(level.begin(), "pause menu");
            return level;
        }

        if (_snapshotPrompt != nullptr)
        {
            level.insert(level.begin(), "save prompt");
        }

        return level;
    }

    Engine::Platform::Typing Viewer::Typing() const
    {
        return _snapshotPrompt != nullptr ? _snapshotPrompt->Typing() : Engine::Platform::Typing::Off;
    }

    std::shared_ptr<Scripts::GameStats> Viewer::Stats() const
    {
        return _scripts->Stats;
    }

    int Viewer::Health() const
    {
        return _kurt->Health;
    }

    void Viewer::CarryFromFall(GameState& state)
    {
        if (!state.Carry.has_value())
        {
            return;
        }

        auto carry = *state.Carry;
        state.Carry = std::nullopt;
        for (const auto& pickup : carry.Pickups)
        {
            _kurt->Collect(pickup);
        }

        _kurt->SetHealth(carry.Health);
        Console::WriteLine(std::format("Level starts with the fall's health {} and pickups {}", carry.Health, Str::Join(",", carry.Pickups)));
    }

    void Viewer::Settle()
    {
        if (_settled)
        {
            return;
        }

        _settled = true;
        CompactLargeObjects();
        // GC.Collect: no garbage collector here.
    }

    void Viewer::CompactLargeObjects()
    {
        // GCSettings.LargeObjectHeapCompactionMode: no garbage collector here.
    }

    Event Viewer::Frame(float elapsed, const std::optional<std::string>& screenshot)
    {
        auto& input = _ui.Input();

        // While the game waits, the mouse and the wheel don't pile up for Kurt.
        if (_snapshotPrompt != nullptr || _pause->Open())
        {
            input.ClearMouse();
        }

        // F2's name prompt: the game waits under it.
        if (_snapshotPrompt != nullptr)
        {
            return AskSnapshot(elapsed, screenshot);
        }

        // The pause menu: the game waits under it.
        if (_pause->Open())
        {
            DrawScene(elapsed);
            auto paused = _pause->Update(elapsed);
            _renderer.Present(SceneView(), Background(), screenshot);
            CaptureMouse();

            // Its Quick load reloads at once.
            return paused == Event::None ? _next : paused;
        }

        if (input.WasPressed(MenuKey::Back) && (_scripts->Strike == nullptr || !_scripts->Strike->Active))
        {
            _pause->Show();
            _ui.Window().CaptureMouse(Capture::Off);
            return Event::None;
        }

        // The full-screen strike: the game waits (Esc skips it); tests count its time.
        auto skip = input.WasPressed(Key::Escape) ? SniperView::StrikeSkip::Now : SniperView::StrikeSkip::No;
        if (auto strikeView = _sniper->DrawStrike(*_scripts, skip, elapsed))
        {
            _strikeTime += elapsed;
            auto shot = SavePath(_options, input, _time + _strikeTime);
            if (!shot.has_value())
            {
                shot = screenshot;
            }

            _renderer.Present(*strikeView, Background(), shot);
            if (shot.has_value() && _test)
            {
                Console::WriteLine(std::format("Saved {} (strike)", *shot));
                return Event::Quit;
            }

            return Event::None;
        }

        if (input.WasPressed(Key::AlwaysRun))
        {
            input.ToggleRun();
        }

        if (input.WasPressed(Key::Fly))
        {
            _flying = !_flying;
            _fly->Position = _follow->Position;
            _fly->Yaw = _kurt->Yaw;
        }

        // Tests ignore the real mouse.
        if (_test)
        {
            input.ClearMouse();
        }

        TypeCheats(input.Typed());
        if (input.Digit() != Input::NoDigit && input.IsDown(Key::Teleport))
        {
            BetaTeleport(input.Digit());
        }

        if (input.WasPressed(Key::QuickSave))
        {
            OpenSnapshot();
        }

        if (input.WasPressed(Key::QuickLoad))
        {
            QuickLoad();
        }

        MouseLook(input);
        RunSteps(input, elapsed);

        if (_ending.has_value())
        {
            auto ending = *_ending;
            _ending->Delay = ending.Delay - elapsed;
            if (_ending->Delay <= 0.f)
            {
                _next = ending.Event;
            }
        }

        auto camera = DrawScene(elapsed);
        if (_options.Trace)
        {
            Trace(camera.Position);
        }

        auto save = SavePath(_options, input, _time);
        if (!save.has_value())
        {
            save = FramePath();
        }

        if (!save.has_value())
        {
            save = screenshot;
        }

        {
            auto measure = _ui.Dev->Profiler.Measure(Section::Render);
            _renderer.Present(camera, Background(), save);
        }

        Settle();

        if (!save.has_value())
        {
            return _next;
        }

        Console::WriteLine(std::format("Saved {}", *save));
        if (!_test || (_options.Frames.has_value() && !_options.Frames->IsLast(_frameStep - 1)))
        {
            return _next;
        }

        if (_options.Snapshot.has_value())
        {
            SaveSnapshot(*_options.Snapshot);
        }

        Report(*_kurt, *_space);
        if (_soak != nullptr)
        {
            _soak->Report(*_scripts, *_kurt, _time);
        }

        return Event::Quit;
    }

    void Viewer::RunSteps(Input& input, float elapsed)
    {
        const auto& options = _options;
        _pending = MathF::Min(_pending + elapsed, Step * MaxSteps);
        while (_pending >= Step)
        {
            _pending -= Step;
            _time += Step;
            if (_soak == nullptr)
            {
                HoldTestKeys(input);
            }

            RunTests();
            _sniperTest->Step(*_kurt, *_scripts, input, _time);
            _rideTest->Step(*_kurt, *_scripts, input, _time);
            UpdateKurt(input);
            {
                auto measure = _ui.Dev->Profiler.Measure(Section::Scripts);
                _scripts->Update(Step);
            }

            // The camera follows Kurt where his platform carried him (game_frame: objects, then
            // camera); before, it lagged a step behind on each tick: a ghost.
            UpdateCamera(input);
            input.ClearMouse();
            _blend->Record(*_kurt, Carrier(), Driven(), _scripts->TickCount());

            // The music follows Kurt's arena, which the scripts set (BSPShow).
            {
                auto measure = _ui.Dev->Profiler.Measure(Section::Audio);
                _music->Enter(_scripts->CurrentArena);
                _music->Update(Step);
            }

            if (auto arena = _level->ArenaNamed(_scripts->CurrentArena))
            {
                _hud->Enter(_level->PaletteOf(*arena));
            }

            if (CutsceneCamera::Active(*_scripts))
            {
                _cutscene->Update(*_scripts);
            }

            _space->SetSolid(_scripts->SolidArenas());
            if (_soak != nullptr)
            {
                _soak->Step(*_kurt, *_scripts, *_space, _time);
            }

            if (MathF::Floor(_time * Kurt::Kurt::Ticks) > MathF::Floor((_time - Step) * Kurt::Kurt::Ticks))
            {
                _hud->Tick(HudStateOf(*_kurt, *_scripts, _hudSlots));
            }

            if (options.Profile && MathF::Floor(_time) > MathF::Floor(_time - Step))
            {
                Profile(*_scripts, _time);
                Console::WriteLine(std::format("  sky {} ({})", _scripts->SkyMode, BackdropName(_renderer.Backdrop)));
                Console::WriteLine(std::format("  music {} {}dB, fading {}", _music->Playing().value_or(""), Decibels(_music->Volume()), _music->Fading().value_or("-")));
            }
        }
    }

    void Viewer::UpdateKurt(Input& input)
    {
        auto measure = _ui.Dev->Profiler.Measure(Section::Physics);
        if (_flying)
        {
            _fly->Update(input, Step);
            return;
        }

        _kurt->Update(input, Step);
    }

    void Viewer::UpdateCamera(Input& input)
    {
        auto measure = _ui.Dev->Profiler.Measure(Section::Physics);
        if (_flying)
        {
            return;
        }

        _follow->Update(*_kurt, PitchGoal(), input, Step);
    }

    void Viewer::MouseLook(Input& input)
    {
        if (_flying || _test)
        {
            return;
        }

        _blend->Turn(_kurt->MouseLook(input));
        _follow->MouseLook(*_kurt, input);
    }

    std::shared_ptr<MdkObject> Viewer::Carrier() const
    {
        auto& rides = *_scripts->Rides;
        if (rides.Bomber != nullptr)
        {
            return rides.Ridden;
        }

        return _kurt->OnFloor && _kurt->Platform != nullptr ? _kurt->Platform->shared_from_this() : nullptr;
    }

    std::shared_ptr<MdkObject> Viewer::Driven() const
    {
        return _scripts->Rides->Bomber == nullptr ? _scripts->Rides->Ridden : nullptr;
    }

    void Viewer::BlendFrame()
    {
        auto step = _pending / Step;
        auto tick = _scripts->TickBlend(_pending);
        _blend->Update(*_kurt, step, tick);
        if (_blending == Blending::Off || _flying)
        {
            return;
        }

        if (CutsceneCamera::Active(*_scripts))
        {
            _cutscene->Blend(tick);
        }

        _follow->Blend(*_kurt, _blend->Feet, _blend->Yaw, step);
    }

    void Viewer::HoldTestKeys(Input& input)
    {
        const auto& options = _options;
        auto started = _time >= options.Delay;
        input.Hold(Key::Forward, started && _time <= options.Delay + options.Walk ? Input::State::Down : Input::State::Up);
        input.Hold(Key::Jump, started && options.Jump ? Input::State::Down : Input::State::Up);
        input.Hold(Key::Fire, started && options.Fire ? Input::State::Down : Input::State::Up);
        input.Hold(Key::UseItem, options.Use && _time >= UseStart && _time <= UseStart + UseTime ? Input::State::Down : Input::State::Up);
        if (options.Roll.has_value())
        {
            input.Hold(*options.Roll == BetaRoll::Left ? Key::RollLeft : Key::RollRight, started ? Input::State::Down : Input::State::Up);
        }

        if (options.BetaTeleport.has_value() && started && _time - Step < options.Delay)
        {
            BetaTeleport(*options.BetaTeleport);
        }
    }

    void Viewer::BetaTeleport(int index)
    {
        if (_beta == nullptr)
        {
            return;
        }

        std::vector<std::pair<std::string, Vector3>> teleports;
        for (const auto& t : _beta->LoadTeleports())
        {
            auto inLevel = std::any_of(_level->Arenas.begin(), _level->Arenas.end(), [&t](const auto& a) { return a->Name == t.first; });
            if (inLevel)
            {
                teleports.push_back(t);
            }
        }

        if (index >= static_cast<int>(teleports.size()))
        {
            return;
        }

        auto [arena, position] = teleports[index];
        _scripts->TeleportKurt(arena, position, _kurt->Yaw);
        Console::WriteLine(std::format("Teleport {}: {} {} {} {}", index, arena, Str::FromFloat(position.X), Str::FromFloat(position.Y),
            Str::FromFloat(position.Z)));
    }

    void Viewer::RunTests()
    {
        if (_time < TestStart || _time - Step >= TestStart)
        {
            return;
        }

        if (_options.Die)
        {
            _kurt->Hurt(FatalDamage);
        }

        if (_options.Event.has_value())
        {
            _scripts->SpecialEvent(*_scripts->GetArenaState(_scripts->CurrentArena)->Controller, *_options.Event);
        }
    }

    View Viewer::DrawScene(float elapsed)
    {
        BlendFrame();
        auto camera = SceneView();
        auto eye = Eye();
        _mixer->ListenerPosition = camera.Position;
        _mixer->ListenerRight = eye.Right;
        // In sniper mode the sounds are heard through the scope.
        _mixer->ScopeZoom = _kurt->Sniping && !_flying ? _kurt->Scope->Zoom : 0.f;
        _mixer->ListenerForward = _kurt->SniperForward();
        _mixer->ListenerUp = FollowCamera::UpOf(_kurt->SniperForward());
        {
            auto measure = _ui.Dev->Profiler.Measure(Section::Audio);
            _mixer->Update(elapsed);
        }

        _scripts->Eye = camera.Position;
        if (_light != nullptr)
        {
            _light->Update(*_scripts, *_kurt);
        }

        auto render = _ui.Dev->Profiler.Measure(Section::Scene);
        // The camera's arena covers the other drawn one (ArenaBand); Kurt and his board are in front.
        auto cameraArena = _scripts->CameraArena(camera.Position);
        _view->Draw(_scripts->DrawnArenas(), *_scripts->AnimatedTextures, cameraArena);
        _view->DrawEnd(_scripts->EndLevel.get(), cameraArena);
        DrawObjects(_renderer, *_objects, *_scripts, *_level, _looks, *_blend, cameraArena);
        DrawBoard(camera.Position, eye.Forward);
        _ropes->Draw(_scripts->Objects, *_blend, cameraArena);
        _sniper->Draw(*_kurt, *_scripts, *_blend, elapsed);
        _effects->Draw(*_scripts, camera, _blend->Tick(), cameraArena);
        _sprite->Draw(*_kurt, _blend->Feet, camera.Position, eye.Forward, eye.Up, eye.FieldOfView, *_space);

        _hud->Messages->Update(elapsed);
        _hud->Draw(HudStateOf(*_kurt, *_scripts, _hudSlots));
        return camera;
    }

    View Viewer::SceneView()
    {
        auto aspect = _renderer.AspectRatio();
        if (_flying)
        {
            return _fly->View(aspect);
        }

        if (_kurt->Sniping)
        {
            return _blending == Blending::Off ? FollowCamera::SniperView(*_kurt, aspect) : ScopeView(aspect);
        }

        return CutsceneCamera::Active(*_scripts) ? _cutscene->View(aspect) : _follow->View(aspect);
    }

    View Viewer::ScopeView(float aspect)
    {
        auto yaw = _blend->Yaw;
        auto pitch = _blend->ScopePitch;
        auto eye = Scope::Eye(_blend->Feet, Scope::Direction(yaw, 0.f), pitch);
        return FollowCamera::SniperView(eye, Scope::Direction(yaw, pitch), _blend->Zoom, aspect);
    }

    Viewer::EyeAxes Viewer::Eye() const
    {
        if (_flying)
        {
            return { _fly->Forward(), Vector3::UnitZ(), _fly->Right(), FreeCamera::FieldOfView };
        }

        return CutsceneCamera::Active(*_scripts)
            ? EyeAxes{ _cutscene->Forward, _cutscene->Up, _cutscene->Right, FollowCamera::FieldOfView }
            : EyeAxes{ _follow->Forward, _follow->Up, _follow->Right, FollowCamera::FieldOfView };
    }

    float Viewer::PitchGoal()
    {
        return _scripts->CameraTrackTicks > 0 ? _scripts->CameraTrackPitch : ArenaPitch(_scripts->CurrentArena);
    }

    void Viewer::RestoreSnapshot(GameState& state)
    {
        if (!state.Snapshot.has_value())
        {
            return;
        }

        auto json = *state.Snapshot;
        state.Snapshot = std::nullopt;
        _scripts->Load(json);
        Console::WriteLine(std::format("restored hash {}, objects {}", Scripts::Snapshot::Hash(_scripts->Capture()), _scripts->Objects.size()));
    }

    void Viewer::OpenSnapshot()
    {
        if (!_scripts->CanSnapshot() || _kurt->Sniping)
        {
            Notify("Can't save now");
            return;
        }

        auto name = std::to_string(GameState::IndexOf(_level->Number) + 1);
        auto json = _scripts->Capture();
        Console::WriteLine(std::format("snapshot hash {}, objects {}", Scripts::Snapshot::Hash(json), _scripts->Objects.size()));
        _snapshotPrompt = std::make_shared<Menu::SavePrompt>(_ui, std::make_shared<Menu::Fonts>(_renderer, *_ui.Fti, nullptr, _ui.CanvasMods()),
            SaveGames::In(_ui.UserFolder()), SnapshotSave(json), name);
        _ui.Window().CaptureMouse(Capture::Off);
    }

    Event Viewer::AskSnapshot(float elapsed, const std::optional<std::string>& screenshot)
    {
        _ui.View->Layout(Menu::ScreenView::Fit::Inside);
        _snapshotPrompt->Update(_ui.Input(), elapsed);
        _snapshotPrompt->Draw();
        _ui.Present(screenshot);
        if (!_snapshotPrompt->Closed())
        {
            return Event::None;
        }

        if (_snapshotPrompt->Saved())
        {
            _state.QuickSlot = _snapshotPrompt->Name();
            Notify("Game saved");
        }

        _snapshotPrompt = nullptr;
        CaptureMouse();
        return Event::None;
    }

    void Viewer::QuickLoad()
    {
        auto saves = SaveGames::In(_ui.UserFolder());
        auto name = saves.QuickSlot(_state.QuickSlot);
        auto save = name.has_value() ? saves.Read(*name) : std::nullopt;
        if (!save.has_value())
        {
            Notify("Nothing to load");
            return;
        }

        _state.Load(*save);
        _state.Notice = "Game loaded";
        Console::WriteLine(std::format("Loaded game {}: level {}", *name, save->Level));
        _next = save->Kind == SaveKind::BeforeLevel ? Event::Briefing : Event::Play;
    }

    bool Viewer::CanQuickLoad() const
    {
        return SaveGames::In(_ui.UserFolder()).QuickSlot(_state.QuickSlot).has_value();
    }

    void Viewer::Notify(const std::string& text)
    {
        _hud->Messages->PushText(std::vector<byte>(text.begin(), text.end()), Messages::FlagZoom | Messages::FlagFront, NoticeSeconds);
        Console::WriteLine(text);
    }

    void Viewer::ShowNotice(GameState& state)
    {
        if (!state.Notice.has_value())
        {
            return;
        }

        auto notice = *state.Notice;
        state.Notice = std::nullopt;
        Notify(notice);
    }

    void Viewer::SaveSnapshot(const std::string& name)
    {
        auto json = _scripts->Capture();
        SaveGames::In(_ui.UserFolder()).Write(name, SnapshotSave(json));
        Console::WriteLine(std::format("snapshot hash {}, objects {}", Scripts::Snapshot::Hash(json), _scripts->Objects.size()));
    }

    SaveGame Viewer::SnapshotSave(const std::string& json)
    {
        _state.Level = _level->Number;
        auto save = _state.Save(SaveKind::Snapshot);
        save.Health = _kurt->Health;
        save.State = json;
        return save;
    }

    void Viewer::CaptureMouse()
    {
        if (!_pause->Open() && _snapshotPrompt == nullptr)
        {
            _ui.Window().CaptureMouse(_test ? Capture::Off : Capture::On);
        }
    }

    void Viewer::TypeCheats(const std::string& typed)
    {
        if (typed.size() == 0)
        {
            return;
        }

        for (auto c : Str::ToUpper(typed))
        {
            if (c < 'A' || c > 'Z')
            {
                continue;
            }

            auto cheat = _cheats->Type(c);
            if (cheat == Flow::Cheats::Cheat::Gore)
            {
                _scripts->Option = _ui.Settings().Gore ? 1 : 0;
                Console::WriteLine(std::format("Gore {}", _ui.Settings().Gore ? "on" : "off"));
            }
            else if (cheat == Flow::Cheats::Cheat::DebugKeys)
            {
                Console::WriteLine(std::format("Debug keys {}", _state.DebugKeys ? "on" : "off"));
            }
        }
    }

    void Viewer::OnDied()
    {
        Console::WriteLine("Kurt died");
        _state.Level = _level->Number;
        _state.Deaths++;
        _state.StrikeUsed = _scripts->AirStrike->UsedUp;
        _next = Event::KurtDied;
    }

    void Viewer::Dispose()
    {
        _audio.StopAll();
        _renderer.Backdrop = Engine::Render::Backdrop::Sky;
        _ui.Window().CaptureMouse(Capture::Off);
    }

    HudState Viewer::HudStateOf(Kurt::Kurt& kurt, ScriptRuntime& scripts, std::vector<HudSlot>& slots)
    {
        auto& inventory = *kurt.Inventory;
        slots.clear();
        for (size_t i = 0; i < inventory.Slots().size(); i++)
        {
            slots.push_back(HudSlot{ static_cast<int>(inventory.Slots()[i].Item), inventory.Slots()[i].Count });
        }

        auto [barHealth, barMax] = scripts.GetBar();
        auto bomber = scripts.Rides->Bomber;
        return HudState{ kurt.Health, kurt.HurtFlash, kurt.WhiteFlash, kurt.Current == KurtState::Dead, slots, inventory.Selected,
            inventory.SuperChainGun, barHealth, barMax, bomber != nullptr ? std::optional(bomber->Shown()) : std::nullopt, scripts.TownTimeLeft() };
    }

    void Viewer::PreloadModels(LevelData& level)
    {
        for (const auto& arena : level.Arenas)
        {
            for (const auto& [name, model] : arena->Models)
            {
                _objects->Preload(model, ObjectLook(level, _looks, arena->Name));
            }
        }

        for (const auto& obj : _scripts->Objects)
        {
            if (obj->Model != nullptr)
            {
                _objects->Preload(obj->Model, ObjectLook(level, _looks, obj->Arena));
            }
        }
    }

    void Viewer::Preload(Level::MaterialResolver& surfaces, LevelData& level)
    {
        surfaces.Preload(level.Dti->Palette, std::vector<std::shared_ptr<Formats::TextureArchive>>{ level.LevelTextures });
        for (const auto& arena : level.Arenas)
        {
            surfaces.Preload(level.PaletteOf(*arena), level.ArchivesOf(*arena));
        }
    }

    void Viewer::DrawObjects(Renderer& renderer, ObjectView& view, ScriptRuntime& scripts, LevelData& level, Looks& looks, FrameBlend& blend,
        const std::string& camera)
    {
        for (const auto& obj : scripts.Objects)
        {
            if (!obj->Visible || IsBoard(scripts, *obj))
            {
                continue;
            }

            renderer.Band = Level::ArenaBand::Of(obj->Arena, camera);
            view.Draw(*obj, ObjectLook(level, looks, obj->Arena), blend.TransformOf(*obj), blend.PoseOf(*obj));
        }

        renderer.Band = Engine::Render::DepthBand::Front;
    }

    bool Viewer::IsBoard(ScriptRuntime& scripts, const MdkObject& obj)
    {
        return scripts.Rides->OnBoard() && &obj == scripts.Rides->Ridden.get();
    }

    void Viewer::DrawBoard(Vector3 eye, Vector3 forward)
    {
        auto board = _scripts->Rides->Ridden;
        if (board == nullptr || !board->Visible || !IsBoard(*_scripts, *board))
        {
            return;
        }

        auto transform = Objects::FloorPull::Of(*board, _blend->TransformOf(*board), eye, forward, *_space);
        _objects->Draw(*board, ObjectLook(*_level, _looks, board->Arena), transform, _blend->PoseOf(*board));
    }

    ObjectView::Look Viewer::ObjectLook(LevelData& level, Looks& looks, const std::string& name)
    {
        auto found = looks.find(name);
        if (found != looks.end())
        {
            return found->second;
        }

        auto arena = level.ArenaNamed(name);
        return looks[name] = arena != nullptr
            ? ObjectView::Look{ level.PaletteOf(*arena), level.ArchivesOf(*arena) }
            : ObjectView::Look{ level.Dti->Palette, { level.LevelTextures } };
    }

    void Viewer::Profile(ScriptRuntime& scripts, float time)
    {
        auto second = scripts.SecondArena + (scripts.SecondActive ? " (active)" : "");
        Console::WriteLine(std::format("Profile {:.0f}s: objects {}, arena {}, second {}", time, scripts.Objects.size(), scripts.CurrentArena, second));
        Console::WriteLine(std::format("  effects {}, debris pieces {}, fans {}", scripts.Effects->All().size(), scripts.Debris->PieceCount(),
            scripts.Fans->Count()));
        for (const auto& obj : scripts.Objects)
        {
            if (obj->Arena != scripts.CurrentArena)
            {
                continue;
            }

            auto p = obj->Position;
            auto door = (obj->Flags & MdkObject::FlagDoor) != 0 ? std::format(" door {:x}", static_cast<uint>(obj->DoorState)) : "";
            Console::WriteLine(std::format("  {}_{} {} ({}, {}, {}) yaw {} ", obj->TypeName, obj->InstanceId, obj->Arena, Rounded(p.X), Rounded(p.Y),
                Rounded(p.Z), ToInt(obj->Yaw))
                + std::format("move {} path {} anim {} frame {} ", obj->MoveCommand, obj->Path, obj->Animation != nullptr ? obj->Animation->Name : "-",
                    obj->AnimationFrame)
                + std::format("speed {:.1f} health {} flags {:x}{}", obj->Speed, obj->Health, static_cast<uint>(obj->Flags), door));
        }

        if (CutsceneCamera::Active(scripts))
        {
            auto c = scripts.CameraPoint;
            Console::WriteLine(std::format("  cutscene {:x} shot {} camera ({}, {}, {}) yaw {:.0f} pitch {:.0f}", static_cast<uint>(scripts.Cutscene),
                scripts.CameraMode, Rounded(c.X), Rounded(c.Y), Rounded(c.Z), scripts.CameraYaw, scripts.CameraPitch));
        }

        if (scripts.Items->Twisters().size() != 0)
        {
            auto triangles = 0;
            for (const auto& t : scripts.Items->Twisters())
            {
                triangles += static_cast<int>(t->Ribbon->Triangles().size()) / 3;
            }

            Console::WriteLine(std::format("  twisters {}, ribbon triangles {}", scripts.Items->Twisters().size(), triangles));
        }

        for (const auto& obj : scripts.Objects)
        {
            if (obj->RopeMask == 0 || obj->Dead)
            {
                continue;
            }

            Console::WriteLine(std::format("  rope {}_{} lines {} colour {}", obj->TypeName, obj->InstanceId, RopeView::LinesOf(*obj).size() / 2, obj->RopeColor));
        }

        for (const auto& [key, frame] : scripts.AnimatedTextures->Frames())
        {
            const auto& [arena, texture] = key;
            Console::WriteLine(std::format("  texture {} {} frame {:.2f}", arena, texture, frame));
        }

        RideTest::Report(scripts);
        if (scripts.EndLevel != nullptr)
        {
            Console::WriteLine(scripts.EndLevel->Describe());
        }

        if (scripts.Vm->Unimplemented.Count() != 0)
        {
            std::vector<std::string> counts;
            for (const auto& [opcode, count] : scripts.Vm->Unimplemented)
            {
                counts.push_back(std::format("{}x{}", opcode, count));
            }

            Console::WriteLine(std::format("  unimplemented opcodes: {}", Str::Join(", ", counts)));
        }
    }

    void Viewer::Trace(Vector3 camera)
    {
        auto carrier = _scripts->Rides->Ridden;
        if (carrier == nullptr && _kurt->Platform != nullptr)
        {
            carrier = _kurt->Platform->shared_from_this();
        }

        auto at = carrier != nullptr ? _blend->PositionOf(*carrier) : Vector3::Zero();
        auto on = carrier != nullptr ? std::format(" on {} {:.2f} {:.2f} {:.2f}", carrier->TypeName, at.X, at.Y, at.Z) : "";
        auto f = _blend->Feet;
        Console::WriteLine(std::format("trace {:.3f} kurt {:.2f} {:.2f} {:.2f} camera {:.2f} {:.2f} {:.2f}{}", _time, f.X, f.Y, f.Z, camera.X, camera.Y,
            camera.Z, on));
        TracePoses();
        TraceEffects();
        TraceScope();
    }

    void Viewer::TraceScope()
    {
        if (!_kurt->Sniping)
        {
            return;
        }

        Console::WriteLine(std::format("blend scope {:.3f} scope {:.4f} {:.3f} {:.4f}", _time, _blend->Zoom, _blend->ScopePitch, _blend->ClipTime));
        for (auto i = 0; i < SniperRounds::Slots; i++)
        {
            auto camera = _scripts->SniperRounds->Camera(i, _blend->Tick());
            if (camera.Shot != SniperRounds::Shot::Watching)
            {
                continue;
            }

            auto p = camera.Position;
            Console::WriteLine(std::format("blend round {:.3f} {} {:.2f} {:.2f} {:.2f}", _time, i, p.X, p.Y, p.Z));
        }
    }

    void Viewer::TraceEffects()
    {
        // RuntimeHelpers.GetHashCode: an identity per object; here its address.
        auto identity = [](const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); };
        for (const auto& piece : _scripts->Debris->Pieces())
        {
            auto c = piece->CenterAt(_blend->Tick());
            Console::WriteLine(std::format("blend piece {:.3f} {} {:.3f} {:.3f} {:.3f}", _time, identity(piece.get()), c.X, c.Y, c.Z));
        }

        for (const auto& effect : _scripts->Effects->All())
        {
            auto p = effect->PositionAt(_blend->Tick());
            Console::WriteLine(std::format("blend effect {:.3f} {} {:.3f} {:.3f} {:.3f}", _time, identity(effect.get()), p.X, p.Y, p.Z));
        }
    }

    void Viewer::TracePoses()
    {
        for (const auto& obj : _scripts->Objects)
        {
            if (obj->Animation == nullptr || obj->Dead || !obj->Visible || obj->Arena != _scripts->CurrentArena)
            {
                continue;
            }

            auto sum = Vector3::Zero();
            auto count = 0;
            for (const auto& part : _blend->PoseOf(*obj))
            {
                for (const auto& vertex : part)
                {
                    sum += vertex;
                    count++;
                }
            }

            auto m = count == 0 ? Vector3::Zero() : sum / static_cast<float>(count);
            Console::WriteLine(std::format("blend pose {:.3f} {}_{} {:.3f} {:.3f} {:.3f}", _time, obj->TypeName, obj->InstanceId, m.X, m.Y, m.Z));
        }
    }

    int Viewer::Decibels(float volume)
    {
        return ToInt(MathF::Round(20.f * MathF::Log10(Audio::SoundMixer::Gain(volume))));
    }

    std::string Viewer::Rounded(float value)
    {
        return std::format("{:.1f}", MathF::RoundAwayFromZero(value) + 0.f);
    }

    std::optional<std::string> Viewer::SavePath(const ViewerOptions& options, Input& input, float time)
    {
        if (options.Screenshot.has_value())
        {
            return time >= options.Wait ? options.Screenshot : std::nullopt;
        }

        if (!input.WasPressed(Key::Screenshot))
        {
            return std::nullopt;
        }

        auto now = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
        return std::format("mdk-{}.bmp", stamp);
    }

    std::optional<std::string> Viewer::FramePath()
    {
        if (!_options.Frames.has_value() || _time < _options.Wait)
        {
            return std::nullopt;
        }

        return _options.Frames->PathOf(_frameStep++);
    }

    void Viewer::Report(Kurt::Kurt& kurt, ArenaSpace& space)
    {
        auto f = kurt.Feet;
        Console::WriteLine(std::format("Kurt at {:.2f} {:.2f} {:.2f} yaw {:.0f} {} floor {} arena {} health {}", f.X, f.Y, f.Z, kurt.Yaw,
            Kurt::StateName(kurt.Current), BoolText(kurt.OnFloor), space.ArenaAt(f).value_or(""), kurt.Health));
    }

    float Viewer::ArenaPitch(const std::string& name)
    {
        constexpr float DefaultPitch = 4.f;
        auto found = _pitches.find(name);
        if (found != _pitches.end())
        {
            return found->second;
        }

        auto entry = _level->EntryNamed(name);
        return _pitches[name] = entry != nullptr ? entry->Pitch : DefaultPitch;
    }

    Engine::Render::Panorama Viewer::CreatePanorama(Renderer& renderer, const Formats::Dti& dti)
    {
        auto sky = renderer.CreateIndexTexture(dti.Sky->Width, dti.Sky->Height, dti.Sky->Indices);
        auto mirrorSky = dti.MirrorSky == dti.Sky ? sky : renderer.CreateIndexTexture(dti.MirrorSky->Width, dti.MirrorSky->Height, dti.MirrorSky->Indices);
        auto palette = renderer.CreatePalette(dti.Palette->Rgba);
        return Engine::Render::Panorama{ sky, mirrorSky, palette, static_cast<float>(dti.SkyWrapWidth), static_cast<float>(dti.SkyHorizonRow), static_cast<float>(dti.SkyOffset),
            static_cast<float>(dti.Sky->Height),
            dti.SkyTopColor, dti.SkyBottomColor };
    }

    Vector4 Viewer::Background()
    {
        _renderer.Backdrop = Level::SkyModes::BackdropOf(_scripts->SkyMode);
        return Level::SkyModes::ClearOf(_scripts->SkyMode, SkyColour(*_level->Dti));
    }

    Vector4 Viewer::SkyColour(const Formats::Dti& dti)
    {
        constexpr float ByteMax = 255.f;
        auto at = dti.SkyTopColor * 4;
        const auto& rgba = dti.Palette->Rgba;
        return Vector4(rgba[at], rgba[at + 1], rgba[at + 2], ByteMax) / ByteMax;
    }
}
