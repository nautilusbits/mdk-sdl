#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "formats/BinReader.h"

namespace Mdk::Formats
{
    // An Autodesk FLC animation (MISC/FLIC/*.FLC): 8-bit frames with a palette, decoded
    // one frame at a time into Indices and Palette.
    //   header (128) ─► frame 1 (palette + BYTE_RUN) ─► frames 2..n (DELTA_FLC) ─► ring frame (to frame 1)
    class Flc
    {
    public:
        int Width() const { return _width; }
        int Height() const { return _height; }
        // Frames to show (the file has one more, the ring frame).
        int FrameCount() const { return _frameCount; }
        // Milliseconds per frame.
        int Speed() const { return _speed; }
        // The current frame's palette indices and palette (256 RGB, 0-255).
        std::vector<byte> Indices;
        std::array<byte, 768> Palette{};
        // Frames decoded since the start.
        int Frame() const { return _frame; }

        static std::shared_ptr<Flc> Load(const std::string& path);

        // Back to before the first frame.
        void Rewind();

        // Decodes the next frame; false after the last.
        bool NextFrame();

    private:
        static constexpr ushort Magic = 0xAF12;
        static constexpr int HeaderSize = 128;
        static constexpr int FrameHeaderSize = 16;
        static constexpr int ChunkHeaderSize = 6;
        static constexpr int PaletteBytes = 768;
        static constexpr int FirstFrameOffset = 80;

        // Chunk types: frames; in a frame the palette (256 or 64 levels), deltas (word FLC,
        // byte FLI), all black, a full run-length frame, raw pixels.
        enum class Chunk : ushort
        {
            Color256 = 4,
            DeltaFlc = 7,
            Color64 = 11,
            DeltaFli = 12,
            Black = 13,
            ByteRun = 15,
            Copy = 16,
            Frame = 0xF1FA,
        };

        // DELTA_FLC line words: the top bits tell a packet count (00), a line skip (11) or the
        // last pixel of an odd-width line (10).
        static constexpr int OpcodeMask = 0xC000;
        static constexpr int OpcodeSkip = 0xC000;
        static constexpr int OpcodeLastByte = 0x8000;
        static constexpr int SkipBase = 0x10000;
        static constexpr int Levels64Scale = 4;
        static constexpr int FullCount = 256;

        std::vector<byte> _bytes;
        int _firstFrame;
        int _pos = 0;
        int _width;
        int _height;
        int _frameCount;
        int _speed;
        int _frame = 0;

        explicit Flc(std::vector<byte> bytes);

        void DecodeChunk(Chunk type, int p);

        // Packets of (colours to skip, colours to set (0 = 256), RGB...); 64 levels scaled.
        void DecodePalette(int p, int scale);

        // Per line a packet count (ignored), then runs: negative copies, positive repeats.
        void DecodeByteRun(int p);

        // Lines of word packets: line words (skips, odd lines' last byte, the packet count),
        // then packets of (columns to skip, count): positive copies words, negative repeats one.
        void DecodeDeltaFlc(int p);

        // The older byte delta: first line, line count, then per line packets of (skip,
        // count): positive copies bytes, negative repeats one.
        void DecodeDeltaFli(int p);

        // span.CopyTo(Indices.AsSpan(at)), bounds checked as C#.
        void CopyToIndices(int from, int count, int at);
        // Indices.AsSpan(at, count).Fill(value), bounds checked as C#.
        void FillIndices(int at, int count, byte value);
    };
}
