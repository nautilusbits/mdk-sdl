#include "formats/Flc.h"

#include <algorithm>
#include <stdexcept>

#include "core/Files.h"

namespace Mdk::Formats
{
    Flc::Flc(std::vector<byte> bytes) : _bytes(std::move(bytes))
    {
        _frameCount = Bin::U16(_bytes, 6);
        _width = Bin::U16(_bytes, 8);
        _height = Bin::U16(_bytes, 10);
        _speed = static_cast<int>(Bin::U32(_bytes, 16));
        auto first = static_cast<int>(Bin::U32(_bytes, FirstFrameOffset));
        _firstFrame = first == 0 ? HeaderSize : first;
        Indices.resize(static_cast<size_t>(_width) * _height);
        Rewind();
    }

    std::shared_ptr<Flc> Flc::Load(const std::string& path)
    {
        auto bytes = File::ReadAllBytes(path);
        if (bytes.size() < HeaderSize || Bin::U16(bytes, 4) != Magic)
        {
            return nullptr;
        }

        return std::shared_ptr<Flc>(new Flc(std::move(bytes)));
    }

    void Flc::Rewind()
    {
        _pos = _firstFrame;
        _frame = 0;
        std::fill(Indices.begin(), Indices.end(), byte{0});
    }

    bool Flc::NextFrame()
    {
        if (_frame >= _frameCount)
        {
            return false;
        }

        while (_pos + ChunkHeaderSize <= static_cast<int>(_bytes.size()))
        {
            auto size = static_cast<int>(Bin::U32(_bytes, _pos));
            auto type = static_cast<Chunk>(Bin::U16(_bytes, _pos + 4));
            auto start = _pos;
            _pos += Math::Max(size, ChunkHeaderSize);
            if (type != Chunk::Frame)
            {
                continue;
            }

            int chunks = Bin::U16(_bytes, start + 6);
            auto p = start + FrameHeaderSize;
            for (auto i = 0; i < chunks; i++)
            {
                auto chunkSize = static_cast<int>(Bin::U32(_bytes, p));
                DecodeChunk(static_cast<Chunk>(Bin::U16(_bytes, p + 4)), p + ChunkHeaderSize);
                p += Math::Max(chunkSize, ChunkHeaderSize);
            }

            _frame++;
            return true;
        }

        return false;
    }

    void Flc::DecodeChunk(Chunk type, int p)
    {
        switch (type)
        {
            case Chunk::Color256: DecodePalette(p, 1); break;
            case Chunk::Color64: DecodePalette(p, Levels64Scale); break;
            case Chunk::ByteRun: DecodeByteRun(p); break;
            case Chunk::DeltaFlc: DecodeDeltaFlc(p); break;
            case Chunk::DeltaFli: DecodeDeltaFli(p); break;
            case Chunk::Black: std::fill(Indices.begin(), Indices.end(), byte{0}); break;
            case Chunk::Copy: CopyToIndices(p, static_cast<int>(Indices.size()), 0); break;
            default: break;
        }
    }

    void Flc::DecodePalette(int p, int scale)
    {
        int packets = Bin::U16(_bytes, p);
        p += 2;
        auto colour = 0;
        for (auto i = 0; i < packets; i++)
        {
            colour += _bytes.at(p);
            int count = _bytes.at(p + 1);
            count = count == 0 ? FullCount : count;
            p += 2;
            for (auto k = 0; k < count * 3; k++)
            {
                if (colour * 3 + k < PaletteBytes)
                {
                    Palette[colour * 3 + k] = static_cast<byte>(Math::Min(_bytes.at(p + k) * scale, 255));
                }
            }

            p += count * 3;
            colour += count;
        }
    }

    void Flc::DecodeByteRun(int p)
    {
        for (auto y = 0; y < _height; y++)
        {
            p++;
            auto x = 0;
            auto row = y * _width;
            while (x < _width)
            {
                int count = static_cast<sbyte>(_bytes.at(p++));
                if (count < 0)
                {
                    CopyToIndices(p, -count, row + x);
                    p += -count;
                    x += -count;
                    continue;
                }

                FillIndices(row + x, count, _bytes.at(p++));
                x += count;
            }
        }
    }

    void Flc::DecodeDeltaFlc(int p)
    {
        int lines = Bin::U16(_bytes, p);
        p += 2;
        auto y = 0;
        for (auto line = 0; line < lines; line++)
        {
            auto packets = 0;
            while (true)
            {
                int word = Bin::U16(_bytes, p);
                p += 2;
                if ((word & OpcodeMask) == OpcodeSkip)
                {
                    y += SkipBase - word;
                }
                else if ((word & OpcodeMask) == OpcodeLastByte)
                {
                    Indices.at(y * _width + _width - 1) = static_cast<byte>(word);
                }
                else
                {
                    packets = word;
                    break;
                }
            }

            auto x = 0;
            auto row = y * _width;
            for (auto i = 0; i < packets; i++)
            {
                x += _bytes.at(p);
                int count = static_cast<sbyte>(_bytes.at(p + 1));
                p += 2;
                if (count > 0)
                {
                    CopyToIndices(p, count * 2, row + x);
                    p += count * 2;
                    x += count * 2;
                    continue;
                }

                auto a = _bytes.at(p);
                auto b = _bytes.at(p + 1);
                p += 2;
                for (auto k = 0; k < -count; k++)
                {
                    Indices.at(row + x) = a;
                    Indices.at(row + x + 1) = b;
                    x += 2;
                }
            }

            y++;
        }
    }

    void Flc::DecodeDeltaFli(int p)
    {
        int y = Bin::U16(_bytes, p);
        int lines = Bin::U16(_bytes, p + 2);
        p += 4;
        for (auto line = 0; line < lines; line++)
        {
            int packets = _bytes.at(p++);
            auto x = 0;
            auto row = y * _width;
            for (auto i = 0; i < packets; i++)
            {
                x += _bytes.at(p);
                int count = static_cast<sbyte>(_bytes.at(p + 1));
                p += 2;
                if (count > 0)
                {
                    CopyToIndices(p, count, row + x);
                    p += count;
                    x += count;
                    continue;
                }

                FillIndices(row + x, -count, _bytes.at(p++));
                x += -count;
            }

            y++;
        }
    }

    void Flc::CopyToIndices(int from, int count, int at)
    {
        auto source = Bin::Slice(_bytes, from, count);
        if (at < 0 || at + count > static_cast<int>(Indices.size()))
        {
            throw std::out_of_range("Destination is too short.");
        }

        std::copy(source.begin(), source.end(), Indices.begin() + at);
    }

    void Flc::FillIndices(int at, int count, byte value)
    {
        if (at < 0 || count < 0 || at + count > static_cast<int>(Indices.size()))
        {
            throw std::out_of_range("Specified argument was out of the range of valid values.");
        }

        std::fill_n(Indices.begin() + at, count, value);
    }
}
