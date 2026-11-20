#include "Mdk.Engine/Audio/Limiter.h"

#include "Mdk.Core/DotNet.h"

namespace Mdk::Engine::Audio
{
    const float Limiter::Ceiling = MathF::Pow(10.f, CeilingDb / 20.f);

    Limiter::Limiter(int sampleRate)
        : _release(1.f - MathF::Exp(-1.f / (ReleaseSeconds * static_cast<float>(sampleRate))))
    {
    }

    void Limiter::Process(std::span<float> stereo)
    {
        for (size_t f = 0; f + 1 < stereo.size(); f += 2)
        {
            auto peak = MathF::Max(MathF::Abs(stereo[f]), MathF::Abs(stereo[f + 1]));
            auto limit = peak > Ceiling ? Ceiling / peak : 1.f;
            _gain = MathF::Min(_gain + (1.f - _gain) * _release, limit);
            if (_gain >= 1.f)
            {
                continue;
            }

            stereo[f] *= _gain;
            stereo[f + 1] *= _gain;
        }
    }
}
