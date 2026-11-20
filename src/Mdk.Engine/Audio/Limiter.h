#pragma once

#include <span>

namespace Mdk::Engine::Audio
{
    // A hard limiter on the master mix (settings.gd: Godot's AudioEffectHardLimiter on
    // Master) so that many sounds at once don't clip. Both channels share one gain: a peak over
    // the ceiling lowers it at once, then it recovers over the release time.
    //   peak 2.0 ─► gain 0.47 (out 0.94) ─► ~0.1 s later back toward 1
    class Limiter
    {
    public:
        explicit Limiter(int sampleRate);

        // The highest sample out (linear, -0.5 dB).
        static const float Ceiling;

        // Limits interleaved stereo samples in place.
        void Process(std::span<float> stereo);

    private:
        static constexpr float CeilingDb = -0.5f;
        // Godot's default release.
        static constexpr float ReleaseSeconds = 0.1f;

        // The gain's step back toward 1 each frame (one-pole).
        float _release;
        float _gain = 1.f;
    };
}
