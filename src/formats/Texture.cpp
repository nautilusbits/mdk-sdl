#include "formats/Texture.h"

#include <algorithm>

#include "formats/BinReader.h"

namespace Mdk::Formats
{
    std::shared_ptr<Texture> Texture::Parse(const std::string& name, std::span<const byte> bytes, int offset)
    {
        int width = Bin::U16(bytes, offset);
        int height = Bin::U16(bytes, offset + 2);
        auto texture = std::make_shared<Texture>();
        texture->Name = name;
        texture->Width = width;
        texture->Height = height;
        texture->Indices = Bin::Slice(bytes, offset + 4, width * height);
        return texture;
    }

    std::shared_ptr<Texture> Texture::ParseAnimated(const std::string& name, std::span<const byte> bytes, int offset, uint kind)
    {
        auto count = static_cast<int>(Bin::U32(bytes, offset));
        int width = Bin::U16(bytes, offset + 4);
        int height = Bin::U16(bytes, offset + 6);
        auto size = width * height;
        auto indices = (kind & Deltas) != 0
            ? ApplyDeltas(bytes, offset + 8, size, count)
            : Bin::Slice(bytes, offset + 8, size * count);
        auto texture = std::make_shared<Texture>();
        texture->Name = name;
        texture->Width = width;
        texture->Height = height;
        texture->FrameCount = count;
        texture->Indices = std::move(indices);
        return texture;
    }

    // After the first frame: f32, then u32 offsets (from after it) to 2n blocks,
    // n controls then n data. A control is u16 start, u16 runs, then per run u8 copy, u8 skip
    // in units of 4 pixels. Change k turns frame k into frame k + 1.
    std::vector<byte> Texture::ApplyDeltas(std::span<const byte> bytes, int p, int size, int count)
    {
        auto frame = Bin::Slice(bytes, p, size);
        std::vector<byte> frames(static_cast<size_t>(size) * count);
        std::copy(frame.begin(), frame.end(), frames.begin());
        auto table = p + size + 4;
        for (auto k = 0; k < count - 1; k++)
        {
            auto control = table + static_cast<int>(Bin::U32(bytes, table + k * 4));
            auto data = table + static_cast<int>(Bin::U32(bytes, table + (count + k) * 4));
            auto destination = Bin::U16(bytes, control) * DeltaUnit;
            int runs = Bin::U16(bytes, control + 2);
            for (auto run = 0; run < runs; run++)
            {
                auto copy = bytes[control + 4 + run * 2] * DeltaUnit;
                auto skip = bytes[control + 5 + run * 2] * DeltaUnit;
                for (auto i = 0; i < copy; i++)
                {
                    if (destination + i < size)
                    {
                        frame[destination + i] = bytes[data + i];
                    }
                }

                data += copy;
                destination += copy + skip;
            }

            std::copy(frame.begin(), frame.end(), frames.begin() + static_cast<std::ptrdiff_t>(k + 1) * size);
        }

        return frames;
    }
}
