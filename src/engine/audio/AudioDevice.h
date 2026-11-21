#pragma once

#include <array>
#include <deque>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/DotNet.h"
#include "engine/audio/Limiter.h"

struct SDL_AudioStream;

namespace Mdk::Engine::Audio
{
    enum class Looping { Once, Forever };

    // Voices mix into a bus, each with its volume: the music (with its filter) or the effects.
    enum class Bus { Effects, Music };

    // The music's low-pass filter on or off.
    enum class Filter { Off, On };

    // Whether sounds reach the speakers or are mixed silently (tests).
    enum class Output { Speakers, Muted };

    // Sound data the mixer can play: samples as floats, interleaved when stereo.
    class Sound
    {
    public:
        Sound(std::vector<float> samples, int channels, int sampleRate, Audio::Looping looping)
            : _samples(std::move(samples)), _channels(channels), _sampleRate(sampleRate), _looping(looping)
        {
        }

        const std::vector<float>& Samples() const { return _samples; }
        int Channels() const { return _channels; }
        int SampleRate() const { return _sampleRate; }
        Audio::Looping Looping() const { return _looping; }
        int Frames() const { return static_cast<int>(_samples.size()) / _channels; }

        // From PCM: 8-bit unsigned or 16-bit signed little-endian.
        static std::shared_ptr<Sound> FromPcm(std::span<const byte> data, int channels, int sampleRate, int bitsPerSample, Audio::Looping looping);

    private:
        std::vector<float> _samples;
        int _channels;
        int _sampleRate;
        Audio::Looping _looping;
    };

    // The sound output: a software mixer of voices pushed to an SDL audio stream (stereo
    // float). Each voice has a gain, a pitch (a multiple of its sound's rate) and a pan (-1 left, 1 right,
    // the far channel attenuated like DirectSound's).
    //   voices ──resample, gain, pan──► mix buffer ──► SDL audio stream ──► device
    // Update tops the stream up once a frame. Voices go through a bus (music or
    // effects), the master volume and a limiter; a streamed voice plays what is pushed to it (a movie's sound).
    //   effects voices ─────────────────────┐
    //   music voices ──low-pass (option)─────┼──master──limiter──► stream
    class AudioDevice
    {
    public:
        // Opens the default output; muted or without one, sounds are mixed and dropped.
        explicit AudioDevice(Output output);
        AudioDevice(const AudioDevice&) = delete;
        AudioDevice& operator=(const AudioDevice&) = delete;
        ~AudioDevice();

        // The volume of everything (0-1).
        float MasterGain = 1.f;
        Filter MusicFilter = Filter::On;

        // A bus's volume (0-1).
        void SetBusGain(Bus bus, float gain) { _busGains[static_cast<int>(bus)] = gain; }

        // Starts a voice; returns its id, or 0 when all voices are busy.
        int Play(std::shared_ptr<Sound> sound, float gain, float pitch = 1.f, float pan = 0.f, Bus bus = Bus::Effects);

        // Starts a streamed voice at sampleRate: it plays what Push gives it, silent while it
        // waits. Returns its id, or 0.
        int PlayStream(int sampleRate, float gain);

        // Stereo frames for a streamed voice.
        void Push(int voice, std::span<const std::pair<float, float>> frames);

        // Seconds of sound a streamed voice still has to play.
        float Queued(int voice) const;

        // Stops every voice (a screen of the game ends).
        void StopAll();

        bool IsPlaying(int voice) const { return FindVoice(voice) != nullptr; }

        void Set(int voice, float gain, float pitch, float pan);

        void Stop(int voice);

        // A voice's gain, 0 when it isn't playing.
        float GainOf(int voice) const;

        // Mixes what the stream needs to stay QueueSeconds ahead; without a device, the delta
        // seconds that passed.
        void Update(float delta);

        // Saves the mix from now on as a stereo float WAV at path (with Output::Muted: in step
        // with the frames; --record-audio, videos).
        void Record(const std::string& path);

    private:
        static constexpr int OutputRate = 44100;
        static constexpr int OutputChannels = 2;
        // Audio kept queued ahead: enough to cover a slow frame, short enough to stay in sync.
        static constexpr float QueueSeconds = 0.06f;
        static constexpr int MaxVoices = 64;
        // DirectSound pan: the far channel loses up to 100 dB.
        static constexpr float PanRangeDb = 100.f;
        // The music is 8-bit at 14-20 kHz and hisses: the filter cuts it above 6 kHz.
        static constexpr float MusicCutoff = 6000.f;
        static constexpr int WavHeader = 44;
        static constexpr ushort FloatFormat = 3;

        // A voice playing; voices are made once (MaxVoices) and reused.
        struct Voice
        {
            std::shared_ptr<Audio::Sound> Sound;
            Audio::Bus Bus = Audio::Bus::Effects;
            float Gain = 0.f;
            float Pitch = 0.f;
            float Pan = 0.f;
            double Position = 0.;
            // A streamed voice: frames pushed and not played yet (stereo).
            std::optional<std::deque<std::pair<float, float>>> Pending;
        };

        // A slot of the voices by id, as C#'s Dictionary keeps them: a freed slot is reused
        // first (its iteration order), and removing while iterating is allowed.
        struct VoiceSlot
        {
            // 0: free.
            int Id = 0;
            AudioDevice::Voice* Voice = nullptr;
        };

        Voice* FindVoice(int id) const;
        void AddVoice(int id, Voice* voice);
        Voice* RemoveVoice(int id);
        void ClearVoices();

        void AddMusic(std::span<float> mix, std::span<const float> music, int frames);
        static bool MixStream(Voice& voice, std::span<float> mix, int frames);
        static bool MixVoice(Voice& voice, std::span<float> mix, int frames);
        static std::pair<float, float> Sample(const Sound& sound, int frame);
        void EndRecording();

        std::vector<Voice> _pool = std::vector<Voice>(MaxVoices);
        std::vector<Voice*> _spare;

        SDL_AudioStream* _stream = nullptr;
        std::vector<VoiceSlot> _voices;
        std::vector<size_t> _freeSlots;
        int _voiceCount = 0;
        std::vector<float> _mix;
        std::vector<float> _music;
        std::array<float, 2> _busGains = {1.f, 1.f};
        std::pair<float, float> _filtered;
        Limiter _limiter = Limiter(OutputRate);
        int _nextId = 1;
        // Output frames owed without a device (a frame's 1/60 s is 735 of them, not 734).
        double _owed = 0.;
        // The mix saved as a WAV (Record), or null.
        std::unique_ptr<std::ofstream> _recording;
    };
}
