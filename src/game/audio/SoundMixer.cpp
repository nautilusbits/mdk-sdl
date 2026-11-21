#include "game/audio/SoundMixer.h"

#include "core/DotNet.h"

namespace Mdk::Game::Audio
{
    using Engine::Audio::Bus;
    using Engine::Audio::Looping;
    using Engine::Audio::Sound;

    Vector3 SoundMixer::Voice::Position() const
    {
        switch (Place)
        {
            case Place::Point:
                return Point;
            case Place::Source:
                return Source->SoundPosition(Point);
            default:
                return Follow();
        }
    }

    SoundMixer::SoundMixer(Engine::Audio::AudioDevice& device, std::function<std::optional<Entry>(const std::string&)> sounds)
        : _device(device), _sounds(std::move(sounds))
    {
    }

    int SoundMixer::Play(const std::string& name, Start start)
    {
        return Launch(name, start, Place::Flat);
    }

    int SoundMixer::PlayAt(const std::string& name, Vector3 point, Start start)
    {
        return Launch(name, start, Place::Point, point);
    }

    int SoundMixer::PlayOn(const std::string& name, std::function<Vector3()> position, Start start)
    {
        return Launch(name, start, Place::Follow, {}, nullptr, std::move(position));
    }

    int SoundMixer::PlayOn(const std::string& name, ISoundSource* source, Vector3 offset, Start start)
    {
        return Launch(name, start, Place::Source, offset, source);
    }

    int SoundMixer::PlayMusic(const std::string& name)
    {
        return Launch(name, Start::New, Place::Flat, {}, nullptr, nullptr, Repeat::AsStored, Bus::Music);
    }

    int SoundMixer::PlayLooped(const std::string& name)
    {
        return Launch(name, Start::New, Place::Flat, {}, nullptr, nullptr, Repeat::Forever);
    }

    bool SoundMixer::IsPlaying(const std::string& name) const
    {
        for (const auto& voice : _voices)
        {
            if (voice->Name == name)
            {
                return true;
            }
        }

        return false;
    }

    SoundMixer::Voice* SoundMixer::VoiceOf(int id) const
    {
        for (const auto& voice : _voices)
        {
            if (voice->Id == id)
            {
                return voice.get();
            }
        }

        return nullptr;
    }

    void SoundMixer::Stop(const std::string& name)
    {
        for (auto i = static_cast<int>(_voices.size()) - 1; i >= 0; i--)
        {
            if (_voices[i]->Name == name)
            {
                _device.Stop(_voices[i]->Id);
                Forget(i);
            }
        }
    }

    void SoundMixer::StopVoice(int id)
    {
        _device.Stop(id);
        for (auto i = static_cast<int>(_voices.size()) - 1; i >= 0; i--)
        {
            if (_voices[i]->Id == id)
            {
                Forget(i);
            }
        }
    }

    void SoundMixer::SetVolume(int id, int volume)
    {
        auto* voice = VoiceOf(id);
        if (voice == nullptr)
        {
            return;
        }

        voice->Volume = volume;
        if (!voice->Positioned())
        {
            _device.Set(id, Gain(static_cast<float>(volume)), 1.f, 0.f);
        }
    }

    void SoundMixer::SetPitch(int id, float pitch)
    {
        auto* voice = VoiceOf(id);
        if (voice != nullptr && !voice->Positioned())
        {
            _device.Set(id, Gain(static_cast<float>(voice->Volume)), pitch, 0.f);
        }
    }

    float SoundMixer::Gain(float volume)
    {
        auto db = FloorDb * (1.f - Math::Clamp(volume, 0.f, static_cast<float>(FullVolume)) / FullVolume);
        return MathF::Pow(10.f, db / 20.f);
    }

    void SoundMixer::Update(float delta)
    {
        for (auto i = static_cast<int>(_voices.size()) - 1; i >= 0; i--)
        {
            if (!_device.IsPlaying(_voices[i]->Id))
            {
                Forget(i);
            }
        }

        for (const auto& voice : _voices)
        {
            if (voice->Positioned())
            {
                Update3D(*voice, delta);
            }
        }
    }

    void SoundMixer::Forget(int index)
    {
        auto voice = _voices[index];
        voice->Source = nullptr;
        voice->Follow = nullptr;
        _voices.erase(_voices.begin() + index);
        _spare.push(voice);
    }

    int SoundMixer::Launch(const std::string& name, Start start, Place place, Vector3 point, ISoundSource* source,
        std::function<Vector3()> follow, Repeat repeat, Bus bus)
    {
        auto entry = _sounds(name);
        if (!entry.has_value() || (start == Start::Once && IsPlaying(name)))
        {
            return 0;
        }

        if (start == Start::Restart)
        {
            Stop(name);
        }

        auto sound = repeat == Repeat::Forever ? Looped(name, entry->Sound) : entry->Sound;
        auto id = _device.Play(sound, Gain(static_cast<float>(entry->Volume)), 1.f, 0.f, bus);
        if (id == 0)
        {
            return 0;
        }

        std::shared_ptr<Voice> voice;
        if (!_spare.empty())
        {
            voice = _spare.top();
            _spare.pop();
        }
        else
        {
            voice = std::make_shared<Voice>();
        }

        voice->Id = id;
        voice->Name = name;
        voice->Volume = entry->Volume;
        voice->Distance = -1.f;
        voice->Place = place;
        voice->Point = point;
        voice->Source = source;
        voice->Follow = std::move(follow);
        _voices.push_back(voice);
        if (voice->Positioned())
        {
            Update3D(*voice, 0.f);
        }

        return id;
    }

    std::shared_ptr<Sound> SoundMixer::Looped(const std::string& name, const std::shared_ptr<Sound>& sound)
    {
        if (sound->Looping() == Looping::Forever)
        {
            return sound;
        }

        auto it = _looped.find(name);
        if (it != _looped.end())
        {
            return it->second;
        }

        auto looped = std::make_shared<Sound>(sound->Samples(), sound->Channels(), sound->SampleRate(), Looping::Forever);
        _looped[name] = looped;
        return looped;
    }

    float SoundMixer::ScopeGain(Vector3 local, float zoom)
    {
        auto depth = local.Z;
        if (depth <= 0.f)
        {
            return 0.f;
        }

        auto offset = Vector2(local.X, local.Y);
        auto r = offset.Length();
        auto screen = Vector2::Zero();
        if (r > ScopeAxis)
        {
            offset -= offset * ScopeAxis / r;
            screen = Vector2(offset.X * 2.f / zoom, offset.Y * 2.f * ScopeRatio / zoom);
        }

        auto gain = MathF::Min(1.f, ScopeDepth / (zoom * depth));
        return Math::Clamp(gain * (ScopeEdge - screen.Length() / depth), 0.f, 1.f);
    }

    void SoundMixer::Update3D(Voice& voice, float delta)
    {
        auto offset = voice.Position() - ListenerPosition;
        auto distance = Math::Max(offset.Length(), 1.f);

        // Doppler: coming closer raises the pitch.
        auto pitch = 1.f;
        if (voice.Distance >= 0.f && delta > 0.f)
        {
            pitch = Math::Clamp(1.f + (voice.Distance - distance) / (delta * SpeedOfSound), PitchMin, PitchMax);
        }

        voice.Distance = distance;
        auto gain = ScopeZoom > 0.f
            ? ScopeGain(Vector3(Vector3::Dot(offset, ListenerRight), Vector3::Dot(offset, ListenerUp), Vector3::Dot(offset, ListenerForward)), ScopeZoom)
            : Math::Clamp((Far - distance) / (Far - Near), 0.f, 1.f);
        auto side = Vector3::Dot(offset, ListenerRight) / distance;
        _device.Set(voice.Id, Gain(voice.Volume * gain), pitch, side);
    }
}
