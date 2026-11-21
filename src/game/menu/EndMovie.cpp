#include "game/menu/EndMovie.h"

#include "core/Files.h"
#include "engine/audio/AudioDevice.h"
#include "engine/platform/Input.h"
#include "formats/Bni.h"
#include "formats/MdkData.h"
#include "game/menu/Ui.h"
#include "game/menu/VideoPlayer.h"

namespace Mdk::Game::Menu
{
    const std::unordered_map<int, std::string>& EndMovie::FrameSounds()
    {
        static const std::unordered_map<int, std::string> sounds{
            {1, "DOGSHIP"}, {129, "DROP"}, {133, "FLYBY"}, {186, "EXPLODE1"}, {194, "ENDEXP"}, {196, "EXPLODE1"},
        };
        return sounds;
    }

    EndMovie::EndMovie(Ui& ui) : _ui(ui)
    {
        _video = std::make_shared<VideoPlayer>(ui);
        auto path = ui.Data()->PathOf(Sounds);
        _sounds = File::Exists(path) ? Formats::Bni::Load(path) : nullptr;
        _video->FrameShown = [this](int frame) { OnFrame(frame); };
        _video->Finished = [this] { PlayMovie(); };
        if (!_video->Play(Video))
        {
            PlayMovie();
        }
    }

    Flow::Event EndMovie::Frame(float elapsed, const std::optional<std::string>& screenshot)
    {
        _ui.View->Layout(ScreenView::Fit::Inside);
        if (_ui.Input().WasPressed(Engine::Platform::MenuKey::Back))
        {
            _next = Flow::Event::Menu;
        }

        _video->Update(elapsed);
        _video->Draw();
        _ui.Present(screenshot);
        return _next;
    }

    std::vector<std::string> EndMovie::Status() const
    {
        return {_video->Playing() ? "end movies" : "end movies: done"};
    }

    void EndMovie::OnFrame(int frame)
    {
        auto sound = FrameSounds().find(frame);
        if (sound != FrameSounds().end())
        {
            Play(sound->second);
        }

        auto loop = _voices.find(Looped);
        if (frame == LoopEnd && loop != _voices.end())
        {
            _ui.Audio().Stop(loop->second);
        }

        _video->Brighten(Flash(frame));
        if (frame == FlashPeak)
        {
            _video->Hold(FlashHold);
        }
    }

    float EndMovie::Flash(int frame)
    {
        if (frame < FlashStart || frame > FlashEnd)
        {
            return 0.f;
        }

        auto t = frame < FlashPeak
            ? static_cast<float>(frame - FlashStart) / (FlashPeak - FlashStart)
            : static_cast<float>(FlashEnd - frame) / (FlashEnd - FlashPeak);
        return MathF::Round(MaxColour * t) / MaxColour;
    }

    void EndMovie::PlayMovie()
    {
        // The handlers replace PlayMovie (C#: Finished -= PlayMovie; Finished += ...).
        _video->Finished = nullptr;
        for (const auto& [name, voice] : _voices)
        {
            _ui.Audio().Stop(voice);
        }

        _video->Brighten(0.f);
        _video->Finished = [this] { _next = Flow::Event::Menu; };
        if (!_video->PlayMovie(Movie))
        {
            _next = Flow::Event::Menu;
        }
    }

    void EndMovie::Play(const std::string& name)
    {
        if (_sounds == nullptr)
        {
            return;
        }

        _voices[name] = _ui.Play(Ui::SoundOf(*_sounds, name,
            name == Looped ? Engine::Audio::Looping::Forever : Engine::Audio::Looping::Once));
    }

    void EndMovie::Dispose()
    {
        _video->Stop();
        _ui.Audio().StopAll();
    }
}
