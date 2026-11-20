#pragma once

#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"

namespace Mdk::Game::Fall
{
    // The fall's palette effects on FALLPn (0x5209c8): the red of death raises the red
    // of colours 1-254, then the brightness b whitens every component (0x410cc0):
    //   c' = (c × k + (256 - k) × 255) >> 8      k = round(256 × clamp(b, 0, 1))
    //   b = 1: unchanged    b = 0.5: 0 → 127, 100 → 177    b = 0: white
    // Colour 0 is whitened only in the fall's first second.
    class FallPalette
    {
    public:
        enum class Zero { Whitened, Kept };

        // The palette with the effects, RGBA.
        std::vector<byte> Rgba;

        explicit FallPalette(std::vector<byte> rgba);

        // k of a brightness.
        static int Level(float brightness) { return ToInt(MathF::Round(Unit * Math::Clamp(brightness, 0.f, 1.f))); }

        static byte Whiten(byte c, int level) { return static_cast<byte>((c * level + (Unit - level) * 255) >> Shift); }

        // Applies a brightness and a red (0-1, added to R); false when nothing changed.
        bool Set(float brightness, float red, Zero zero);

        // Colour index with the effects (0-1).
        Vector4 Colour(int index) const
        {
            return Vector4(Rgba[index * Channels], Rgba[index * Channels + 1], Rgba[index * Channels + 2], 255.f) / 255.f;
        }

        // A colour outside the palette (0-1, alpha kept) with the effects of colours 1-254.
        Vector4 Apply(Vector4 colour) const;

    private:
        static constexpr int Unit = 256;
        static constexpr int Shift = 8;
        static constexpr int Channels = 4;
        static constexpr int LastReddened = 254;

        struct Applied
        {
            int Level = Unit;
            int Red = 0;
            FallPalette::Zero Zero = Zero::Whitened;

            bool operator==(const Applied&) const = default;
        };

        std::vector<byte> _base;
        Applied _applied;
    };
}
