#pragma once

#include <array>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "core/DotNet.h"

namespace Mdk::Game::HdTextures
{
    // An HD image's cover: hard (0 or 255: surfaces' cut-outs), or soft (the source's, smooth:
    // sprites, whose outline the shader cuts at half cover as smoothly as the original's).
    enum class HdAlpha { Hard, Soft };

    // A texture frame before and after the upscaler (RGBA8, straight alpha). The upscaler
    // sees colour only: clear texels take their neighbours' colour first (no dark fringe where a
    // cut-out ends), and the frame wraps around by a margin, so a tiling texture's edges are upscaled
    // with what they meet. After: the margin cut, the size brought to the cache's scale (a box filter),
    // and the alpha the source's, upscaled and kept hard (0 or 255: cut-outs stay cut-outs), or soft
    // for sprites (HdAlpha).
    //   ┌───────────┐            ┌─────────────┐
    //   │ m  wrap   │  upscaler  │             │   crop m × factor ─► box ÷ (factor / scale)
    //   │  ┌─────┐  │  ───────►  │   × factor  │   alpha: source's, bilinear, ≥ ½ ─► 255
    //   │  │frame│  │            │             │
    //   │  └─────┘  │            │             │
    //   └───────────┘            └─────────────┘
    struct UpscaleImages
    {
        // The size of a frame with its margin all around.
        static std::tuple<int, int> Padded(int width, int height, int margin)
        {
            return {width + margin * 2, height + margin * 2};
        }

        // The upscaler's input: clear texels are bled; the margin wraps horizontally and wraps or clamps vertically.
        // wrapY: whether to wrap vertically; false clamps to the nearest row.
        static std::vector<byte> Input(std::span<const byte> rgba, int width, int height, int margin, bool wrapY = true);

        // The cache's frame (`scale` × the source's size) from the upscaler's output of Input
        // (`factor` × its size).
        static std::vector<byte> Output(std::span<const byte> upscaled, int factor, std::span<const byte> source, int width,
            int height, int margin, int scale, HdAlpha alpha = HdAlpha::Hard);

    private:
        static constexpr int Channels = 4;
        static constexpr int Alpha = 3;
        static constexpr byte Opaque = 255;
        static constexpr byte HalfCover = 128;
        static constexpr std::array<std::pair<int, int>, 4> Neighbours{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};

        static int Wrap(int value, int size) { return ((value % size) + size) % size; }

        // Clear texels take the mean colour of their opaque (or already filled) neighbours, ring
        // after ring, until none is left (an all clear frame stays black). All end opaque.
        static std::vector<byte> Bleed(std::span<const byte> rgba, int width, int height);

        // Writes the mean colour of a texel's filled neighbours into it; false when it has none.
        static bool Mean(std::span<byte> rgba, std::vector<bool>& filled, int x, int y, int width, int height);

        static bool AllOpaque(std::span<const byte> rgba);

        // The mean colour of a `size`² block.
        static void Box(std::span<const byte> rgba, int width, int left, int top, int size, std::span<byte> colour);

        // The source's alpha at a point (texel centres at whole numbers, edges clamped),
        // bilinear, then hard (opaque from half cover on) or as it is.
        static byte Cover(std::span<const byte> rgba, int width, int height, float x, float y, HdAlpha alpha);
    };
}
