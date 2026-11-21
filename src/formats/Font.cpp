#include "formats/Font.h"

#include "formats/BinReader.h"

namespace Mdk::Formats
{
    std::shared_ptr<Font> Font::Parse(std::span<const byte> data, int spaceWidth)
    {
        auto font = std::shared_ptr<Font>(new Font(spaceWidth));
        for (auto c = 0; c < CharacterCount; c++)
        {
            auto offset = static_cast<int>(Bin::U32(data, c * 4));
            if (offset == 0)
            {
                continue;
            }

            int ascent = static_cast<sbyte>(data[offset]);
            int descent = static_cast<sbyte>(data[offset + 1]);
            int width = data[offset + 2];
            auto size = (ascent + descent + 1) * width;
            font->Glyphs[c] = Glyph{ascent, descent, width, Bin::Slice(data, offset + 3, size)};
        }

        return font;
    }

    int Font::Width(std::span<const byte> text) const
    {
        auto width = 0;
        for (auto c : text)
        {
            width += Glyphs[c] ? Glyphs[c]->Width : _spaceWidth;
        }

        return width;
    }
}
