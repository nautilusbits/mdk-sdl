#include "Mdk.Formats/BinReader.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Mdk::Formats
{
    namespace
    {
        // C#'s AsSpan(at) / BinaryPrimitives throw past the end; C++ would read out of bounds.
        void Check(std::span<const byte> b, int at, int size)
        {
            if (at < 0 || size < 0 || static_cast<size_t>(at) + static_cast<size_t>(size) > b.size())
            {
                throw std::out_of_range("Specified argument was out of the range of valid values.");
            }
        }

        template <typename T>
        T Read(std::span<const byte> b, int at)
        {
            Check(b, at, sizeof(T));
            T value;
            std::memcpy(&value, b.data() + at, sizeof(T));
            return value;
        }
    }

    byte BinReader::U8()
    {
        Check(Bytes, Pos, 1);
        return Bytes[Pos++];
    }

    ushort BinReader::U16()
    {
        Pos += 2;
        return Bin::U16(Bytes, Pos - 2);
    }

    short BinReader::S16()
    {
        Pos += 2;
        return Bin::S16(Bytes, Pos - 2);
    }

    uint BinReader::U32()
    {
        Pos += 4;
        return Bin::U32(Bytes, Pos - 4);
    }

    int BinReader::S32()
    {
        Pos += 4;
        return Bin::S32(Bytes, Pos - 4);
    }

    float BinReader::F32()
    {
        Pos += 4;
        return Bin::F32(Bytes, Pos - 4);
    }

    Vector3 BinReader::Vec3()
    {
        // Braces: the reads run in order, as C#'s arguments.
        return Vector3{F32(), F32(), F32()};
    }

    std::string BinReader::Name(int length)
    {
        Pos += length;
        return Bin::Ascii(Bytes, Pos - length, length);
    }

    std::vector<byte> BinReader::Buffer(int length)
    {
        Pos += length;
        return Bin::Slice(Bytes, Pos - length, length);
    }

    ushort Bin::U16(std::span<const byte> b, int at) { return Read<ushort>(b, at); }
    short Bin::S16(std::span<const byte> b, int at) { return Read<short>(b, at); }
    uint Bin::U32(std::span<const byte> b, int at) { return Read<uint>(b, at); }
    int Bin::S32(std::span<const byte> b, int at) { return Read<int>(b, at); }
    float Bin::F32(std::span<const byte> b, int at) { return Read<float>(b, at); }

    std::string Bin::Ascii(std::span<const byte> b, int at, int length)
    {
        auto count = std::min(length, static_cast<int>(b.size()) - at);
        Check(b, at, count);
        auto span = b.subspan(at, count);
        auto end = std::find(span.begin(), span.end(), byte{0});
        return AsciiString(span.subspan(0, end - span.begin()));
    }

    std::string Bin::AsciiString(std::span<const byte> b)
    {
        std::string text(b.size(), '\0');
        for (size_t i = 0; i < b.size(); i++)
        {
            text[i] = b[i] > 0x7F ? '?' : static_cast<char>(b[i]);
        }

        return text;
    }

    std::vector<byte> Bin::Slice(std::span<const byte> b, int at, int length)
    {
        Check(b, at, length);
        return std::vector<byte>(b.begin() + at, b.begin() + at + length);
    }
}
