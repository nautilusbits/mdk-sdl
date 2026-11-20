#include "Mdk.Formats/Wav.h"

#include "Mdk.Formats/BinReader.h"

namespace Mdk::Formats
{
    std::optional<Wav> Wav::Parse(std::span<const byte> wav)
    {
        std::optional<std::vector<byte>> format;
        std::optional<std::vector<byte>> data;
        auto length = static_cast<int>(wav.size());
        auto pos = RiffHeader;
        while (pos + ChunkHeader <= length)
        {
            auto id = Bin::Ascii(wav, pos, 4);
            // Truncated chunks can claim more than the file holds.
            auto size = static_cast<int>(Math::Min(Bin::U32(wav, pos + 4), static_cast<uint>(length - pos - ChunkHeader)));
            auto end = Math::Min(pos + ChunkHeader + size, length);
            auto body = Bin::Slice(wav, pos + ChunkHeader, end - pos - ChunkHeader);
            if (id == "fmt ")
            {
                format = std::move(body);
            }
            else if (id == "data")
            {
                data = std::move(body);
            }

            // Chunks are padded to an even size.
            pos += ChunkHeader + size + (size & 1);
        }

        if (!format || !data)
        {
            return std::nullopt;
        }

        // fmt: u16 format, u16 channels, u32 rate, u32 byte rate, u16 align, u16 bits.
        return Wav{Bin::U16(*format, 2), static_cast<int>(Bin::U32(*format, 4)), Bin::U16(*format, 14), std::move(*data)};
    }
}
