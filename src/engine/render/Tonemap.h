#pragma once

#include "core/DotNet.h"

namespace Mdk::Engine::Render
{
    // The enhanced look's tone curve, per linear channel (shaders/enhanced.hlsl
    // tonemap): light up to the knee is kept as is (the original's textures, unlit, stay
    // theirs; no black is crushed), brighter light rolls off exponentially towards white, its slope
    // unbroken at the knee: lit faces brighten without clipping, their hue fading to white.
    //   out 1 ┤          ___────── white
    //         │      _.-'
    //    knee ┤    ／   1 - (1 - k)·e^(-(x - k)/(1 - k))
    //         │  ／  identity
    //       0 ┼──┬───────────── in
    //            k
    struct Tonemap
    {
        static constexpr float Knee = 0.6f;

        static float Map(float light)
        {
            if (light <= Knee)
            {
                return light;
            }

            const auto room = 1.0f - Knee;
            return 1.0f - room * MathF::Exp(-(light - Knee) / room);
        }
    };
}
