#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Formats
{
    // A font of MISC/MDKFONT.FTI (FONTBIG, FONTSML).
    //   u32 glyph offsets[256] (from the font, 0 = none), then per glyph:
    //     s8 ascent, s8 descent, u8 width, (ascent + descent + 1) x width palette indices
    // A glyph spans `ascent` rows above the baseline to `descent` below. Characters without a
    // glyph are spaces. Pixels use the palette's first 64 colours (SYS_PAL); 0 is transparent.
    class Font
    {
    public:
        struct Glyph
        {
            int Ascent = 0;
            int Descent = 0;
            int Width = 0;
            std::vector<byte> Indices;

            bool operator==(const Glyph&) const = default;
        };

        // Per character, empty without a glyph.
        std::vector<std::optional<Glyph>> Glyphs = std::vector<std::optional<Glyph>>(CharacterCount);
        int SpaceWidth() const { return _spaceWidth; }

        // Space widths: 6 in FONTBIG, 4 in FONTSML.
        static std::shared_ptr<Font> Parse(std::span<const byte> data, int spaceWidth);

        // Width of a text in pixels (0x4159d4).
        int Width(std::span<const byte> text) const;

    private:
        static constexpr int CharacterCount = 256;

        int _spaceWidth;

        explicit Font(int spaceWidth) : _spaceWidth(spaceWidth) {}
    };
}
