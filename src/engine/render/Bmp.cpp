#include "engine/render/Bmp.h"

#include "core/Files.h"

#include <vector>

namespace Mdk::Engine::Render
{
    namespace
    {
        // BinaryPrimitives.WriteInt32LittleEndian / WriteInt16LittleEndian.
        void WriteInt32(byte* at, int value)
        {
            const auto bits = static_cast<uint>(value);
            for (int i = 0; i < 4; i++)
            {
                at[i] = static_cast<byte>(bits >> (8 * i));
            }
        }

        void WriteInt16(byte* at, short value)
        {
            const auto bits = static_cast<ushort>(value);
            at[0] = static_cast<byte>(bits);
            at[1] = static_cast<byte>(bits >> 8);
        }
    }

    void Bmp::Write(const std::string& path, int width, int height, std::span<const byte> rgba)
    {
        const auto dataSize = width * height * 4;
        std::vector<byte> file(FileHeaderSize + InfoHeaderSize + dataSize);
        auto* span = file.data();

        // File header: "BM", size, reserved, pixel data offset.
        span[0] = static_cast<byte>('B');
        span[1] = static_cast<byte>('M');
        WriteInt32(span + 2, static_cast<int>(file.size()));
        WriteInt32(span + 10, FileHeaderSize + InfoHeaderSize);

        // Info header: a negative height stores rows top-down.
        WriteInt32(span + 14, InfoHeaderSize);
        WriteInt32(span + 18, width);
        WriteInt32(span + 22, -height);
        WriteInt16(span + 26, 1);
        WriteInt16(span + 28, BitsPerPixel);

        // BMP pixels are BGRA.
        auto* pixels = span + (FileHeaderSize + InfoHeaderSize);
        for (int i = 0; i < dataSize; i += 4)
        {
            pixels[i] = rgba[i + 2];
            pixels[i + 1] = rgba[i + 1];
            pixels[i + 2] = rgba[i];
            pixels[i + 3] = rgba[i + 3];
        }

        File::WriteAllBytes(path, file);
    }
}
