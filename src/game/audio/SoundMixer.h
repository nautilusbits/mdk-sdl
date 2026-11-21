#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "game/audio/ISoundSource.h"

namespace Mdk::Game::Audio
{
    // The game's sound effects, mixed like the original (0x402b40-0x403c70, listener update
    // 0x403348; see godot-mdk docs/sound.md).
    //
    // Volume is linear in decibels over 25 dB: dB = -25 + 25 * volume / 0x7FFF (never silent).
    // 3D voices: full volume up to 20 units, linear to 0 at 250; Doppler at 1100 u/s (0.25-3).
    // In sniper mode the scope listens: sounds near the crosshair are loud, those behind silent (0x403750).
    // A new sound is dropped when all voices are busy.
    //
    //   Play("LAND")                     2D, the sound's default volume
    //   PlayAt("EXPLODE", point)         3D, fixed at a point
    //   PlayOn("DUMMY", source, offset)  3D, following something (an object)
    //
    // Voices are made once and reused: a sound played makes nothing.
    class SoundMixer
    {
    public:
        // A new voice starts even if the sound plays (New), stops its voices first
        // (Restart), or starts only if it isn't playing (Once).
        enum class Start { New, Restart, Once };

        // A sound and its default volume (0-0x7FFF, from the SNI entry).
        struct Entry
        {
            std::shared_ptr<Engine::Audio::Sound> Sound;
            int Volume = 0;

            bool operator==(const Entry&) const = default;
        };

        static constexpr int FullVolume = 0x7FFF;

        SoundMixer(Engine::Audio::AudioDevice& device, std::function<std::optional<Entry>(const std::string&)> sounds);

        // The listener: position and right (MDK coordinates).
        Vector3 ListenerPosition{};
        Vector3 ListenerRight = Vector3::UnitX();
        // The listener's line of sight and up, for the scope.
        Vector3 ListenerForward = Vector3::UnitY();
        Vector3 ListenerUp = Vector3::UnitZ();
        // The sniper scope's zoom (1 to 0.25) while Kurt snipes, else 0.
        float ScopeZoom = 0;

        // Plays a sound without position. Returns the voice, or 0.
        int Play(const std::string& name, Start start = Start::New);

        int PlayAt(const std::string& name, Vector3 point, Start start = Start::New);

        int PlayOn(const std::string& name, std::function<Vector3()> position, Start start = Start::New);

        // Plays a sound on source, offset from it (in its frame).
        int PlayOn(const std::string& name, ISoundSource* source, Vector3 offset, Start start = Start::New);

        // Plays the music (on the music bus, without position).
        int PlayMusic(const std::string& name);

        // Plays a sound without position, looping even if its SNI entry doesn't (Kurt's chain gun).
        int PlayLooped(const std::string& name);

        bool IsPlaying(const std::string& name) const;

        // Whether a voice (as returned by the Play methods) still plays.
        bool IsVoicePlaying(int id) const { return id != 0 && VoiceOf(id) != nullptr; }

        void Stop(const std::string& name);

        void StopVoice(int id);

        // A voice's volume (0-0x7FFF), e.g. the XD2's quieter DUMMY (0x4032e8).
        void SetVolume(int id, int volume);

        // A 2D voice's pitch (1: the sound's rate), e.g. BUTSLIDE at 15000 Hz instead of 11025.
        void SetPitch(int id, float pitch);

        // Decibels to a linear gain, for a volume (0-0x7FFF): linear over 25 dB.
        static float Gain(float volume);

        // Once a frame: forgets ended voices and updates the 3D ones from the listener.
        void Update(float delta);

        // The gain of a sound through the scope (0x403750), from its place seen from the
        // listener (local: right, up, depth): its offset from the line of sight (in
        // scope half-widths, beyond 2 units) and its depth; behind the listener it's 0.
        static float ScopeGain(Vector3 local, float zoom);

    private:
        static constexpr float FloorDb = -25.f;
        static constexpr float Near = 20.f;
        static constexpr float Far = 250.f;
        static constexpr float SpeedOfSound = 1100.f;
        static constexpr float PitchMin = 0.25f;
        static constexpr float PitchMax = 3.f;
        // Scope listening (0x403750): the scope is 384 x 280 pixels, its focal length 384 / zoom.
        static constexpr float ScopeAxis = 2.f;
        static constexpr float ScopeRatio = 384.f / 280.f;
        static constexpr float ScopeDepth = 300.f;
        static constexpr float ScopeEdge = 1.3f;

        // Where a voice is heard from: nowhere (2D), a point, or something moving.
        enum class Place { Flat, Point, Source, Follow };

        // A sound loops as its SNI entry says, or always.
        enum class Repeat { AsStored, Forever };

        struct Voice
        {
            int Id = 0;
            std::string Name;
            int Volume = 0;
            SoundMixer::Place Place = Place::Flat;
            Vector3 Point{};
            ISoundSource* Source = nullptr;
            std::function<Vector3()> Follow;
            // Distance at the last update, -1 before the first.
            float Distance = -1.f;

            bool Positioned() const { return Place != Place::Flat; }

            Vector3 Position() const;
        };

        Engine::Audio::AudioDevice& _device;
        std::function<std::optional<Entry>(const std::string&)> _sounds;

        std::vector<std::shared_ptr<Voice>> _voices;
        std::stack<std::shared_ptr<Voice>> _spare;
        // Looping copies of sounds that don't loop by themselves.
        std::unordered_map<std::string, std::shared_ptr<Engine::Audio::Sound>> _looped;

        // A voice by id, or null (no lambda: called every frame).
        Voice* VoiceOf(int id) const;

        // A voice ended or stopped: back to the spares.
        void Forget(int index);

        int Launch(const std::string& name, Start start, Place place, Vector3 point = {}, ISoundSource* source = nullptr,
            std::function<Vector3()> follow = nullptr, Repeat repeat = Repeat::AsStored,
            Engine::Audio::Bus bus = Engine::Audio::Bus::Effects);

        std::shared_ptr<Engine::Audio::Sound> Looped(const std::string& name, const std::shared_ptr<Engine::Audio::Sound>& sound);

        // Volume, pan and pitch of a 3D voice from its place relative to the listener (0x40347c).
        void Update3D(Voice& voice, float delta);
    };
}
