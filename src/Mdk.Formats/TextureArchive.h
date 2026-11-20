#pragma once

#include <memory>
#include <span>
#include <string>

#include "Mdk.Core/Dictionary.h"
#include "Mdk.Formats/Texture.h"

namespace Mdk::Formats
{
    // A texture archive: an arena's HMO_n.MAT or a level's LEVELnS.MTI.
    //   char[12] name, u32 size, u32 count, then per entry:
    //     char[8] name, u32 kind, u32 value, f32 ?, u32 offset (from base)
    // kind 0xFFFFFFFF: a palette colour (value; 256 and up are special). High 16 bits set: animated.
    class TextureArchive
    {
    public:
        // The 1996 demo's names are in lower case; the port's in upper case.
        enum class Names { AsStored, Upper };

        // Palette index 0: see-through everywhere, or only in textures of kind bit 0.
        enum class Zero { SeeThrough, ByKind };

        Dictionary<std::string, std::shared_ptr<Texture>> Textures;
        // Name to palette index (or special value of 256 and up).
        Dictionary<std::string, int> Colors;

        static std::shared_ptr<TextureArchive> Parse(std::span<const byte> bytes, int baseOffset);

        // An archive whose count is `header` bytes after `baseOffset`.
        static std::shared_ptr<TextureArchive> Parse(std::span<const byte> bytes, int baseOffset, int header, Names names, Zero zero = Zero::SeeThrough);

        static std::shared_ptr<TextureArchive> Load(const std::string& path, Zero zero = Zero::SeeThrough);

    private:
        static constexpr uint KindColor = 0xFFFFFFFF;
        static constexpr uint KindAnimatedMask = 0xFFFF0000;
        // Uploaded with alpha: index 0 see-through (0x474c9c; the effects: EXPLODE, TRAIL...).
        static constexpr uint KindKeyed = 1;
        // Palette black (BLACK), standing in for index 0 where it's opaque.
        static constexpr byte OpaqueBlack = 16;
        // Standalone files start with a size word.
        static constexpr int FileBase = 4;
        // The name and size before the count; the 1996 demo's archives have none.
        static constexpr int Header = 16;
    };
}
