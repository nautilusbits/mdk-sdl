#include "Mdk.Formats/Dti.h"

#include "Mdk.Core/Files.h"

namespace Mdk::Formats
{
    std::shared_ptr<Dti> Dti::Load(const std::string& path)
    {
        auto dti = std::make_shared<Dti>();
        dti->_bytes = Share(File::ReadAllBytes(path));
        dti->Parse();
        return dti;
    }

    int Dti::Block(int index) const { return 4 + static_cast<int>(Bin::U32(*_bytes, BlockTable + index * 4)); }

    void Dti::Parse()
    {
        auto& bytes = *_bytes;
        auto paletteOffset = Block(3) + 4;
        Palette = Palette::FromRgb(Bin::Slice(bytes, paletteOffset, Palette::Size * 3));

        BinReader r0(bytes, Block(0));
        StartArena = static_cast<int>(r0.U32());
        StartPosition = r0.Vec3();
        StartAngle = r0.F32();
        SkyTopColor = static_cast<int>(r0.U32());
        SkyBottomColor = static_cast<int>(r0.U32());
        SkyHorizonRow = static_cast<int>(r0.U32());
        SkyOffset = static_cast<int>(r0.U32());
        SkyWrapWidth = static_cast<int>(r0.U32());
        auto skyHeight = static_cast<int>(r0.U32());
        // Positive when the level has a second panorama, for the mirrors.
        auto secondSkyTop = r0.S32();
        r0.S32();
        for (auto i = 0; i < GlassCount; i++)
        {
            Glass.push_back(std::vector<uint>{r0.U32(), r0.U32(), r0.U32(), r0.U32()});
        }

        Sky = Panorama(Block(4), skyHeight);
        MirrorSky = secondSkyTop > 0 ? Panorama(Block(4) + Sky->Width * Sky->Height, skyHeight) : Sky;

        BinReader r(bytes, Block(2));
        auto count = r.U32();
        for (uint i = 0; i < count; i++)
        {
            auto name = r.Name(8);
            auto recordsOffset = 4 + static_cast<int>(r.U32());
            auto pitch = r.F32();
            Arenas.push_back(ArenaEntry{name, pitch, ParseRecords(recordsOffset)});
        }
    }

    std::shared_ptr<Texture> Dti::Panorama(int offset, int height) const
    {
        auto width = SkyWrapWidth + SkyWrapExtra;
        auto texture = std::make_shared<Texture>();
        texture->Name = "SKY";
        texture->Width = width;
        texture->Height = height;
        texture->Indices = Bin::Slice(*_bytes, offset, width * height);
        return texture;
    }

    std::vector<Dti::Record> Dti::ParseRecords(int offset) const
    {
        std::vector<Record> records;
        BinReader r(*_bytes, offset);
        auto count = r.U32();
        for (uint i = 0; i < count; i++)
        {
            auto type = r.U32();
            auto id = r.S32();
            auto angle = r.F32();
            auto position = r.Vec3();
            // Boxes (fans, wind zones) keep their far corner in the name's bytes.
            auto boxEnd = r.Vec3();
            r.Skip(-12);
            records.push_back(Record{type, id, angle, position, boxEnd, r.Name(12)});
        }

        return records;
    }
}
