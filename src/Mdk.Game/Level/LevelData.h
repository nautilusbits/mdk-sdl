#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Formats/Arena.h"
#include "Mdk.Formats/BetaDemo.h"
#include "Mdk.Formats/Dti.h"
#include "Mdk.Formats/MdkData.h"
#include "Mdk.Formats/Mto.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/TextureArchive.h"

namespace Mdk::Game::Level
{
    // The files of one level: settings and sky (DTI), arenas (MTO), shared textures (MTI) and
    // the corridors between arenas (in LEVELnO.SNI).
    class LevelData
    {
    public:
        // DTI record type of a link to another arena: arenas without one start hidden.
        static constexpr uint Connection = 6;

        int Number = 0;
        std::shared_ptr<Formats::Dti> Dti;
        std::shared_ptr<Formats::Mto> Mto;
        std::shared_ptr<Formats::TextureArchive> LevelTextures;
        // Arenas then corridors (a corridor CHMO_1 follows arena HMO_1).
        std::vector<std::shared_ptr<Formats::Arena>> Arenas;

        LevelData(const Formats::MdkData& data, int number);

        // A level of the 1996 demo (961, 963, 966; BetaDemo): its arenas, then its corridors.
        LevelData(const Formats::BetaDemo& beta, int number);

        // The palette an arena draws with: the level's, with the arena's colours (corridors
        // borrow their arena's: CHMO_1 from HMO_1).
        std::shared_ptr<Formats::Palette> PaletteOf(const Formats::Arena& arena);

        // Texture archives searched for an arena's materials: its own, the level's, then every
        // other arena's (some borrow, e.g. O3_* in level 6).
        std::vector<std::shared_ptr<Formats::TextureArchive>> ArchivesOf(const Formats::Arena& arena) const;

        // The arena of that name, or null (a loop: no closure per call).
        std::shared_ptr<Formats::Arena> ArenaNamed(const std::string& name) const;

        // The DTI entry of the arena of that name, or null (points into Dti).
        const Formats::Dti::ArenaEntry* EntryNamed(const std::string& name) const;

        // Whether an arena can be entered from the start: the starting one, or one with a connection.
        bool IsReachable(const std::string& arenaName) const;

    private:
        static constexpr const char* SystemFile = "MISC/MDKFONT.FTI";
        static constexpr const char* SystemPalette = "SYS_PAL";

        std::unordered_map<const Formats::Arena*, std::shared_ptr<Formats::Palette>> _palettes;
    };
}
