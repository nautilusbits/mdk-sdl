#include "Mdk.Formats/TextureArchive.h"

#include <algorithm>

#include "Mdk.Core/Files.h"
#include "Mdk.Formats/BinReader.h"

namespace Mdk::Formats
{
    std::shared_ptr<TextureArchive> TextureArchive::Parse(std::span<const byte> bytes, int baseOffset)
    {
        return Parse(bytes, baseOffset, Header, Names::AsStored, Zero::ByKind);
    }

    std::shared_ptr<TextureArchive> TextureArchive::Parse(std::span<const byte> bytes, int baseOffset, int header, Names names, Zero zero)
    {
        auto archive = std::make_shared<TextureArchive>();
        BinReader r(bytes, baseOffset + header);
        auto count = r.U32();
        for (uint i = 0; i < count; i++)
        {
            auto name = names == Names::Upper ? Str::ToUpper(r.Name(8)) : r.Name(8);
            auto kind = r.U32();
            auto value = r.U32();
            r.F32();
            auto offset = baseOffset + static_cast<int>(r.U32());
            if (kind == KindColor)
            {
                archive->Colors[name] = static_cast<int>(value);
                continue;
            }

            if ((kind & KindAnimatedMask) != 0)
            {
                archive->Textures[name] = Texture::ParseAnimated(name, bytes, offset, kind);
                continue;
            }

            auto texture = Texture::Parse(name, bytes, offset);
            archive->Textures[name] = texture;

            // LEVEL8's walls paint black with index 0 (I2_WALL1): opaque unless keyed.
            if (zero == Zero::ByKind && (kind & KindKeyed) == 0)
            {
                std::replace(texture->Indices.begin(), texture->Indices.end(), byte{0}, OpaqueBlack);
            }
        }

        return archive;
    }

    std::shared_ptr<TextureArchive> TextureArchive::Load(const std::string& path, Zero zero)
    {
        return Parse(File::ReadAllBytes(path), FileBase, Header, Names::AsStored, zero);
    }
}
