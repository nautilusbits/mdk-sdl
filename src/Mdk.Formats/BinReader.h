#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"

namespace Mdk::Formats
{
    // A file's bytes kept by the objects read from it (C#'s shared byte[] reference).
    using SharedBytes = std::shared_ptr<const std::vector<byte>>;

    inline SharedBytes Share(std::vector<byte> bytes) { return std::make_shared<const std::vector<byte>>(std::move(bytes)); }

    // Sequential little-endian reader over a byte array.
    class BinReader
    {
    public:
        BinReader(std::span<const byte> bytes, int pos = 0) : Bytes(bytes), Pos(pos) {}

        std::span<const byte> Bytes;
        int Pos;

        bool Eof() const { return Pos >= static_cast<int>(Bytes.size()); }

        void Skip(int count) { Pos += count; }

        byte U8();
        ushort U16();
        short S16();
        uint U32();
        int S32();
        float F32();
        Vector3 Vec3();

        // Reads a fixed-length, NUL-padded ASCII string.
        std::string Name(int length);

        std::vector<byte> Buffer(int length);
    };

    // Random-access reads at an offset.
    struct Bin
    {
        static ushort U16(std::span<const byte> b, int at);
        static short S16(std::span<const byte> b, int at);
        static uint U32(std::span<const byte> b, int at);
        static int S32(std::span<const byte> b, int at);
        static float F32(std::span<const byte> b, int at);

        // ASCII up to the first NUL within `length` bytes.
        static std::string Ascii(std::span<const byte> b, int at, int length);

        // Encoding.ASCII.GetString: bytes above 0x7F become '?'.
        static std::string AsciiString(std::span<const byte> b);

        // span.Slice(at, length).ToArray(), bounds checked as C#.
        static std::vector<byte> Slice(std::span<const byte> b, int at, int length);
    };
}
