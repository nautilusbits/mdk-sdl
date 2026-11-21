#pragma once

#include <optional>
#include <string>

#include "core/Dictionary.h"
#include "game/audio/SoundMixer.h"

namespace Mdk::Game::Audio
{
    // The music of Kurt's arena (0x419158, fades 0x418ffc; godot-mdk level_audio.gd,
    // docs/sound.md "Music"). An arena plays its track (the CMI's), a corridor (no track) the level's
    // CORRIDOR, NONE nothing. A new track fades in from -25 dB to full in about 8.7 s while
    // the previous one fades out in about 4.35 s, then stops; going back to the fading track swaps them.
    //
    //   arena ──► track ──┬─ same as current: nothing
    //                     ├─ the fading one: they swap (it goes on where it was)
    //                     └─ else: current ──► fading (-0x100/frame), new ──► current (+0x80/frame)
    class LevelMusic
    {
    public:
        static constexpr const char* Corridor = "CORRIDOR";

    private:
        // The original's frames: 34 ms each.
        static constexpr float FramesPerSecond = 1000.f / 34.f;

    public:
        static constexpr float FadeIn = 0x80 * FramesPerSecond;
        static constexpr float FadeOut = 0x100 * FramesPerSecond;

        LevelMusic(SoundMixer& mixer, Mdk::Dictionary<std::string, std::string> tracks);

        // Arena to its ambient loop, played under the music (the 1996 demo's Cmi.ArenaAmbience).
        Mdk::Dictionary<std::string, std::string> Ambience;

        // The track fading in or playing (not a sound, as NONE: silence), and its volume.
        const std::optional<std::string>& Playing() const { return _current.Name; }
        float Volume() const { return _current.Volume; }
        // The track fading out, and its volume.
        const std::optional<std::string>& Fading() const { return _fading.Name; }
        float FadingVolume() const { return _fading.Volume; }

        // The track of an arena: its own, or CORRIDOR without one.
        std::string TrackOf(const std::string& arena) const;

        // Kurt is in arena (null outside every arena: no change).
        void Enter(const std::optional<std::string>& arena);

        // One frame of the fades: the current track up, the fading one down and stopped below 0.
        void Update(float delta);

    private:
        // A track and its volume (0-0x7FFF); no name is silence.
        struct Track
        {
            std::optional<std::string> Name;
            int Voice = 0;
            float Volume = 0;
        };

        SoundMixer& _mixer;
        Mdk::Dictionary<std::string, std::string> _tracks;

        Track _current;
        Track _fading;
        std::string _arena;
        std::string _ambience;
        int _ambienceVoice = 0;

        // The arena's ambient loop replaces the last one (none: silence).
        void PlayAmbience(const std::string& name);

        void Switch(const std::string& name);

        void Apply(Track& track);
    };
}
