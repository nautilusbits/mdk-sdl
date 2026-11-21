#include "formats/SniperScreen.h"

namespace Mdk::Formats
{
    std::shared_ptr<Texture> SniperScreen::Frame(const Bni& sprites)
    {
        auto offset = sprites.Entries().At(FrameName).Offset;
        auto texture = std::make_shared<Texture>();
        texture->Name = FrameName;
        texture->Width = ScreenWidth;
        texture->Height = ScreenHeight;
        texture->Indices = Bin::Slice(*sprites.Bytes(), offset, ScreenWidth * ScreenHeight);
        return texture;
    }

    std::shared_ptr<Texture> SniperScreen::Mask(const Bni& sprites)
    {
        auto texture = std::make_shared<Texture>();
        texture->Name = MaskName;
        texture->Width = ViewWidth;
        texture->Height = ViewHeight;
        texture->Indices = DecodeMask(*sprites.Bytes(), sprites.Entries().At(MaskName).Offset + SizeField);
        return texture;
    }

    std::vector<byte> SniperScreen::DecodeMask(std::span<const byte> bytes, int offset)
    {
        std::vector<byte> indices(ViewWidth * ViewHeight);
        auto length = static_cast<int>(indices.size());
        auto pixel = 0;
        while (pixel < length && offset + WordSize <= static_cast<int>(bytes.size()))
        {
            auto word = Bin::U16(bytes, offset);
            offset += WordSize;
            if (word == End)
            {
                break;
            }

            int count;
            if ((word & ShortLiteral) == ShortLiteral)
            {
                count = word & ShortCountMask;
            }
            else if ((word & Skip) != 0)
            {
                pixel += word & SkipMask;
                continue;
            }
            else
            {
                count = word * LiteralUnit;
            }

            for (auto i = 0; i < count && pixel < length; i++, pixel++)
            {
                indices[pixel] = bytes[offset + i];
            }

            offset += count;
        }

        return indices;
    }
}
