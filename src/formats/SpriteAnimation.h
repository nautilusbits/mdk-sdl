#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "formats/BinReader.h"
#include "formats/Texture.h"

namespace Mdk::Formats
{
    // An RLE sprite animation, such as Kurt's K_RUN in TRAVSPRT.BNI.
    //   u32 frame count, u32 frame offsets[count] (from the count), then per frame:
    //     u16 width, u16 height, s16 hotspot x, s16 hotspot y, rows of commands:
    //     0x00-0x7F  n + 1 literal indices follow
    //     0x80-0xFD  the next index repeated n - 0x7C times
    //     0xFE       end of row (the rest is transparent)
    //     0xFF       end of frame
    // Palette index 0 is transparent.
    class SpriteAnimation
    {
    public:
        // A frame and its anchor. Kurt's top-left corner is drawn at (feet x - hotspot x,
        // feet y - 101 - hotspot y) on a 600x360 view.
        struct Frame
        {
            std::shared_ptr<Texture> Image;
            int HotspotX = 0;
            int HotspotY = 0;

            bool operator==(const Frame&) const = default;
        };

        const std::string& Name() const { return _name; }
        int FrameCount() const { return _frameCount; }

        static std::shared_ptr<SpriteAnimation> Parse(const std::string& name, SharedBytes bytes, int offset);

        // Frame `index`, decoded on first use.
        Frame GetFrame(int index);

    private:
        static constexpr byte LiteralLast = 0x7F;
        static constexpr byte EndOfRow = 0xFE;
        static constexpr int RepeatBias = 0x7C;

        SharedBytes _bytes;
        int _base;
        std::unordered_map<int, Frame> _frames;
        std::string _name;
        int _frameCount;

        SpriteAnimation(const std::string& name, SharedBytes bytes, int offset);

        Frame Decode(int offset);
    };
}
