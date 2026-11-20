#include "Mdk.Game/HdTextures/HdKey.h"

#include <array>
#include <cstdint>
#include <vector>

#include "Mdk.Core/Sha256.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/Texture.h"

namespace Mdk::Game::HdTextures
{
    namespace
    {
        // BinaryPrimitives.WriteInt32LittleEndian.
        void WriteInt32LittleEndian(std::span<byte> target, int value)
        {
            const auto bits = static_cast<std::uint32_t>(value);
            for (size_t i = 0; i < sizeof(int); i++)
            {
                target[i] = static_cast<byte>(bits >> (8 * i));
            }
        }
    }

    std::string HdKey::Of(const Formats::Texture& texture, const Formats::Palette& palette)
    {
        std::array<bool, Formats::Palette::Size> used{};
        for (const auto index : texture.Indices)
        {
            used[index] = true;
        }

        // Index 0 is always clear: its colour shows nowhere.
        used[0] = false;
        std::vector<byte> colours;
        for (int i = 0; i < Formats::Palette::Size; i++)
        {
            if (used[i])
            {
                colours.push_back(static_cast<byte>(i));
                colours.insert(colours.end(), palette.Rgba.begin() + i * Channels, palette.Rgba.begin() + i * Channels + Rgb);
            }
        }

        std::array<byte, HeaderInts * sizeof(int)> header{};
        WriteInt32LittleEndian(header, texture.Width);
        WriteInt32LittleEndian(std::span(header).subspan(sizeof(int)), texture.Height);
        WriteInt32LittleEndian(std::span(header).subspan(2 * sizeof(int)), texture.FrameCount);

        Sha256 hash;
        hash.AppendData(header);
        hash.AppendData(texture.Indices);
        hash.AppendData(colours);
        const auto digest = hash.GetHashAndReset();
        return Convert::ToHexStringLower(std::span<const byte>(digest).subspan(0, KeyBytes));
    }
}
