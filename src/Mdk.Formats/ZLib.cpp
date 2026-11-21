#include "Mdk.Formats/ZLib.h"

#include <libdeflate.h>
#include <miniz.h>

#include <memory>

namespace Mdk::Formats
{
    namespace
    {
        constexpr int OptimalLevel = 6;
        constexpr int FastestLevel = 1;
    }

    std::vector<byte> ZLib::Compress(std::span<const byte> data, Level level)
    {
        // libdeflate: same zlib format as miniz/.NET, much faster at the same level.
        std::unique_ptr<libdeflate_compressor, decltype(&libdeflate_free_compressor)> compressor(
            libdeflate_alloc_compressor(level == Level::Fastest ? FastestLevel : OptimalLevel), libdeflate_free_compressor);
        if (!compressor)
        {
            throw IOException("zlib compression failed");
        }

        std::vector<byte> compressed(libdeflate_zlib_compress_bound(compressor.get(), data.size()));
        auto length = libdeflate_zlib_compress(compressor.get(), data.data(), data.size(), compressed.data(), compressed.size());
        if (length == 0)
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
