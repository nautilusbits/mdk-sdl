#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/BinReader.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/Texture.h"

namespace Mdk::Formats
{
    // LEVELn.DTI: level settings, arena list, base palette and sky. Block offsets are
    // relative to the file without its first 4 bytes.
    //   block 0  settings: start, sky fields, GLASS1-4 colours
    //   block 2  arenas and corridors with their object records
    //   block 3  u32 arena colour count, 256-colour palette
    //   block 4  sky panorama(s)
    class Dti
    {
    public:
        static constexpr int GlassCount = 4;

        struct Record
        {
            uint Type = 0;
            int Id = 0;
            float Angle = 0;
            Vector3 Position;
            Vector3 BoxEnd;
            std::string Name;

            bool operator==(const Record&) const = default;
        };

        // An arena or corridor (HMO_n, CHMO_n); Pitch is the camera pitch in
        // degrees (positive looks down).
        struct ArenaEntry
        {
            std::string Name;
            float Pitch = 0;
            std::vector<Record> Records;

            bool operator==(const ArenaEntry&) const = default;
        };

        std::shared_ptr<Formats::Palette> Palette = std::make_shared<Formats::Palette>();
        // Index in Arenas of the starting arena.
        int StartArena = 0;
        // MDK coordinates; the angle in degrees (90 faces +Y).
        Vector3 StartPosition;
        float StartAngle = 0;

        // Sky panorama: the first SkyWrapWidth columns cover 360 degrees.
        std::shared_ptr<Texture> Sky = std::make_shared<Texture>();
        int SkyWrapWidth = 0;
        int SkyHorizonRow = 0;
        int SkyOffset = 0;
        int SkyTopColor = 0;
        int SkyBottomColor = 0;
        // The panorama mirrors show: the second one in levels 5 and 6, else the sky.
        std::shared_ptr<Texture> MirrorSky = std::make_shared<Texture>();
        // RGBA8 of GLASS1-GLASS4.
        std::vector<std::vector<uint>> Glass;
        std::vector<ArenaEntry> Arenas;

        static std::shared_ptr<Dti> Load(const std::string& path);

    private:
        static constexpr int BlockTable = 4 + 0x10;
        // Panorama rows hold 4 extra pixels for wrapping.
        static constexpr int SkyWrapExtra = 4;

        SharedBytes _bytes = Share({});

        int Block(int index) const;

        void Parse();

        std::shared_ptr<Texture> Panorama(int offset, int height) const;

        std::vector<Record> ParseRecords(int offset) const;
    };
}
