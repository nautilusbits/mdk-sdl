#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Formats
{
    // An 8-bit paletted texture: u16 width, u16 height, then palette indices row by row.
    // Animated textures keep their frames one after another.
    class Texture
    {
    public:
        // Animated kind whose frames are changes to the first one (M_COMM).
        static constexpr uint Deltas = 0x20000;

        std::string Name;
        int Width = 0;
        // Height of one frame.
        int Height = 0;
        int FrameCount = 1;
        // Palette indices of all frames.
        std::vector<byte> Indices;

        static std::shared_ptr<Texture> Parse(const std::string& name, std::span<const byte> bytes, int offset);

        // u32 frame count, u16 width, u16 height, then the frames back to back, or with
        // Deltas the first frame and the changes making each next one (0x422c00).
        static std::shared_ptr<Texture> ParseAnimated(const std::string& name, std::span<const byte> bytes, int offset, uint kind);

    private:
        static constexpr int DeltaUnit = 4;

        static std::vector<byte> ApplyDeltas(std::span<const byte> bytes, int p, int size, int count);
    };
}
