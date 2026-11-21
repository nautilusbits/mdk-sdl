#include "game/stream/StreamScreen.h"

#include <algorithm>
#include <format>

#include "core/Console.h"
#include "core/DotNet.h"
#include "engine/platform/Window.h"
#include "formats/Fti.h"
#include "formats/MdkData.h"
#include "formats/TextureArchive.h"
#include "game/flow/GameState.h"
#include "game/flow/Settings.h"
#include "game/menu/Ui.h"

namespace Mdk::Game::Stream
{
    using Engine::Platform::Capture;
    using Engine::Platform::Key;
    using Engine::Platform::MenuKey;
    using Engine::Platform::Visibility;
    using Flow::Event;

    namespace
    {
        // C#'s enum ToString in the messages.
        const char* NameOf(StreamTube::Kind kind)
        {
            return kind == StreamTube::Kind::Gunter ? "Gunter" : "Normal";
        }

        const char* NameOf(Outcome outcome)
        {
            return outcome == Outcome::Died ? "Died" : "Survived";
        }
    }

    StreamScreen::StreamScreen(Menu::Ui& ui, int level, int health, std::shared_ptr<WatcomRandom> random)
        : _ui(ui), _level(level)
    {
        // No pointer over the flight (as in a level); a hidden test window keeps it free.
        if (ui.Window().Visibility() == Visibility::Shown)
        {
            ui.Window().CaptureMouse(Capture::On);
        }

        auto index = Math::Clamp(Flow::GameState::IndexOf(level), 0, LastStatisticsIndex + 1);
        auto kind = index > LastStatisticsIndex ? StreamTube::Kind::Gunter : StreamTube::Kind::Normal;
        auto difficulty = ui.Settings().Difficulty;
        _bni = Formats::Bni::Load(ui.Data()->PathOf(Archive));
        _otherModel = kind == StreamTube::Kind::Gunter ? "GUNTA" : "SWH150";
        _otherAnimation = kind == StreamTube::Kind::Gunter ? "GUNTANIM" : "SWHANM";

        auto tube = std::make_shared<StreamTube>(index, difficulty, kind, random);
        _view = std::make_unique<StreamView>(ui.Renderer(), _bni, LoadPalette(), Formats::TextureArchive::Load(ui.Data()->PathOf(Textures)), ui.CanvasMods());
        _flight = std::make_unique<Flight>(tube, kind, difficulty, random, health, ClipOf(KurtAnimation), ClipOf(RescueAnimation), ClipOf(_otherAnimation));

        _ui.Play(Menu::Ui::SoundOf(*_bni, WindSound, Engine::Audio::Looping::Forever));
        auto last = tube->Head - 1;
        auto ring = tube->Origin(last);
        Console::WriteLine(std::format("Stream after LEVEL{}: {}, turn {}, radius {}-{}, {} lights, ring {} at {:.3f} {:.3f} {:.3f}",
            level, NameOf(kind), Str::FromFloat(tube->MaxTurn), Str::FromFloat(tube->MinRadius), Str::FromFloat(tube->MaxRadius),
            _flight->LightCount, last, ring.X, ring.Y, ring.Z));
    }

    std::shared_ptr<Formats::Palette> StreamScreen::LoadPalette()
    {
        auto systemSize = Formats::Palette::ArenaFirstIndex * RgbSize;
        std::vector<byte> rgb(Formats::Palette::Size * RgbSize);
        auto system = _ui.Fti->GetBytes(SystemPalette);
        std::copy_n(system.begin(), systemSize, rgb.begin());
        const auto& entry = _bni->Entries().At(StreamPalette);
        const auto& bytes = *_bni->Bytes();
        std::copy_n(bytes.begin() + entry.Offset + systemSize, rgb.size() - systemSize, rgb.begin() + systemSize);
        return Formats::Palette::FromRgb(rgb);
    }

    Clip StreamScreen::ClipOf(const std::string& animation)
    {
        auto a = _view->AnimationOf(animation);
        return Clip{a->FrameCount, a->Speed};
    }

    Event StreamScreen::Frame(float elapsed, const std::optional<std::string>& screenshot)
    {
        auto& input = _ui.Input();
        if (input.WasPressed(MenuKey::Back))
        {
            _flight->Stop();
        }

        _flight->Update(elapsed, SteeringOf(input));
        PlaySounds();
        Report();

        auto animation = _flight->Bones == nullptr ? KurtAnimation : RescueAnimation;
        _view->Draw(*_flight, animation, _otherModel, _otherAnimation);
        _ui.Renderer().Present(_view->ViewOf(*_flight), Menu::Ui::Black, screenshot);
        if (!_flight->Ended)
        {
            return Event::None;
        }

        return _flight->Outcome() == Outcome::Died ? Event::Menu : Event::StreamEnded;
    }

    std::vector<std::string> StreamScreen::Status() const
    {
        return {
            std::format("stream after LEVEL{} segment {} t {:.1f} speed {:.3f} health {}",
                _level, _flight->Tube()->Tail, _flight->Time, _flight->Kurt->Speed, Health()),
        };
    }

    Steering StreamScreen::SteeringOf(Engine::Platform::Input& input)
    {
        auto held = [&](Key key) { return input.IsDown(key) ? 1 : 0; };
        auto right = held(Key::TurnRight) + held(Key::StrafeRight) - held(Key::TurnLeft) - held(Key::StrafeLeft);
        auto up = held(Key::Forward) - held(Key::Back);
        return Steering{Math::Sign(right), Math::Sign(up)};
    }

    void StreamScreen::PlaySounds()
    {
        const auto& events = _flight->Events;
        if (std::find(events.begin(), events.end(), FlightEvent::WallHit) != events.end() && !_ui.Audio().IsPlaying(_hitVoice))
        {
            _hitVoice = _ui.Play(SoundOf(HitSound));
        }

        std::erase_if(_voices, [&](int v) { return !_ui.Audio().IsPlaying(v); });
        for (const auto& name : _flight->Sounds)
        {
            if (static_cast<int>(_voices.size()) >= Players)
            {
                return;
            }

            _voices.push_back(_ui.Play(SoundOf(name)));
        }
    }

    std::shared_ptr<Engine::Audio::Sound> StreamScreen::SoundOf(const std::string& name)
    {
        auto found = _sounds.find(name);
        if (found != _sounds.end())
        {
            return found->second;
        }

        auto sound = Menu::Ui::SoundOf(*_bni, name);
        _sounds[name] = sound;
        return sound;
    }

    void StreamScreen::Report()
    {
        const auto& tube = *_flight->Tube();
        const auto& kurt = *_flight->Kurt;
        for (auto e : _flight->Events)
        {
            auto firstHit = e == FlightEvent::WallHit && _wallHits++ == 0;
            std::optional<std::string> line;
            switch (e)
            {
            case FlightEvent::WallHit:
                if (firstHit)
                {
                    line = std::format("Stream: first wall hit at segment {}, t {:.3f} x {:.3f} z {:.3f} yaw {:.2f} speed {:.3f} health {}",
                        tube.Tail, kurt.T, kurt.X, kurt.Z, kurt.Yaw, kurt.Speed, _flight->Health);
                }
                break;
            case FlightEvent::BonesCame:
                line = std::format("Stream: Bones comes at segment {}, health {}, {:.1f} s", tube.Tail, _flight->Health, _flight->Time);
                break;
            case FlightEvent::KurtDied:
                line = std::format("Stream: Kurt died at segment {}, {:.1f} s", tube.Tail, _flight->Time);
                break;
            case FlightEvent::BonusTaken:
                line = std::format("Stream: health bonus, health {}", _flight->Health);
                break;
            case FlightEvent::Ended:
                line = std::format("Stream ended after LEVEL{}: {}, health {}, segment {}, {:.1f} s",
                    _level, NameOf(_flight->Outcome()), _flight->Health, tube.Tail, _flight->Time);
                break;
            }

            if (line.has_value())
            {
                Console::WriteLine(*line);
            }
        }
    }

    void StreamScreen::Dispose()
    {
        _ui.Window().CaptureMouse(Capture::Off);
    }
}
