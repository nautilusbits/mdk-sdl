#include "formats/Gif.h"

#include <algorithm>

#include "core/Files.h"

namespace Mdk::Formats
{
    Gif::Gif(std::vector<byte> bytes) : _bytes(std::move(bytes))
    {
        _width = Bin::U16(_bytes, 6);
        _height = Bin::U16(_bytes, 8);
    }

    std::shared_ptr<Gif> Gif::Load(const std::string& path)
    {
        auto bytes = File::ReadAllBytes(path);
        if (bytes.size() < HeaderSize || Bin::Ascii(bytes, 0, 3) != "GIF")
        {
            return nullptr;
        }

        auto gif = std::shared_ptr<Gif>(new Gif(std::move(bytes)));
        return gif->Parse() ? gif : nullptr;
    }

    bool Gif::Parse()
    {
        auto p = HeaderSize;
        int fields = _bytes[10];
        if ((fields & HasTable) != 0)
        {
            p = ReadPalette(p, fields);
        }

        while (p < static_cast<int>(_bytes.size()))
        {
            auto block = _bytes[p++];
            if (block == Extension)
            {
                p = SkipSubBlocks(p + 1);
                continue;
            }

            return block == Image && ReadImage(p);
        }

        return false;
    }

    int Gif::ReadPalette(int p, int fields)
    {
        auto count = 2 << (fields & TableSize);
        auto colours = Bin::Slice(_bytes, p, count * 3);
        std::copy(colours.begin(), colours.end(), Palette.begin());
        return p + count * 3;
    }

    int Gif::SkipSubBlocks(int p) const
    {
        while (p < static_cast<int>(_bytes.size()) && _bytes[p] != 0)
        {
            p += _bytes[p] + 1;
        }

        return p + 1;
    }

    bool Gif::ReadImage(int p)
    {
        int left = Bin::U16(_bytes, p);
        int top = Bin::U16(_bytes, p + 2);
        int imageWidth = Bin::U16(_bytes, p + 4);
        int imageHeight = Bin::U16(_bytes, p + 6);
        int fields = _bytes.at(p + 8);
        p += 9;
        if ((fields & HasTable) != 0)
        {
            p = ReadPalette(p, fields);
        }

        int codeSize = _bytes.at(p++);
        std::vector<byte> data;
        while (p < static_cast<int>(_bytes.size()) && _bytes[p] != 0)
        {
            auto block = Bin::Slice(_bytes, p + 1, _bytes[p]);
            data.insert(data.end(), block.begin(), block.end());
            p += _bytes[p] + 1;
        }

        auto pixels = Lzw(data, codeSize, imageWidth * imageHeight);
        _indices.assign(static_cast<size_t>(_width) * _height, 0);
        auto rows = RowOrder(imageHeight, (fields & Interlaced) != 0 ? Interlace::On : Interlace::Off);
        for (auto i = 0; i < imageHeight; i++)
        {
            auto y = rows[i] + top;
            if (y >= _height)
            {
                continue;
            }

            auto count = Math::Min(imageWidth, _width - left);
            auto at = y * _width + left;
            if (count < 0 || at + count > static_cast<int>(_indices.size()))
            {
                throw std::out_of_range("Destination is too short.");
            }

            std::copy_n(pixels.begin() + i * imageWidth, count, _indices.begin() + at);
        }

        return true;
    }

    std::vector<int> Gif::RowOrder(int rows, Interlace interlace)
    {
        std::vector<int> order;
        if (interlace == Interlace::Off)
        {
            for (auto y = 0; y < rows; y++)
            {
                order.push_back(y);
            }

            return order;
        }

        for (auto [start, step] : Passes)
        {
            for (auto y = start; y < rows; y += step)
            {
                order.push_back(y);
            }
        }

        return order;
    }

    std::vector<byte> Gif::Lzw(const std::vector<byte>& data, int minSize, int count)
    {
        std::vector<byte> output(count);
        auto clear = 1 << (minSize & 31);
        auto end = clear + 1;
        constexpr int Codes = 1 << MaxCodeSize;

        // Each code is a prefix code and its last byte; strings are rebuilt backwards.
        std::vector<int> prefixes(Codes);
        std::vector<byte> suffixes(Codes);
        std::vector<byte> firsts(Codes);
        for (auto i = 0; i < clear; i++)
        {
            prefixes.at(i) = -1;
            suffixes.at(i) = static_cast<byte>(i);
            firsts.at(i) = static_cast<byte>(i);
        }

        std::vector<byte> stack(Codes);
        auto size = minSize + 1;
        auto next = end + 1;
        auto previous = -1;
        auto bits = 0;
        auto bitCount = 0;
        auto written = 0;
        size_t p = 0;
        while (written < count)
        {
            while (bitCount < size && p < data.size())
            {
                bits |= data[p++] << bitCount;
                bitCount += 8;
            }

            if (bitCount < size)
            {
                break;
            }

            auto code = bits & ((1 << size) - 1);
            bits >>= size;
            bitCount -= size;
            if (code == clear)
            {
                size = minSize + 1;
                next = end + 1;
                previous = -1;
                continue;
            }

            if (code == end)
            {
                break;
            }

            // A code not defined yet is the previous string plus its own first byte.
            auto current = code;
            auto depth = 0;
            if (code >= next && previous >= 0)
            {
                stack.at(depth++) = firsts[previous];
                current = previous;
            }

            while (current >= 0)
            {
                stack.at(depth++) = suffixes.at(current);
                current = prefixes[current];
            }

            for (auto k = depth - 1; k >= 0 && written < count; k--)
            {
                output[written++] = stack[k];
            }

            if (previous >= 0 && next < Codes)
            {
                prefixes[next] = previous;
                suffixes[next] = stack[depth - 1];
                firsts[next] = firsts[previous];
                next++;
                if (next == 1 << size && size < MaxCodeSize)
                {
                    size++;
                }
            }

            previous = code;
        }

        return output;
    }
}
