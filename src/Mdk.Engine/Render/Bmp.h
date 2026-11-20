#pragma once

#include "Mdk.Core/DotNet.h"

#include <span>
#include <string>

namespace Mdk::Engine::Render
{
    // Writes RGBA8 pixels as an uncompressed 32-bit BMP (top-down rows).
    class Bmp
    {
    public:
        static void Write(const std::string& path, int width, int height, std::span<const byte> rgba);

    private:
        static constexpr int FileHeaderSize = 14;
        static constexpr int InfoHeaderSize = 40;
        static constexpr int BitsPerPixel = 32;
    };
}
