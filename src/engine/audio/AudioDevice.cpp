#include "engine/audio/AudioDevice.h"

#include <algorithm>

#include <SDL3/SDL.h>

#include "core/FileStream.h"

namespace Mdk::Engine::Audio
{
    namespace
    {
        // BinaryWriter.Write: the value's bytes, little-endian (the host's).
        template <typename T>
        void Write(std::ostream& to, T value)
        {
            to.write(reinterpret_cast<const char*>(&value), sizeof(T));
        }

        void Write(std::ostream& to, std::string_view text)
        {
            to.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
    }

    std::shared_ptr<Sound> Sound::FromPcm(std::span<const byte> data, int channels, int sampleRate, int bitsPerSample, Audio::Looping looping)
    {
        constexpr float ByteScale = 1.f / 128.f;
        constexpr float ShortScale = 1.f / 32768.f;
        constexpr int ByteMiddle = 128;
        if (bitsPerSample == 8)
        {
            std::vector<float> bytes(data.size());
            for (size_t i = 0; i < data.size(); i++)
            {
                bytes[i] = static_cast<float>(data[i] - ByteMiddle) * ByteScale;
            }

            return std::make_shared<Sound>(std::move(bytes), channels, sampleRate, looping);
        }

        std::vector<float> samples(data.size() / 2);
        for (size_t i = 0; i < samples.size(); i++)
        {
            auto value = static_cast<std::int16_t>(data[i * 2] | (data[i * 2 + 1] << 8));
            samples[i] = static_cast<float>(value) * ShortScale;
        }

        return std::make_shared<Sound>(std::move(samples), channels, sampleRate, looping);
    }

    AudioDevice::AudioDevice(Output output)
    {
        for (auto& voice : _pool)
        {
            _spare.push_back(&voice);
        }

        if (output == Output::Muted || !SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            return;
        }

        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32LE;
        spec.channels = OutputChannels;
        spec.freq = OutputRate;
        _stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (_stream != nullptr)
        {
            SDL_ResumeAudioStreamDevice(_stream);
        }
    }

    int AudioDevice::Play(std::shared_ptr<Sound> sound, float gain, float pitch, float pan, Bus bus)
    {
        if (_voiceCount >= MaxVoices)
        {
            return 0;
        }

        auto* voice = _spare.back();
        _spare.pop_back();
        voice->Sound = std::move(sound);
        voice->Gain = gain;
        voice->Pitch = pitch;
        voice->Pan = pan;
        voice->Bus = bus;
        voice->Position = 0.;
        voice->Pending.reset();
        AddVoice(_nextId, voice);
        return _nextId++;
    }

    int AudioDevice::PlayStream(int sampleRate, float gain)
    {
        auto id = Play(std::make_shared<Sound>(std::vector<float>{0.f, 0.f}, 2, sampleRate, Looping::Forever), gain);
        if (id != 0)
        {
            FindVoice(id)->Pending.emplace();
        }

        return id;
    }

    void AudioDevice::Push(int voice, std::span<const std::pair<float, float>> frames)
    {
        auto* v = FindVoice(voice);
        if (v == nullptr || !v->Pending)
        {
            return;
        }

        v->Pending->insert(v->Pending->end(), frames.begin(), frames.end());
    }

    float AudioDevice::Queued(int voice) const
    {
        auto* v = FindVoice(voice);
        return v != nullptr && v->Pending ? static_cast<float>(v->Pending->size()) / static_cast<float>(v->Sound->SampleRate()) : 0.f;
    }

    void AudioDevice::StopAll()
    {
        for (auto& slot : _voices)
        {
            if (slot.Id != 0)
            {
                _spare.push_back(slot.Voice);
            }
        }

        ClearVoices();
    }

    void AudioDevice::Set(int voice, float gain, float pitch, float pan)
    {
        auto* v = FindVoice(voice);
        if (v == nullptr)
        {
            return;
        }

        v->Gain = gain;
        v->Pitch = pitch;
        v->Pan = pan;
    }

    void AudioDevice::Stop(int voice)
    {
        auto* stopped = RemoveVoice(voice);
        if (stopped != nullptr)
        {
            _spare.push_back(stopped);
        }
    }

    float AudioDevice::GainOf(int voice) const
    {
        auto* v = FindVoice(voice);
        return v != nullptr ? v->Gain : 0.f;
    }

    void AudioDevice::Update(float delta)
    {
        constexpr int bytesPerFrame = OutputChannels * static_cast<int>(sizeof(float));
        _owed += OutputRate * static_cast<double>(delta);
        auto frames = _stream == nullptr
            ? ToInt(_owed)
            : ToInt(OutputRate * QueueSeconds) - SDL_GetAudioStreamQueued(_stream) / bytesPerFrame;
        _owed -= _stream == nullptr ? frames : _owed;
        if (frames <= 0)
        {
            return;
        }

        auto samples = static_cast<size_t>(frames * OutputChannels);
        if (_mix.size() < samples)
        {
            _mix.resize(samples);
        }

        if (_music.size() < samples)
        {
            _music.resize(samples);
        }

        auto mix = std::span<float>(_mix).first(samples);
        auto music = std::span<float>(_music).first(samples);
        std::fill(mix.begin(), mix.end(), 0.f);
        std::fill(music.begin(), music.end(), 0.f);
        // Removing while enumerating is allowed: the enumeration goes on.
        for (size_t s = 0; s < _voices.size(); s++)
        {
            auto [id, voice] = _voices[s];
            if (id == 0)
            {
                continue;
            }

            auto mixed = voice->Pending ? MixStream(*voice, mix, frames) : MixVoice(*voice, voice->Bus == Bus::Music ? music : mix, frames);
            if (!mixed)
            {
                RemoveVoice(id);
                _spare.push_back(voice);
            }
        }

        AddMusic(mix, music, frames);
        _limiter.Process(mix);
        if (_recording != nullptr)
        {
            _recording->write(reinterpret_cast<const char*>(mix.data()), static_cast<std::streamsize>(mix.size_bytes()));
        }

        if (_stream == nullptr)
        {
            return;
        }

        SDL_PutAudioStreamData(_stream, mix.data(), static_cast<int>(mix.size_bytes()));
    }

    // The music bus, filtered (one-pole low-pass), into the mix; then the volumes.
    void AudioDevice::AddMusic(std::span<float> mix, std::span<const float> music, int frames)
    {
        auto alpha = 1.f - MathF::Exp(-2.f * MathF::PI * MusicCutoff / OutputRate);
        auto effects = _busGains[static_cast<int>(Bus::Effects)] * MasterGain;
        auto musicGain = _busGains[static_cast<int>(Bus::Music)] * MasterGain;
        for (int f = 0; f < frames; f++)
        {
            auto l = music[f * 2];
            auto r = music[f * 2 + 1];
            if (MusicFilter == Filter::On)
            {
                _filtered = {_filtered.first + (l - _filtered.first) * alpha, _filtered.second + (r - _filtered.second) * alpha};
                std::tie(l, r) = _filtered;
            }

            mix[f * 2] = mix[f * 2] * effects + l * musicGain;
            mix[f * 2 + 1] = mix[f * 2 + 1] * effects + r * musicGain;
        }
    }

    // A streamed voice into the mix (nearest sample: its rate is close to the output's).
    // It never ends by itself.
    bool AudioDevice::MixStream(Voice& voice, std::span<float> mix, int frames)
    {
        auto step = static_cast<double>(voice.Sound->SampleRate()) / OutputRate;
        auto& pending = *voice.Pending;
        for (int f = 0; f < frames && !pending.empty(); f++)
        {
            auto [l, r] = pending.front();
            mix[f * 2] += l * voice.Gain;
            mix[f * 2 + 1] += r * voice.Gain;
            voice.Position += step;
            while (voice.Position >= 1. && !pending.empty())
            {
                voice.Position -= 1.;
                pending.pop_front();
            }
        }

        return true;
    }

    // Adds a voice to the mix (linear interpolation). Returns false once it has ended.
    bool AudioDevice::MixVoice(Voice& voice, std::span<float> mix, int frames)
    {
        auto& sound = *voice.Sound;
        auto step = voice.Pitch * static_cast<float>(sound.SampleRate()) / OutputRate;
        auto far = MathF::Pow(10.f, -PanRangeDb * Math::Abs(voice.Pan) / 20.f);
        auto left = voice.Gain * (voice.Pan > 0.f ? far : 1.f);
        auto right = voice.Gain * (voice.Pan < 0.f ? far : 1.f);
        for (int f = 0; f < frames; f++)
        {
            if (voice.Position >= sound.Frames())
            {
                if (sound.Looping() == Looping::Once)
                {
                    return false;
                }

                voice.Position -= sound.Frames();
            }

            auto i = ToInt(voice.Position);
            auto t = static_cast<float>(voice.Position - i);
            auto next = i + 1 < sound.Frames() ? i + 1 : (sound.Looping() == Looping::Forever ? 0 : i);
            auto [l, r] = Sample(sound, i);
            auto [nl, nr] = Sample(sound, next);
            mix[f * 2] += (l + (nl - l) * t) * left;
            mix[f * 2 + 1] += (r + (nr - r) * t) * right;
            voice.Position += step;
        }

        return true;
    }

    std::pair<float, float> AudioDevice::Sample(const Sound& sound, int frame)
    {
        if (sound.Channels() == 1)
        {
            auto s = sound.Samples()[frame];
            return {s, s};
        }

        return {sound.Samples()[frame * 2], sound.Samples()[frame * 2 + 1]};
    }

    void AudioDevice::Record(const std::string& path)
    {
        _recording = std::make_unique<std::ofstream>(FileStream::Create(path));
        std::array<char, WavHeader> header{};
        _recording->write(header.data(), header.size());
    }

    // The RIFF header, now that the data's size is known.
    void AudioDevice::EndRecording()
    {
        if (_recording == nullptr)
        {
            return;
        }

        auto& w = *_recording;
        auto data = static_cast<int>(w.tellp()) - WavHeader;
        w.seekp(0, std::ios::beg);
        Write(w, std::string_view("RIFF"));
        Write(w, data + WavHeader - 8);
        Write(w, std::string_view("WAVEfmt "));
        Write(w, 16);
        Write(w, FloatFormat);
        Write(w, static_cast<ushort>(OutputChannels));
        Write(w, OutputRate);
        Write(w, OutputRate * OutputChannels * static_cast<int>(sizeof(float)));
        Write(w, static_cast<ushort>(OutputChannels * sizeof(float)));
        Write(w, static_cast<ushort>(sizeof(float) * 8));
        Write(w, std::string_view("data"));
        Write(w, data);
        w.close();
        _recording = nullptr;
    }

    AudioDevice::~AudioDevice()
    {
        EndRecording();
        if (_stream != nullptr)
        {
            SDL_DestroyAudioStream(_stream);
        }

        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    AudioDevice::Voice* AudioDevice::FindVoice(int id) const
    {
        for (auto& slot : _voices)
        {
            if (slot.Id == id && id != 0)
            {
                return slot.Voice;
            }
        }

        return nullptr;
    }

    void AudioDevice::AddVoice(int id, Voice* voice)
    {
        _voiceCount++;
        if (_freeSlots.empty())
        {
            _voices.push_back({id, voice});
            return;
        }

        _voices[_freeSlots.back()] = {id, voice};
        _freeSlots.pop_back();
    }

    AudioDevice::Voice* AudioDevice::RemoveVoice(int id)
    {
        for (size_t s = 0; s < _voices.size(); s++)
        {
            if (_voices[s].Id != id || id == 0)
            {
                continue;
            }

            auto* voice = _voices[s].Voice;
            _voices[s] = {};
            _freeSlots.push_back(s);
            _voiceCount--;
            return voice;
        }

        return nullptr;
    }

    void AudioDevice::ClearVoices()
    {
        _voices.clear();
        _freeSlots.clear();
        _voiceCount = 0;
    }
}
