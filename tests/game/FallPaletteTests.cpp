#include <vector>

#include <doctest.h>

#include "Asserts.h"
#include "core/DotNet.h"
#include "core/Numerics.h"
#include "game/fall/FallPalette.h"

using namespace Mdk;
using namespace Mdk::Game::Fall;

// The fall's palette effects (0x410cc0, gameplay.md "Palette effects"): towards white by
// the brightness, colour 0 kept after the first second.

namespace
{
    constexpr int Entries = 256;
    constexpr int Channels = 4;
    constexpr int ByteMax = 255;

    // Entry i is (i, i, i).
    FallPalette Grey()
    {
        std::vector<byte> rgba(Entries * Channels);
        for (int i = 0; i < Entries; i++)
        {
            rgba[i * 4] = static_cast<byte>(i);
            rgba[i * 4 + 1] = static_cast<byte>(i);
            rgba[i * 4 + 2] = static_cast<byte>(i);
            rgba[i * 4 + 3] = static_cast<byte>(ByteMax);
        }

        return FallPalette(rgba);
    }
}

TEST_CASE("FallPaletteTests.WhitensAsTheOriginal")
{
    struct Row
    {
        int C;
        float Brightness;
        int Expected;
    };
    const Row rows[] = {
        {0, 1.f, 0},
        {100, 1.f, 100},
        {0, 0.5f, 127},
        {100, 0.5f, 177},
        {100, 0.9f, 115},
        {100, 0.f, 255},
        {255, 0.3f, 255},
    };

    for (auto& row : rows)
    {
        // (c × k + (256 − k) × 255) >> 8, k = round(256 b): 0.9 → 230.
        CAPTURE(row.C);
        CAPTURE(row.Brightness);
        CHECK(FallPalette::Whiten(static_cast<byte>(row.C), FallPalette::Level(row.Brightness)) == row.Expected);
    }
}

TEST_CASE("FallPaletteTests.ColourZeroWhitensOnlyInTheFirstSecond")
{
    auto palette = Grey();
    CHECK(palette.Set(0.5f, 0.f, FallPalette::Zero::Whitened));
    CHECK(palette.Rgba[0] == 127);
    CHECK(palette.Rgba[100 * Channels] == 177);

    CHECK(palette.Set(0.5f, 0.f, FallPalette::Zero::Kept));
    CHECK(palette.Rgba[0] == 0);
    CHECK(palette.Rgba[100 * Channels + 2] == 177);
    CHECK_FALSE(palette.Set(0.5f, 0.f, FallPalette::Zero::Kept));
}

TEST_CASE("FallPaletteTests.RedRisesOnColoursOneTo254")
{
    auto palette = Grey();
    palette.Set(1.f, 10.f / ByteMax, FallPalette::Zero::Kept);
    CHECK(palette.Rgba[0] == 0);
    CHECK(palette.Rgba[1] == 0);
    CHECK(palette.Rgba[100 * Channels] == 110);
    CHECK(palette.Rgba[100 * Channels + 1] == 100);
    CHECK(palette.Rgba[250 * Channels] == 255);
    CHECK(palette.Rgba[255 * Channels] == 255);
}

// Flat colours, the radar and the trails take the effects of colours 1-254.
TEST_CASE("FallPaletteTests.OtherColoursTakeTheEffects")
{
    auto palette = Grey();
    palette.Set(0.5f, 10.f / ByteMax, FallPalette::Zero::Kept);
    CHECK(palette.Colour(100) == Vector4(182.f, 177.f, 177.f, ByteMax) / static_cast<float>(ByteMax));

    auto green = palette.Apply(Vector4(0.f, 1.f, 0.f, 0.25f));
    CHECK_NEAR(10.f / ByteMax * 0.5f + 0.5f, green.X, 1e-5f);
    CHECK(green.Y == 1.f);
    CHECK(green.Z == 0.5f);
    CHECK(green.W == 0.25f);
}
