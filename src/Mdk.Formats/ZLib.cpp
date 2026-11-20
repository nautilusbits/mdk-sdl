#include "Mdk.Formats/ZLib.h"

#include <miniz.h>

namespace Mdk::Formats
{
    namespace
    {
        constexpr int OptimalLevel = 6;
        constexpr int FastestLevel = 1;
    }

    std::vector<byte> ZLib::Compress(std::span<const byte> data, Level level)
    {
        auto bound = mz_compressBound(static_cast<mz_ulong>(data.size()));
        std::vector<byte> compressed(bound);
        auto length = bound;
        auto status = mz_compress2(compressed.data(), &length, data.data(), static_cast<mz_ulong>(data.size()), level == Level::Fastest ? FastestLevel : OptimalLevel);
        if (status != MZ_OK)
        {
            throw IOException("zlib compression failed");
        }

        compressed.resize(length);
        return compressed;
    }

    std::vector<byte> ZLib::DecompressExactly(std::span<const byte> data, int size)
    {
        std::vector<byte> raw(size);
        mz_stream stream{};
        stream.next_in = data.data();
        stream.avail_in = static_cast<unsigned int>(data.size());
        stream.next_out = raw.data();
        stream.avail_out = static_cast<unsigned int>(raw.size());
        if (mz_inflateInit(&stream) != MZ_OK)
        {
            throw InvalidDataException("zlib stream could not start");
        }

        int status = MZ_OK;
        while (stream.avail_out > 0)
        {
            status = mz_inflate(&stream, MZ_NO_FLUSH);
            if (status == MZ_STREAM_END)
            {
                break;
            }

            if (status != MZ_OK)
            {
                break;
            }
        }

        auto missing = stream.avail_out;
        mz_inflateEnd(&stream);
        if (missing == 0)
        {
            return raw;
        }

        if (status == MZ_STREAM_END || status == MZ_BUF_ERROR)
        {
            throw IOException("Unable to read beyond the end of the stream.");
        }

        throw InvalidDataException("The archive entry was compressed using an unsupported compression method.");
    }
}
