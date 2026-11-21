#pragma once

#include <array>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "core/DotNet.h"

namespace Mdk::Formats
{
    // PNG images of 8-bit channels, not interlaced: written as RGB or RGBA (rows unfiltered),
    // read as grey, grey and alpha, RGB or RGBA (every row filter), always into RGBA8.
    //   signature, IHDR (width, height, depth 8, colour type), IDAT... (zlib: per row a filter byte, then
    //   the row), IEND; each chunk: u32 length, type, data, CRC-32 (big-endian)
    struct Png
    {
        // The channels written.
        enum class Channels { Rgb, Rgba };

        struct Image
        {
            int Width = 0;
            int Height = 0;
            std::vector<byte> Rgba;

            bool operator==(const Image&) const = default;
        };

        // An RGBA8 image as a PNG of `channels` (RGB drops the alpha).
        static std::vector<byte> Encode(int width, int height, std::span<const byte> rgba, Channels channels);

        // A PNG's pixels as RGBA8 (opaque without alpha).
        static Image Decode(std::span<const byte> bytes);

    private:
        static constexpr std::array<byte, 8> Signature = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
        static constexpr int HeaderSize = 13;
        static constexpr int Depth = 8;
        static constexpr byte Opaque = 255;
        static constexpr int Rgba = 4;
        // The widest and tallest read (256 MB as RGBA8; mods' HD images are far smaller):
        // larger sizes would overflow the buffers or exhaust memory.
        static constexpr int MaxSide = 8192;

        // Colour types (IHDR byte 9) and their bytes per pixel.
        static constexpr byte Grey = 0;
        static constexpr byte Rgb = 2;
        static constexpr byte GreyAlpha = 4;
        static constexpr byte RgbAlpha = 6;

        // Row filters: none, left, up, mean of left and up, Paeth.
        static constexpr byte FilterNone = 0;
        static constexpr byte FilterSub = 1;
        static constexpr byte FilterUp = 2;
        static constexpr byte FilterAverage = 3;
        static constexpr byte FilterPaeth = 4;

        static std::tuple<int, int, byte> ReadHeader(std::span<const byte> header);
        static int BytesPerPixel(byte type);

        // Undoes each row's filter in place (the filter byte stays before the row). The first
        // row's "up" is a row of zeros.
        static void Unfilter(std::vector<byte>& raw, int stride, int bpp, int height);
        static void UnfilterSub(std::span<byte> row, int bpp);
        static void UnfilterUp(std::span<byte> row, std::span<const byte> previous);
        static void UnfilterAverage(std::span<byte> row, std::span<const byte> previous, int bpp);
        static void UnfilterPaeth(std::span<byte> row, std::span<const byte> previous, int bpp);
        static int Paeth(int a, int b, int c);

        // Unfiltered rows to RGBA8: grey to all three, no alpha to opaque.
        static std::vector<byte> ToRgba(const std::vector<byte>& raw, int width, int height, int bpp);

        static void WriteChunk(std::vector<byte>& stream, const std::string& type, std::span<const byte> data);

        // CRC-32 (ISO 3309) of a chunk's type and data.
        static uint Crc(std::span<const byte> type, std::span<const byte> data);
        static uint Crc(uint crc, std::span<const byte> bytes);
        static std::array<uint, 256> MakeCrcTable();
    };
}
