#include "Mdk.Formats/Fall3d.h"

#include <algorithm>

namespace Mdk::Formats
{
    std::vector<byte> Fall3d::Haze(const Bni& bni, const std::string& name)
    {
        std::vector<byte> pixels(HazeWidth * HazeRows);
        BinReader r(*bni.Bytes(), bni.Entries().At(name).Offset + static_cast<int>(sizeof(uint)));
        for (auto row = 0; row < HazeRows; row++)
        {
            auto left = static_cast<int>(r.U32()) * HazeGroup;
            auto leftBytes = r.Buffer(left);
            std::copy(leftBytes.begin(), leftBytes.end(), pixels.begin() + row * HazeWidth);
            auto middle = static_cast<int>(r.U32()) * HazeGroup;
            auto right = static_cast<int>(r.U32()) * HazeGroup;
            auto rightBytes = r.Buffer(right);
            std::copy(rightBytes.begin(), rightBytes.end(), pixels.begin() + row * HazeWidth + left + middle);
        }

        return pixels;
    }

    std::vector<std::string> Fall3d::Pickups(const Bni& bni, const std::string& name)
    {
        std::vector<std::string> names;
        if (!bni.Has(name))
        {
            return names;
        }

        auto& entry = bni.Entries().At(name);
        auto& bytes = *bni.Bytes();
        for (auto offset = entry.Offset; offset + PickupNameLength <= entry.Offset + entry.Size && bytes[offset] != 0; offset += PickupNameLength)
        {
            names.push_back(Bin::Ascii(bytes, offset, PickupNameLength));
        }

        return names;
    }
}
