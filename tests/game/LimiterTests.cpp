#include <algorithm>
#include <vector>

#include <doctest.h>

#include "Asserts.h"
#include "core/DotNet.h"
#include "engine/audio/Limiter.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;

// The master limiter (settings.gd: AudioEffectHardLimiter on Master, ceiling -0.5 dB).

namespace
{
    constexpr int Rate = 44100;
    constexpr int Channels = 2;
    constexpr float Hertz = 440.f;
    constexpr float Tolerance = 1e-6f;

    // A stereo sine of amplitude, seconds long.
    std::vector<float> Sine(float amplitude, float seconds)
    {
        auto frames = ToInt(Rate * seconds);
        std::vector<float> samples(static_cast<size_t>(frames * Channels));
        for (int f = 0; f < frames; f++)
        {
            auto s = amplitude * MathF::Sin(2.f * MathF::PI * Hertz * static_cast<float>(f) / Rate);
            samples[f * Channels] = s;
            samples[f * Channels + 1] = s;
        }

        return samples;
    }

    float Max(const std::vector<float>& samples) { return *std::max_element(samples.begin(), samples.end()); }
}

TEST_CASE("LimiterTests.LoudSoundStaysUnderTheCeiling")
{
    Limiter limiter(Rate);
    auto samples = Sine(4.f, 0.5f);
    limiter.Process(samples);
    for (auto s : samples)
    {
        REQUIRE(MathF::Abs(s) <= Limiter::Ceiling + Tolerance);
    }

    CHECK(Max(samples) > Limiter::Ceiling * 0.99f);
}

TEST_CASE("LimiterTests.QuietSoundPassesUnchanged")
{
    Limiter limiter(Rate);
    auto samples = Sine(0.5f, 0.5f);
    auto original = samples;
    limiter.Process(samples);
    CHECK(original == samples);
}

TEST_CASE("LimiterTests.GainRecoversAfterAPeak")
{
    Limiter limiter(Rate);
    auto peak = Sine(4.f, 0.1f);
    limiter.Process(peak);

    // A second later the quiet sound is back to (almost) its own level.
    auto quiet = Sine(0.5f, 1.f);
    limiter.Process(quiet);
    auto after = Sine(0.5f, 0.1f);
    limiter.Process(after);
    CHECK_PRECISION(0.5f, Max(after), 2);
}
