#pragma once

#include <memory>
#include <span>
#include <vector>

#include "formats/Bni.h"
#include "formats/Texture.h"

namespace Mdk::Formats
{
    // Sniper mode's screen in TRAVSPRT.BNI (godot-mdk docs/gameplay.md "Sniper mode"):
    // SNIPERS1, the 640x480 frame (palette indices, no header), and SNIPERS2, the mask
    // over the 600x360 view with holes for the scope and the round cameras.
    //   SNIPERS2: u32 size, then u16 words over 600-pixel rows:
    //     below 0x8000  that many x 4 literal indices follow
    //     0x8nnn        nnn transparent pixels
    //     0xFFnn        nn literal indices (1-3)
    //     0xFF00        end
    // Palette index 0 is transparent.
    struct SniperScreen
    {
        static constexpr int ScreenWidth = 640;
        static constexpr int ScreenHeight = 480;
        static constexpr int ViewWidth = 600;
        static constexpr int ViewHeight = 360;

        // The 640x480 frame's palette indices.
        static std::shared_ptr<Texture> Frame(const Bni& sprites);

        // The 600x360 mask's palette indices (0 in its holes).
        static std::shared_ptr<Texture> Mask(const Bni& sprites);

        // Decodes the mask's words from `offset`.
        static std::vector<byte> DecodeMask(std::span<const byte> bytes, int offset);

    private:
        static constexpr const char* FrameName = "SNIPERS1";
        static constexpr const char* MaskName = "SNIPERS2";
        static constexpr int SizeField = 4;
        static constexpr int WordSize = 2;
        static constexpr ushort End = 0xFF00;
        static constexpr ushort ShortLiteral = 0xFF00;
        static constexpr ushort Skip = 0x8000;
        static constexpr int SkipMask = 0xFFF;
        static constexpr int ShortCountMask = 0xFF;
        static constexpr int LiteralUnit = 4;
    };
}
