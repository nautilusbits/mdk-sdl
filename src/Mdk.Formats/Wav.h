#pragma once

#include <optional>
#include <span>
#include <vector>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Formats
{
    // PCM sound from a RIFF WAV (stand-alone like MAINSONG, or in SNI archives). Reads
    // only the "fmt " and "data" chunks: some files miscount a pad byte (GATTFIRE) or
    // truncate LIST chunks.
    struct Wav
    {
        int Channels = 0;
        int SampleRate = 0;
        int BitsPerSample = 0;
        std::vector<byte> Data;

        bool operator==(const Wav&) const = default;

        static std::optional<Wav> Parse(std::span<const byte> wav);

    private:
        static constexpr int RiffHeader = 12;
        static constexpr int ChunkHeader = 8;
    };
}
