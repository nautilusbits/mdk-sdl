#include "Mdk.Formats/Png.h"

#include <algorithm>

#include "Mdk.Formats/BinReader.h"
#include "Mdk.Formats/ZLib.h"

namespace Mdk::Formats
{
    namespace
    {
        int ReadInt32BigEndian(std::span<const byte> b, int at)
        {
            if (at < 0 || static_cast<size_t>(at) + 4 > b.size())
            {
                throw std::out_of_range("Specified argument was out of the range of valid values.");
            }

            return static_cast<int>((static_cast<uint>(b[at]) << 24) | (static_cast<uint>(b[at + 1]) << 16) | (static_cast<uint>(b[at + 2]) << 8) | b[at + 3]);
        }

        void WriteUInt32BigEndian(std::vector<byte>& b, uint value)
        {
            b.push_back(static_cast<byte>(value >> 24));
            b.push_back(static_cast<byte>(value >> 16));
            b.push_back(static_cast<byte>(value >> 8));
            b.push_back(static_cast<byte>(value));
        }
    }

    std::vector<byte> Png::Encode(int width, int height, std::span<const byte> rgba, Channels channels)
    {
        auto bpp = channels == Channels::Rgb ? 3 : Rgba;
        auto stride = width * bpp;
        std::vector<byte> raw(static_cast<size_t>(stride + 1) * height);
        for (auto y = 0; y < height; y++)
        {
            auto row = y * (stride + 1) + 1;
            for (auto x = 0; x < width; x++)
            {
                auto pixel = rgba.subspan((y * width + x) * Rgba, bpp);
                std::copy(pixel.begin(), pixel.end(), raw.begin() + row + x * bpp);
            }
        }

        auto compressed = ZLib::Compress(raw, ZLib::Level::Optimal);

        std::vector<byte> header;
        WriteUInt32BigEndian(header, static_cast<uint>(width));
        WriteUInt32BigEndian(header, static_cast<uint>(height));
        header.push_back(Depth);
        header.push_back(channels == Channels::Rgb ? Rgb : RgbAlpha);
        header.resize(HeaderSize);

        std::vector<byte> file(Signature.begin(), Signature.end());
        WriteChunk(file, "IHDR", header);
        WriteChunk(file, "IDAT", compressed);
        WriteChunk(file, "IEND", {});
        return file;
    }

    Png::Image Png::Decode(std::span<const byte> bytes)
    {
        if (bytes.size() < Signature.size() || !std::equal(Signature.begin(), Signature.end(), bytes.begin()))
        {
            throw InvalidDataException("Not a PNG");
        }

        auto width = 0;
        auto height = 0;
        byte type = 0;
        std::vector<byte> data;
        auto length = static_cast<int>(bytes.size());
        auto at = static_cast<int>(Signature.size());
        while (at + 8 <= length)
        {
            auto chunkLength = ReadInt32BigEndian(bytes, at);
            auto name = Bin::AsciiString(bytes.subspan(at + 4, 4));
            if (chunkLength < 0 || chunkLength > length - at - 12)
            {
                throw InvalidDataException("PNG chunk past the end");
            }

            auto chunk = bytes.subspan(at + 8, chunkLength);
            at += 12 + chunkLength;
            if (name == "IHDR")
            {
                std::tie(width, height, type) = ReadHeader(chunk);
            }
            else if (name == "IDAT")
            {
                data.insert(data.end(), chunk.begin(), chunk.end());
            }
            else if (name == "IEND")
            {
                break;
            }
        }

        if (width <= 0 || height <= 0)
        {
            throw InvalidDataException("PNG without a header");
        }

        if (width > MaxSide || height > MaxSide)
        {
            throw InvalidDataException(std::format("PNG of {}x{}, over {}x{}", width, height, MaxSide, MaxSide));
        }

        auto bpp = BytesPerPixel(type);
        auto raw = ZLib::DecompressExactly(data, (width * bpp + 1) * height);
        Unfilter(raw, width * bpp, bpp, height);
        return Image{width, height, ToRgba(raw, width, height, bpp)};
    }

    std::tuple<int, int, byte> Png::ReadHeader(std::span<const byte> header)
    {
        constexpr int interlace = 12;
        if (header.size() < HeaderSize || header[8] != Depth || header[interlace] != 0)
        {
            throw InvalidDataException("PNG not of 8-bit channels, or interlaced");
        }

        return {ReadInt32BigEndian(header, 0), ReadInt32BigEndian(header, 4), header[9]};
    }

    int Png::BytesPerPixel(byte type)
    {
        switch (type)
        {
            case Grey: return 1;
            case GreyAlpha: return 2;
            case Rgb: return 3;
            case RgbAlpha: return Rgba;
            default: throw InvalidDataException(std::format("PNG colour type {} not read", type));
        }
    }

    void Png::Unfilter(std::vector<byte>& raw, int stride, int bpp, int height)
    {
        std::vector<byte> zeros(stride);
        std::span<const byte> previous = zeros;
        for (auto y = 0; y < height; y++)
        {
            auto start = y * (stride + 1) + 1;
            auto row = std::span<byte>(raw).subspan(start, stride);
            auto filter = raw[start - 1];
            switch (filter)
            {
                case FilterNone:
                    break;
                case FilterSub:
                    UnfilterSub(row, bpp);
                    break;
                case FilterUp:
                    UnfilterUp(row, previous);
                    break;
                case FilterAverage:
                    UnfilterAverage(row, previous, bpp);
                    break;
                case FilterPaeth:
                    UnfilterPaeth(row, previous, bpp);
                    break;
                default:
                    throw InvalidDataException(std::format("PNG row filter {}", filter));
            }

            previous = row;
        }
    }

    void Png::UnfilterSub(std::span<byte> row, int bpp)
    {
        for (size_t x = bpp; x < row.size(); x++)
        {
            row[x] += row[x - bpp];
        }
    }

    void Png::UnfilterUp(std::span<byte> row, std::span<const byte> previous)
    {
        // Bytes wrap; the compiler vectorizes this loop as C#'s Vector128 did by hand.
        for (size_t x = 0; x < row.size(); x++)
        {
            row[x] += previous[x];
        }
    }

    void Png::UnfilterAverage(std::span<byte> row, std::span<const byte> previous, int bpp)
    {
        for (auto x = 0; x < bpp; x++)
        {
            row[x] += static_cast<byte>(previous[x] / 2);
        }

        for (size_t x = bpp; x < row.size(); x++)
        {
            row[x] += static_cast<byte>((row[x - bpp] + previous[x]) / 2);
        }
    }

    void Png::UnfilterPaeth(std::span<byte> row, std::span<const byte> previous, int bpp)
    {
        // Left of the first pixel is zero: Paeth(0, b, 0) is b.
        for (auto x = 0; x < bpp; x++)
        {
            row[x] += previous[x];
        }

        for (size_t x = bpp; x < row.size(); x++)
        {
            row[x] += static_cast<byte>(Paeth(row[x - bpp], previous[x], previous[x - bpp]));
        }
    }

    int Png::Paeth(int a, int b, int c)
    {
        auto p = a + b - c;
        auto pa = Math::Abs(p - a);
        auto pb = Math::Abs(p - b);
        auto pc = Math::Abs(p - c);
        return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
    }

    std::vector<byte> Png::ToRgba(const std::vector<byte>& raw, int width, int height, int bpp)
    {
        std::vector<byte> rgba(static_cast<size_t>(width) * height * Rgba);
        for (auto y = 0; y < height; y++)
        {
            auto source = raw.begin() + y * (width * bpp + 1) + 1;
            auto target = rgba.begin() + y * width * Rgba;
            if (bpp == Rgba)
            {
                std::copy_n(source, width * Rgba, target);
                continue;
            }

            for (auto x = 0; x < width; x++)
            {
                auto pixel = source + x * bpp;
                auto colour = bpp >= 3;
                target[x * Rgba] = pixel[0];
                target[x * Rgba + 1] = colour ? pixel[1] : pixel[0];
                target[x * Rgba + 2] = colour ? pixel[2] : pixel[0];
                target[x * Rgba + 3] = bpp == 2 ? pixel[1] : Opaque;
            }
        }

        return rgba;
    }

    void Png::WriteChunk(std::vector<byte>& stream, const std::string& type, std::span<const byte> data)
    {
        WriteUInt32BigEndian(stream, static_cast<uint>(data.size()));
        std::vector<byte> name(type.begin(), type.end());
        stream.insert(stream.end(), name.begin(), name.end());
        stream.insert(stream.end(), data.begin(), data.end());
        WriteUInt32BigEndian(stream, Crc(name, data));
    }

    uint Png::Crc(std::span<const byte> type, std::span<const byte> data)
    {
        auto crc = Crc(0xFFFFFFFFu, type);
        return Crc(crc, data) ^ 0xFFFFFFFFu;
    }

    uint Png::Crc(uint crc, std::span<const byte> bytes)
    {
        static const auto CrcTable = MakeCrcTable();
        for (auto b : bytes)
        {
            crc = CrcTable[(crc ^ b) & 0xFF] ^ (crc >> 8);
        }

        return crc;
    }

    std::array<uint, 256> Png::MakeCrcTable()
    {
        constexpr uint polynomial = 0xEDB88320;
        std::array<uint, 256> table{};
        for (uint n = 0; n < table.size(); n++)
        {
            auto c = n;
            for (auto k = 0; k < 8; k++)
            {
                c = (c & 1) != 0 ? polynomial ^ (c >> 1) : c >> 1;
            }

            table[n] = c;
        }

        return table;
    }
}
