#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "core/Numerics.h"
#include "formats/BinReader.h"

namespace Mdk::Formats
{
    // An Interplay MVE movie (MISC/FLIC/MDKBZK.MVE, the end of the game): chunks of
    // opcodes that set up the screen and sound, then per frame a decoding map (4 bits per 8x8 block),
    // the blocks' data (video format 0x11, 8 bits) and DPCM sound. NextFrame decodes up
    // to the next shown frame.
    //   signature (26) ─► chunk [u16 size, u16 type] ─► opcode [u16 size, u8 type, u8 version, data]
    // Frames decode into the back buffer, which still holds the frame before the last (double
    // buffering): blocks stay, copy from the frame shown last or from the back buffer, or are drawn
    // from 1 to 64 colours.
    class Mve
    {
    public:
        int Width() const { return _width; }
        int Height() const { return _height; }
        // Microseconds per frame.
        int FrameTime() const { return _frameTime; }
        int SampleRate() const { return _sampleRate; }
        int Channels() const { return _channels; }
        // 256 RGB, 0-255.
        std::array<byte, 768> Palette{};
        // Sound decoded since the last call: stereo frames (-1 to 1).
        std::vector<Vector2> Audio;
        int Frame() const { return _frame; }

        // The frame shown last (palette indices).
        const std::vector<byte>& Indices() const { return _last; }

        static std::shared_ptr<Mve> Load(const std::string& path);

        // Runs opcodes up to the next shown frame; false at the end of the movie.
        bool NextFrame();

    private:
        static constexpr std::string_view Signature = "Interplay MVE File\x1a";
        static constexpr int SignatureSize = 26;
        static constexpr int ChunkHeader = 4;
        static constexpr int OpcodeHeader = 4;
        static constexpr int PaletteBytes = 768;

        enum class Opcode : byte
        {
            EndOfStream = 0x00,
            CreateTimer = 0x02,
            InitAudio = 0x03,
            InitVideo = 0x05,
            SendBuffer = 0x07,
            AudioFrame = 0x08,
            SilenceFrame = 0x09,
            SetPalette = 0x0C,
            DecodingMap = 0x0F,
            VideoData = 0x11,
        };

        // Audio flags: stereo, 16 bits, compressed (DPCM, from version 1).
        static constexpr int AudioStereo = 1;
        static constexpr int Audio16Bit = 2;
        static constexpr int AudioCompressed = 4;
        // The sound of stream 0 only.
        static constexpr int StreamMask = 1;
        // The video data starts with a 14-byte header.
        static constexpr int VideoHeader = 14;
        static constexpr int Block = 8;
        static constexpr float SampleScale = 1.0f / 32768.0f;
        static constexpr int ByteMiddle = 128;

        // Interplay DPCM: each byte indexes this table of steps.
        static const std::array<short, 256> Deltas;

        std::vector<byte> _bytes;
        int _pos = SignatureSize;
        int _audioFlags = 0;
        // The back buffer (being decoded) and the frame shown last.
        std::vector<byte> _current;
        std::vector<byte> _last;
        std::vector<byte> _map;
        std::array<int, 2> _predictors{};
        bool _shown = false;
        bool _ended = false;
        int _width = 0;
        int _height = 0;
        int _frameTime = 0;
        int _sampleRate = 0;
        int _channels = 1;
        int _frame = 0;

        explicit Mve(std::vector<byte> bytes) : _bytes(std::move(bytes)) {}

        void Run(Opcode type, int version, int p, int length);
        void InitVideo(int width, int height);

        // 6-bit levels scaled to 8 bits as ffmpeg does (v × 4 + v / 16).
        void SetPalette(int p);

        void DecodeAudio(int p, int end, int size);
        void DecodePcm(int p, int end);
        void PushSamples(const std::vector<int>& samples, int count);

        // Video: one opcode (4 bits of the map, low nibble first) per 8x8 block.
        void DecodeVideo(int p, int end);

        // Decodes one block at (x, y) from the data at p; returns where the next block's data starts.
        int DecodeBlock(int code, int x, int y, int p);

        // Copies the 8x8 block from `source` at the block's place moved by (dx, dy).
        void Copy(const std::vector<byte>& source, int o, int dx, int dy);

        void Fill(int o, int w, int h, byte value);

        // Two colours: P0 ≤ P1, a bit per pixel (8 bytes, a row each, low bit first); else a
        // bit per 2x2 (16 bits).
        int TwoColours(int o, int p);

        // Two colours per 4x4 quadrant (P0 ≤ P1: top-left, bottom-left, top-right,
        // bottom-right, each with its colours and 16 bits), or per half (P2 ≤ P3: left and right, else
        // top and bottom; 32 bits each).
        int TwoColourQuadrants(int o, int p);

        // Four colours: P0 ≤ P1 and P2 ≤ P3: 2 bits per pixel (16 bytes); P0 ≤ P1: per 2x2 (4
        // bytes); else 64 bits per 2x1 (P2 ≤ P3) or 1x2 pairs.
        int FourColours(int o, int p);

        // Four colours per 4x4 quadrant (P0 ≤ P1: each with its colours and 32 bits), or per
        // half (P4 ≤ P5 of the second half: left and right, else top and bottom; 64 bits each).
        int FourColourQuadrants(int o, int p);
    };
}
