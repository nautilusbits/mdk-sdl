#pragma once

#include "core/DotNet.h"

#include <span>
#include <vector>

namespace Mdk::Engine::Render
{
    // The enhanced look's textures on the CPU: palette indices expanded to RGBA8, and their
    // mipmaps. Colours are premultiplied by their cover (alpha), index 0 being clear (0, 0, 0, 0), so
    // filtering weighs texels by cover and clear ones add no dark fringe. Each level halves the last
    // (a box filter); an odd size's last texel joins its neighbours' (3 texels).
    //   level 0   r g r g      level 1   (r+g+r+g)/4 ...
    //             r g r g
    //   chain: [level 0][level 1]...[1 x 1], written into a buffer the caller keeps
    class ColourMips
    {
    public:
        // Indices through an RGBA8 palette; index 0 (or one past the palette) is clear.
        static std::vector<byte> Expand(std::span<const byte> indices, std::span<const byte> palette);

        // Expand(indices, palette) into rgba.
        static void Expand(std::span<const byte> indices, std::span<const byte> palette, std::span<byte> rgba);

        // Levels from width x height down to 1 x 1.
        static int Levels(int width, int height);

        // A size's next level.
        static int Half(int size) { return Math::Max(size / 2, 1); }

        // Bytes of a level.
        static int LevelSize(int width, int height) { return width * height * Channels; }

        // Bytes of all levels of a width x height image.
        static int ChainSize(int width, int height);

        // All levels of an RGBA8 image, the image first.
        static std::vector<std::vector<byte>> Chain(std::span<const byte> rgba, int width, int height);

        // Fills the levels after the first (already in chain's start).
        static void FillChain(std::span<byte> chain, int width, int height);

    private:
        static constexpr int Channels = 4;
        static constexpr int Alpha = 3;
        static constexpr int Opaque = 255;

        // C#'s (int First, int Last) tuple.
        struct Cover
        {
            int First = 0;
            int Last = 0;
        };

        // The next level: each texel the mean of the 2 x 2 (up to 3 x 3 at odd edges) it covers.
        static void Downsample(std::span<const byte> rgba, int width, int height, std::span<byte> result);

        // The source texels [first, last) a texel of the next level covers: two, the whole
        // size when it's 1, and the odd one left over joins the last.
        static Cover Span(int index, int size, int half);
    };
}
