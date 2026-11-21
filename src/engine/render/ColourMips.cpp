#include "engine/render/ColourMips.h"

#include <algorithm>
#include <array>

namespace Mdk::Engine::Render
{
    std::vector<byte> ColourMips::Expand(std::span<const byte> indices, std::span<const byte> palette)
    {
        std::vector<byte> rgba(indices.size() * Channels);
        Expand(indices, palette, rgba);
        return rgba;
    }

    void ColourMips::Expand(std::span<const byte> indices, std::span<const byte> palette, std::span<byte> rgba)
    {
        for (size_t i = 0; i < indices.size(); i++)
        {
            const auto colour = indices[i] * Channels;
            auto texel = rgba.subspan(i * Channels, Channels);
            if (indices[i] == 0 || colour + Channels > static_cast<int>(palette.size()))
            {
                std::fill(texel.begin(), texel.end(), byte{0});
                continue;
            }

            std::copy_n(palette.begin() + colour, Alpha, texel.begin());
            texel[Alpha] = Opaque;
        }
    }

    int ColourMips::Levels(int width, int height)
    {
        return ToInt(Math::Log2(static_cast<double>(Math::Max(Math::Max(width, height), 1)))) + 1;
    }

    int ColourMips::ChainSize(int width, int height)
    {
        auto size = 0;
        const auto count = Levels(width, height);
        for (int level = 0; level < count; level++)
        {
            size += LevelSize(width, height);
            width = Half(width);
            height = Half(height);
        }

        return size;
    }

    std::vector<std::vector<byte>> ColourMips::Chain(std::span<const byte> rgba, int width, int height)
    {
        std::vector<byte> chain(ChainSize(width, height));
        std::copy(rgba.begin(), rgba.end(), chain.begin());
        FillChain(chain, width, height);

        std::vector<std::vector<byte>> levels;
        auto offset = 0;
        for (int level = 0; level < Levels(width, height); level++)
        {
            const auto w = width >> level;
            const auto h = height >> level;
            const auto size = LevelSize(Math::Max(w, 1), Math::Max(h, 1));
            levels.emplace_back(chain.begin() + offset, chain.begin() + offset + size);
            offset += size;
        }

        return levels;
    }

    void ColourMips::FillChain(std::span<byte> chain, int width, int height)
    {
        auto offset = 0;
        const auto count = Levels(width, height);
        for (int level = 1; level < count; level++)
        {
            const auto size = LevelSize(width, height);
            const auto next = LevelSize(Half(width), Half(height));
            Downsample(chain.subspan(offset, size), width, height, chain.subspan(offset + size, next));
            offset += size;
            width = Half(width);
            height = Half(height);
        }
    }

    void ColourMips::Downsample(std::span<const byte> rgba, int width, int height, std::span<byte> result)
    {
        const auto halfWidth = Half(width);
        const auto halfHeight = Half(height);
        std::array<int, Channels> sum{};
        for (int y = 0; y < halfHeight; y++)
        {
            const auto [top, bottom] = Span(y, height, halfHeight);
            for (int x = 0; x < halfWidth; x++)
            {
                const auto [left, right] = Span(x, width, halfWidth);
                sum.fill(0);
                for (int sy = top; sy < bottom; sy++)
                {
                    for (int sx = left; sx < right; sx++)
                    {
                        for (int c = 0; c < Channels; c++)
                        {
                            sum[c] += rgba[(sy * width + sx) * Channels + c];
                        }
                    }
                }

                // Rounded to the nearest.
                const auto count = (bottom - top) * (right - left);
                for (int c = 0; c < Channels; c++)
                {
                    result[(y * halfWidth + x) * Channels + c] = static_cast<byte>((sum[c] + count / 2) / count);
                }
            }
        }
    }

    ColourMips::Cover ColourMips::Span(int index, int size, int half)
    {
        const auto first = Math::Min(index * 2, size - 1);
        const auto last = index == half - 1 ? size : first + 2;
        return {first, last};
    }
}
