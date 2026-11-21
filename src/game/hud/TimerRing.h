#pragma once

#include <span>

#include "core/DotNet.h"

namespace Mdk::Formats
{
    class Texture;
}

namespace Mdk::Game::Hud
{
    // The mission timer's ring around the health (0x420cb0, 0x420d88): the time gone, as an
    // angle counter-clockwise from the right, stamps the empty panel's pixels (SC_BSTAT, its
    // black ring) over the panel's from the centre outwards. Each half of the panel is a fan of rows
    // from its centre row, the upper one for angles up to π, the lower mirrored.
    //        π/2
    //    ┌────┼────┐
    //  π ┤    ●    ├ 0   time gone 0 → 2π: empties ↺ from the right
    //    └────┼────┘
    //        3π/2
    struct TimerRing
    {
        // Stamps the ring of `empty` into `panel` (indices of the same size) from angle `from` to
        // `to` (0x420cb0).
        static void Wipe(std::span<byte> panel, const Formats::Texture& empty, float from, float to);

    private:
        // The split at π (0x420d5a: 0x40490fb1 just below it for the upper half).
        static constexpr float UpperEnd = 3.141582f;
        static constexpr int Fixed = 16;
        static constexpr float FixedOne = 1 << Fixed;
        // Slopes: near π/2 a row goes straight up; near 0 or π, a whole width per row.
        static constexpr float NearUpLow = 1.570482f;
        static constexpr float NearUpHigh = 1.571110f;
        static constexpr float NearZero = 3.14159e-5f;
        static constexpr float NearPi = 3.141561f;

        // An angle within 0-2π.
        static float Wrap(float angle);

        // The rows between angles `high` and `low` of one half (0x420d88): every other row from
        // the centre, x moving by the cotangent per row.
        static void Fan(std::span<byte> panel, const Formats::Texture& empty, float high, float low);

        // x moved per row, 16.16 (truncated like the original's FPU setting).
        static int Slope(float angle, int width);
    };
}
