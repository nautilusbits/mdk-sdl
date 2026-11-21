#include <doctest.h>

#include "engine/render/ColourMips.h"

#include <algorithm>
#include <vector>

using namespace Mdk;
using namespace Mdk::Engine::Render;

// The enhanced look's textures: indices through the palette into RGBA, premultiplied,
// and their mipmaps (box filtered), without the GPU.
namespace
{
    constexpr int Rgba = 4;
    constexpr byte Opaque = 255;
    const std::vector<byte> Red{200, 0, 0, Opaque};
    const std::vector<byte> Blue{0, 0, 100, Opaque};
    const std::vector<byte> Clear{0, 0, 0, 0};

    // A palette: index 1 red, 2 blue, the rest grey.
    std::vector<byte> Palette()
    {
        std::vector<byte> palette(256 * Rgba, 128);
        std::copy(Red.begin(), Red.end(), palette.begin() + 1 * Rgba);
        std::copy(Blue.begin(), Blue.end(), palette.begin() + 2 * Rgba);
        return palette;
    }

    std::vector<byte> Texel(const std::vector<byte>& rgba, int index)
    {
        return {rgba.begin() + index * Rgba, rgba.begin() + (index + 1) * Rgba};
    }

    std::vector<byte> Expand(std::vector<byte> indices)
    {
        return ColourMips::Expand(indices, Palette());
    }
}

TEST_CASE("ColourMipsTests.IndicesTakeThePalettesColours")
{
    const auto rgba = Expand({1, 2, 0});

    CHECK(Texel(rgba, 0) == Red);
    CHECK(Texel(rgba, 1) == Blue);
}

TEST_CASE("ColourMipsTests.IndexZeroIsClear")
{
    // Premultiplied: no colour where nothing covers, so filtering never mixes in black.
    const auto rgba = Expand({0});

    CHECK(Texel(rgba, 0) == Clear);
}

TEST_CASE("ColourMipsTests.MipsHalveDownToOneTexel")
{
    CHECK(ColourMips::Levels(1, 1) == 1);
    CHECK(ColourMips::Levels(64, 64) == 7);
    CHECK(ColourMips::Levels(128, 16) == 8);
    CHECK(ColourMips::Levels(5, 3) == 3);

    const auto chain = ColourMips::Chain(std::vector<byte>(128 * 16 * Rgba), 128, 16);
    CHECK(chain.size() == 8);
    CHECK(chain[1].size() == 64 * 8 * Rgba);
    CHECK(chain[7].size() == 1 * 1 * Rgba);
}

TEST_CASE("ColourMipsTests.MipsAverageTwoByTwo")
{
    // Red and blue columns: the next level is their mean.
    const auto rgba = Expand({1, 2, 1, 2});

    const auto half = ColourMips::Chain(rgba, 2, 2)[1];

    CHECK(half == std::vector<byte>{100, 0, 50, Opaque});
}

TEST_CASE("ColourMipsTests.ClearEdgesDoNotDarken")
{
    // Half red, half clear: half covered, and red again once divided by its cover.
    const auto rgba = Expand({1, 0, 1, 0});

    const auto half = ColourMips::Chain(rgba, 2, 2)[1];

    CHECK(half[3] == 128);
    const auto red = half[0] * static_cast<double>(Opaque) / half[3];
    CHECK(red >= Red[0] - 1.0);
    CHECK(red <= Red[0] + 1.0);
    CHECK(half[1] == 0);
    CHECK(half[2] == 0);
}

TEST_CASE("ColourMipsTests.OddSizesKeepTheirLastTexels")
{
    // 3 x 1: red, red, blue; the second level's one texel covers all three.
    const auto rgba = Expand({1, 1, 2});

    const auto next = ColourMips::Chain(rgba, 3, 1)[1];

    CHECK(next.size() == Rgba);
    CHECK(next[0] == 133);
    CHECK(next[2] == 33);
}
