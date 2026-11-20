#include "Mdk.Game/Fall/FallScreen.h"

#include <algorithm>
#include <format>

#include "Mdk.Core/Console.h"
#include "Mdk.Core/DotNet.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Engine/Platform/Window.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Fall3d.h"
#include "Mdk.Formats/MdkData.h"
#include "Mdk.Formats/Sni.h"
#include "Mdk.Formats/TextureArchive.h"
#include "Mdk.Game/Flow/GameState.h"
#include "Mdk.Game/Flow/Settings.h"
#include "Mdk.Game/Hud/Messages.h"
#include "Mdk.Game/Menu/PauseMenu.h"
#include "Mdk.Game/Menu/Ui.h"

namespace Mdk::Game::Fall
{
    using Engine::Platform::Capture;
    using Engine::Platform::Input;
    using Engine::Platform::Key;
    using Engine::Platform::MenuKey;
    using Engine::Platform::Visibility;
    using Flow::Event;

    FallScreen::FallScreen(Menu::Ui& ui, Flow::GameState& state, ViewerOptions test)
        : _ui(ui), _state(state), _test(test)
    {
        // No pointer over the fall (as in a level); a hidden test window keeps it free.
        if (ui.Window().Visibility() == Visibility::Shown)
        {
            ui.Window().CaptureMouse(Capture::On);
        }

        auto index = Math::Clamp(Flow::GameState::IndexOf(state.Level), 0, LastIndex);
        auto n = index + 1;
        const auto& data = ui.Data();
        auto bni = Formats::Bni::Load(data->PathOf("FALL3D/FALL3D.BNI"));
        auto mti = Formats::TextureArchive::Load(data->PathOf(std::format("FALL3D/FALL3D_{}.MTI", n)));
        _view = std::make_unique<FallView>(ui.Renderer(), bni, mti, index, ui.Fti, ui.CanvasMods(state.Level));
        _sounds = std::make_shared<Audio::SoundBank>(std::vector<std::shared_ptr<Formats::Sni>>{ Formats::Sni::Load(data->PathOf("FALL3D/FALL3D.SNI")) });
        _pause = std::make_shared<Menu::PauseMenu>(ui);

        auto models = _view->Models;
        auto animationOf = [&models](const std::string& name)
        {
            auto animation = models->Animation(name);
            return FallSim::Animation{ animation->Speed, animation->FrameCount };
        };

        auto setup = FallSim::Setup{ index, ui.Settings().Difficulty, Formats::Fall3d::Pickups(*bni, std::format("FALLPU_{}", n)),
            animationOf("KURTANIM"), animationOf("KURT_HIT"), animationOf("BONESANM") };
        _sim = std::make_unique<FallSim>(setup, std::make_shared<Mdk::Random>());
        _sim->Sound = [this](const std::string& name) { Play(name); };
        _sim->Message = [this](const std::string& name) { _view->Hud->Messages->Push(name, Hud::Messages::FlagZoom, MessageSeconds); };

        // The wind plays from the start, silent until the intro's end.
        _windVoice = Loop("WINDLOOP");
        Console::WriteLine(std::format("Fall: index {} difficulty {} beam {:.2f} missiles {} spread {:.2f} missile gap {} radar gap {} pickups {}",
            index, static_cast<int>(setup.Difficulty), _sim->BeamSpeed, _sim->MissilesPerDetection, _sim->Spread, _sim->MissileGap, _sim->RadarGap,
            Str::Join(",", setup.Pickups)));
    }

    Event FallScreen::Frame(float elapsed, const std::optional<std::string>& screenshot)
    {
        auto& input = _ui.Input();

        // The pause menu: the fall waits under it.
        if (_pause->Open())
        {
            return Paused(elapsed, screenshot);
        }

        if (input.WasPressed(MenuKey::Back))
        {
            _pause->Show();
            _ui.Window().CaptureMouse(Capture::Off);
            return Event::None;
        }

        HoldTestKeys(input);
        _sim->Update(elapsed, Steering(input));
        UpdateSounds();
        auto& hud = *_view->Hud;
        if (!_sim->InIntro())
        {
            hud.Messages->Update(MathF::Min(elapsed, MaxDelta));
            hud.Tick(*_sim->Inventory, _sim->Ticks);
            Profile();
        }

        auto view = _view->Draw(*_sim);
        if (!_sim->InIntro())
        {
            hud.Draw(_sim->Health, *_sim->Inventory, _sim->Dying ? FallHud::Life::Dying : FallHud::Life::Alive, _sim->Red);
        }

        _view->DrawEffects(*_sim);
        _ui.Renderer().Present(view, _view->Backdrop(), screenshot);
        return Next();
    }

    // A frame under the pause menu: the fall drawn as it stopped, the menu over it.
    Event FallScreen::Paused(float elapsed, const std::optional<std::string>& screenshot)
    {
        auto view = _view->Draw(*_sim);
        if (!_sim->InIntro())
        {
            _view->Hud->Draw(_sim->Health, *_sim->Inventory, _sim->Dying ? FallHud::Life::Dying : FallHud::Life::Alive, _sim->Red);
        }

        _view->DrawEffects(*_sim);
        auto next = _pause->Update(elapsed);
        _ui.Renderer().Present(view, _view->Backdrop(), screenshot);
        if (!_pause->Open() && _ui.Window().Visibility() == Visibility::Shown)
        {
            _ui.Window().CaptureMouse(Capture::On);
        }

        return next;
    }

    // The fall's time, Kurt's height above the ground, his health and pickups.
    std::vector<std::string> FallScreen::Status() const
    {
        return {
            std::format("fall{} t {:.1f} kurt z {:.1f} health {} pickups {}", _sim->InIntro() ? " intro" : "", _sim->Time, _sim->KurtPosition.Z,
                _sim->Health, _sim->Collected().size()),
        };
    }

    // The level with Kurt's health and pickups, or the menu when he died.
    Event FallScreen::Next()
    {
        switch (_sim->State)
        {
            case FallSim::Outcome::Landed:
                Report("Fall ended");
                _state.Carry = Flow::FallCarry{ _sim->Health, _sim->Collected() };
                return Event::Play;
            case FallSim::Outcome::GameOver:
                Report("Fall: Kurt died");
                return Event::Menu;
            default:
                return Event::None;
        }
    }

    // The steering keys: x right, y up the screen (the turn or strafe keys, forward and back).
    Vector2 FallScreen::Steering(Input& input)
    {
        auto held = [&input](Key key) { return input.IsDown(key) ? 1 : 0; };
        auto right = held(Key::TurnRight) + held(Key::StrafeRight) - held(Key::TurnLeft) - held(Key::StrafeLeft);
        auto up = held(Key::Forward) - held(Key::Back);
        return Vector2(static_cast<float>(Math::Sign(right)), static_cast<float>(Math::Sign(up)));
    }

    // Tests: "forward" held for --walk seconds from --delay seconds of fall.
    void FallScreen::HoldTestKeys(Input& input)
    {
        if (_test.Walk <= 0.f)
        {
            return;
        }

        auto held = !_sim->InIntro() && _sim->Time >= _test.Delay && _sim->Time < _test.Delay + _test.Walk;
        input.Hold(Key::Forward, held ? Input::State::Down : Input::State::Up);
    }

    // Tests (--profile): Kurt, the camera, the wind and the health once a second of fall.
    void FallScreen::Profile()
    {
        if (!_test.Profile || _sim->Time < _nextPrint)
        {
            return;
        }

        _nextPrint += PrintInterval;
        Report("Fall");
    }

    void FallScreen::Report(const std::string& what)
    {
        auto k = _sim->KurtPosition;
        Console::WriteLine(std::format("{} t {:.1f} kurt {:.2f} {:.2f} {:.1f} camera {:.1f} wind {} health {} pickups {}", what, _sim->Time, k.X, k.Y, k.Z,
            _sim->CameraPosition.Z, _sim->Wind >> FallSim::WindShift, _sim->Health, Str::Join(",", _sim->Collected())));
    }

    void FallScreen::UpdateSounds()
    {
        auto& audio = _ui.Audio();
        audio.Set(_windVoice, _sim->WindVolume(), 1.f, 0.f);
        if (_sim->InIntro())
        {
            return;
        }

        // The minecrawler grinds from the start of the fall.
        if (_grindVoice == 0)
        {
            _grindVoice = Loop("C_GRIND");
        }

        audio.Set(_grindVoice, _sim->GrindVolume(), 1.f, 0.f);
    }

    int FallScreen::Loop(const std::string& name)
    {
        auto entry = _sounds->Get(name);
        return entry ? _ui.Audio().Play(entry->Sound, 0.f) : 0;
    }

    void FallScreen::Play(const std::string& name)
    {
        std::erase_if(_voices, [this](int v) { return !_ui.Audio().IsPlaying(v); });
        if (static_cast<int>(_voices.size()) >= MaxVoices)
        {
            return;
        }

        auto entry = _sounds->Get(name);
        if (!entry)
        {
            return;
        }

        _voices.push_back(_ui.Audio().Play(entry->Sound, 1.f));
    }

    void FallScreen::Dispose()
    {
        _ui.Window().CaptureMouse(Capture::Off);
        if (_test.Walk > 0.f)
        {
            _ui.Input().Hold(Key::Forward, Input::State::Up);
        }
    }
}
