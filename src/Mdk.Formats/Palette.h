#pragma once

#include <array>
#include <memory>
#include <span>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Formats
{
    // A 256-colour palette for MDK's 8-bit textures.
    class Palette
    {
    public:
        static constexpr int Size = 256;
        // First index replaced by an arena's colours (WithArenaColors).
        static constexpr int ArenaFirstIndex = 64;

        // RGBA8, 4 bytes per index.
        std::array<byte, Size * 4> Rgba{};

        static std::shared_ptr<Palette> FromRgb(std::span<const byte> rgb);

        // A copy with an arena's 112 colours at indices 64–175.
        std::shared_ptr<Palette> WithArenaColors(std::span<const byte> arenaRgb) const;

        // A copy with the system colours (SYS_PAL) at indices 1–63; 0 stays black.
        std::shared_ptr<Palette> WithSystemColors(std::span<const byte> systemRgb) const;
    };
}
