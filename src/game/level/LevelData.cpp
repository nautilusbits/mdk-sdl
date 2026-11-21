#include "game/level/LevelData.h"

#include <format>

#include "core/Console.h"
#include "formats/Fti.h"
#include "formats/Sni.h"

namespace Mdk::Game::Level
{
    LevelData::LevelData(const Formats::MdkData& data, int number)
    {
        Number = number;
        auto dir = std::format("TRAVERSE/LEVEL{}/LEVEL{}", number, number);
        Dti = Formats::Dti::Load(data.PathOf(dir + ".DTI"));

        // Colours 0-63 stay the system's (0x41ba68 copies only the DTI's 64-255): LEVEL8's DTI has
        // magenta and purple at 10-12, for its aliens' glow, its shots and the grenade's icon.
        auto system = Formats::Fti::Load(data.PathOf(SystemFile))->GetBytes(SystemPalette);
        Dti->Palette = Dti->Palette->WithSystemColors(system);

        Mto = Formats::Mto::Load(data.PathOf(dir + "O.MTO"));
        LevelTextures = Formats::TextureArchive::Load(data.PathOf(dir + "S.MTI"), Formats::TextureArchive::Zero::ByKind);

        for (const auto& name : Mto->ArenaNames())
        {
            Arenas.push_back(Mto->GetArena(name));
        }

        auto overlays = Formats::Sni::Load(data.PathOf(dir + "O.SNI"));
        for (const auto& [name, entry] : overlays->Entries)
        {
            if (overlays->IsSound(entry))
            {
                continue;
            }

            // Empty corridors are a placeholder quad of the NONE material.
            auto corridor = Formats::Arena::ParseWorld(name, *overlays->Bytes(), entry.Offset);
            if (corridor->Materials.size() == 1 && corridor->Materials[0] == "NONE")
            {
                continue;
            }

            Arenas.push_back(corridor);
        }
    }

    LevelData::LevelData(const Formats::BetaDemo& beta, int number)
    {
        Number = number;
        auto level = Formats::BetaDemo::LevelOf(number);
        Dti = beta.LoadDti(level);
        Mto = beta.LoadMto(level);
        LevelTextures = beta.LoadTextures(level);
        for (const auto& name : Mto->ArenaNames())
        {
            Arenas.push_back(Mto->GetArena(name));
        }

        for (const auto& corridor : beta.LoadCorridors(level))
        {
            Arenas.push_back(corridor);
        }

        for (const auto& arena : Arenas)
        {
            if (Formats::BetaDemo::CheckNodes(*arena) == 0)
            {
                continue;
            }

            Console::WriteLine(std::format("Warning: {}: {} BSP nodes don't hold their triangles", arena->Name, Formats::BetaDemo::CheckNodes(*arena)));
        }
    }

    std::shared_ptr<Formats::Palette> LevelData::PaletteOf(const Formats::Arena& arena)
    {
        // One per arena: the GPU palette and the enhanced look's colour textures are made once for it.
        auto it = _palettes.find(&arena);
        if (it != _palettes.end())
        {
            return it->second;
        }

        const auto* source = &arena;
        std::shared_ptr<Formats::Arena> owned;
        if (source->PaletteRgb.empty())
        {
            auto owner = arena.Name.substr(1);
            owned = Mto->Has(owner) ? Mto->GetArena(owner) : Arenas[0];
            source = owned.get();
        }

        return _palettes[&arena] = Dti->Palette->WithArenaColors(source->PaletteRgb);
    }

    std::vector<std::shared_ptr<Formats::TextureArchive>> LevelData::ArchivesOf(const Formats::Arena& arena) const
    {
        std::vector<std::shared_ptr<Formats::TextureArchive>> archives{arena.Textures, LevelTextures};
        for (const auto& a : Arenas)
        {
            archives.push_back(a->Textures);
        }

        return archives;
    }

    std::shared_ptr<Formats::Arena> LevelData::ArenaNamed(const std::string& name) const
    {
        for (const auto& arena : Arenas)
        {
            if (arena->Name == name)
            {
                return arena;
            }
        }

        return nullptr;
    }

    const Formats::Dti::ArenaEntry* LevelData::EntryNamed(const std::string& name) const
    {
        for (const auto& entry : Dti->Arenas)
        {
            if (entry.Name == name)
            {
                return &entry;
            }
        }

        return nullptr;
    }

    bool LevelData::IsReachable(const std::string& arenaName) const
    {
        auto index = -1;
        for (size_t i = 0; i < Dti->Arenas.size(); i++)
        {
            if (Dti->Arenas[i].Name == arenaName)
            {
                index = static_cast<int>(i);
                break;
            }
        }

        if (index < 0 || index == Dti->StartArena)
        {
            return true;
        }

        for (const auto& record : Dti->Arenas[index].Records)
        {
            if (record.Type == Connection)
            {
                return true;
            }
        }

        return false;
    }
}
