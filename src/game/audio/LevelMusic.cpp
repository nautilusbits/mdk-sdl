#include "game/audio/LevelMusic.h"

#include <format>

#include "core/Console.h"
#include "core/DotNet.h"

namespace Mdk::Game::Audio
{
    LevelMusic::LevelMusic(SoundMixer& mixer, Mdk::Dictionary<std::string, std::string> tracks)
        : _mixer(mixer), _tracks(std::move(tracks))
    {
    }

    std::string LevelMusic::TrackOf(const std::string& arena) const
    {
        auto name = _tracks.GetValueOrDefault(arena, "");
        return name.empty() ? Corridor : name;
    }

    void LevelMusic::Enter(const std::optional<std::string>& arena)
    {
        if (!arena.has_value() || arena->empty() || *arena == _arena)
        {
            return;
        }

        _arena = *arena;
        Switch(TrackOf(*arena));
        if (Ambience.Count() != 0)
        {
            PlayAmbience(Ambience.GetValueOrDefault(*arena, ""));
        }
    }

    void LevelMusic::PlayAmbience(const std::string& name)
    {
        if (name == _ambience)
        {
            return;
        }

        _mixer.StopVoice(_ambienceVoice);
        _ambience = name;
        _ambienceVoice = name.empty() ? 0 : _mixer.PlayLooped(name);
    }

    void LevelMusic::Update(float delta)
    {
        _current.Volume = MathF::Min(_current.Volume + FadeIn * delta, static_cast<float>(SoundMixer::FullVolume));
        _fading.Volume -= FadeOut * delta;
        if (_fading.Volume < 0.f && _fading.Name.has_value())
        {
            _mixer.StopVoice(_fading.Voice);
            _fading = Track();
        }

        Apply(_current);
        Apply(_fading);
    }

    void LevelMusic::Switch(const std::string& name)
    {
        if (name == _current.Name)
        {
            return;
        }

        // Back to the fading track: they swap.
        if (name == _fading.Name)
        {
            std::swap(_current, _fading);
            return;
        }

        // The current track fades out, the new one fades in from silence.
        if (_fading.Voice != 0)
        {
            _mixer.StopVoice(_fading.Voice);
        }

        _fading = _current;
        auto voice = _mixer.PlayMusic(name);
        _current = Track{name, voice};
        if (voice != 0)
        {
            Console::WriteLine(std::format("Music: {}", name));
        }
    }

    void LevelMusic::Apply(Track& track)
    {
        if (track.Voice != 0)
        {
            _mixer.SetVolume(track.Voice, ToInt(MathF::Max(track.Volume, 0.f)));
        }
    }
}
