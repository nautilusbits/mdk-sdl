#pragma once

#include <string>

namespace Mdk::Formats
{
    class Palette;
    class Texture;
}

namespace Mdk::Game::HdTextures
{
    // The name of an HD image: a hash of what the texture shows through a palette (its size,
    // frames, indices, and the colours of the indices it uses). Arenas whose palettes differ only in
    // colours a texture doesn't use share its image; a changed texture or colour makes another key, so
    // a cache made from other game files is never used (stale).
    //   SHA-256(width, height, frames, indices, RGB of each used index 1-255) ─► first 8 bytes, hex
    struct HdKey
    {
        static std::string Of(const Formats::Texture& texture, const Formats::Palette& palette);

    private:
        static constexpr int KeyBytes = 8;
        static constexpr int HeaderInts = 3;
        static constexpr int Channels = 4;
        static constexpr int Rgb = 3;
    };
}
