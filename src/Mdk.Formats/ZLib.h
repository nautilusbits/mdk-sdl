#pragma once

// System.IO.Compression.ZLibStream over miniz (zlib header, deflate, Adler-32).

#include <span>
#include <vector>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Formats
{
    struct ZLib
    {
        // CompressionLevel: Optimal (zlib 6), Fastest (zlib 1).
        enum class Level { Optimal, Fastest };

        static std::vector<byte> Compress(std::span<const byte> data, Level level);

        // ZLibStream.ReadExactly of `size` bytes: EndOfStreamException (IOException) when the
        // stream ends first, InvalidDataException when it's broken.
        static std::vector<byte> DecompressExactly(std::span<const byte> data, int size);
    };
}
