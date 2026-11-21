#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "formats/Png.h"
#include "formats/ZLib.h"

using namespace Mdk;
using namespace Mdk::Formats;

// PNG: what Png::Encode writes decodes back, and the upscaler's files
// (RGB, every row filter) decode.

namespace
{
    int Paeth(int a, int b, int c)
    {
        auto p = a + b - c;
        auto pa = Math::Abs(p - a);
        auto pb = Math::Abs(p - b);
        auto pc = Math::Abs(p - c);
        return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
    }

    // Row y filtered with filter y % 5 (RGB, 3 bytes a pixel).
    std::vector<byte> Filter(const std::vector<byte>& raw, int stride, int bpp, int height)
    {
        std::vector<byte> output;
        for (auto y = 0; y < height; y++)
        {
            auto filter = y % 5;
            output.push_back(static_cast<byte>(filter));
            for (auto x = 0; x < stride; x++)
            {
                int a = x >= bpp ? raw[y * stride + x - bpp] : 0;
                int b = y > 0 ? raw[(y - 1) * stride + x] : 0;
                int c = x >= bpp && y > 0 ? raw[(y - 1) * stride + x - bpp] : 0;
                int predicted;
                switch (filter)
                {
                    case 1: predicted = a; break;
                    case 2: predicted = b; break;
                    case 3: predicted = (a + b) / 2; break;
                    case 4: predicted = Paeth(a, b, c); break;
                    default: predicted = 0; break;
                }

                output.push_back(static_cast<byte>(raw[y * stride + x] - predicted));
            }
        }

        return output;
    }

    void WriteInt32BigEndian(std::vector<byte>& bytes, int at, int value)
    {
        auto v = static_cast<uint>(value);
        bytes[at] = static_cast<byte>(v >> 24);
        bytes[at + 1] = static_cast<byte>(v >> 16);
        bytes[at + 2] = static_cast<byte>(v >> 8);
        bytes[at + 3] = static_cast<byte>(v);
    }

    void Chunk(std::vector<byte>& stream, const std::string& type, const std::vector<byte>& data)
    {
        std::vector<byte> length(4);
        WriteInt32BigEndian(length, 0, static_cast<int>(data.size()));
        stream.insert(stream.end(), length.begin(), length.end());
        stream.insert(stream.end(), type.begin(), type.end());
        stream.insert(stream.end(), data.begin(), data.end());
        stream.insert(stream.end(), 4, 0);
    }

    // A PNG of 8-bit RGB with these filtered rows (CRCs left zero: the decoder ignores them).
    std::vector<byte> Build(int width, int height, const std::vector<byte>& filtered)
    {
        std::vector<byte> stream = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        std::vector<byte> header(13);
        WriteInt32BigEndian(header, 0, width);
        WriteInt32BigEndian(header, 4, height);
        header[8] = 8;
        header[9] = 2;
        Chunk(stream, "IHDR", header);
        Chunk(stream, "IDAT", ZLib::Compress(filtered, ZLib::Level::Fastest));
        Chunk(stream, "IEND", {});
        return stream;
    }
}

TEST_CASE("PngTests.RoundTrips")
{
    constexpr int width = 5;
    constexpr int height = 3;
    std::vector<byte> rgba(width * height * 4);
    for (size_t i = 0; i < rgba.size(); i++)
    {
        rgba[i] = static_cast<byte>(i * 7);
    }

    auto image = Png::Decode(Png::Encode(width, height, rgba, Png::Channels::Rgba));

    CHECK(width == image.Width);
    CHECK(height == image.Height);
    CHECK(rgba == image.Rgba);
}

TEST_CASE("PngTests.RgbIsOpaque")
{
    std::vector<byte> rgba = {10, 20, 30, 0, 40, 50, 60, 128};

    auto image = Png::Decode(Png::Encode(2, 1, rgba, Png::Channels::Rgb));

    CHECK(std::vector<byte>{10, 20, 30, 255, 40, 50, 60, 255} == image.Rgba);
}

// Rows filtered None, Sub, Up, Average and Paeth (as other encoders write them).
TEST_CASE("PngTests.DecodesEveryFilter")
{
    constexpr int width = 3;
    constexpr int height = 5;
    std::vector<byte> rgb(width * height * 3);
    for (size_t i = 0; i < rgb.size(); i++)
    {
        rgb[i] = static_cast<byte>(i * 13 + 5);
    }

    auto filtered = Filter(rgb, width * 3, 3, height);

    auto image = Png::Decode(Build(width, height, filtered));

    for (auto i = 0; i < width * height; i++)
    {
        CHECK(std::vector<byte>(rgb.begin() + i * 3, rgb.begin() + i * 3 + 3) == std::vector<byte>(image.Rgba.begin() + i * 4, image.Rgba.begin() + i * 4 + 3));
        CHECK(255 == image.Rgba[i * 4 + 3]);
    }
}

TEST_CASE("PngTests.RejectsOtherFiles")
{
    CHECK_THROWS_AS(Png::Decode(std::vector<byte>{1, 2, 3, 4, 5, 6, 7, 8, 9}), InvalidDataException);
}

// Crafted files (a mod from the internet) are refused, never a crash: sizes whose
// buffers overflow or exhaust memory, a chunk's length near the int's end.
TEST_CASE("PngTests.RefusesHugeSizes")
{
    std::pair<int, int> sizes[] = {{0x40000000, 1}, {0x7FFFFFFF, 0x7FFFFFFF}, {40000, 40000}};
    for (auto [width, height] : sizes)
    {
        CAPTURE(width);
        CHECK_THROWS_AS(Png::Decode(Build(width, height, {0, 1, 2, 3})), InvalidDataException);
    }
}

TEST_CASE("PngTests.RefusesAChunkPastTheEnd")
{
    auto bytes = Build(1, 1, {0, 1, 2, 3});
    WriteInt32BigEndian(bytes, 8, 0x7FFFFFFF - 4);

    CHECK_THROWS_AS(Png::Decode(bytes), InvalidDataException);
}
