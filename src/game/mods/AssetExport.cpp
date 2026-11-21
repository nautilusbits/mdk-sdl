#include "game/mods/AssetExport.h"

#include <format>
#include <unordered_set>
#include <vector>

#include "core/Console.h"
#include "core/Files.h"
#include "formats/Arena.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "formats/Glb.h"
#include "formats/MdkData.h"
#include "formats/Model.h"
#include "formats/Palette.h"
#include "formats/Png.h"
#include "formats/Texture.h"
#include "formats/TextureArchive.h"
#include "game/audio/SoundBank.h"
#include "game/hd_textures/HdGenerator.h"
#include "game/hd_textures/TextureExport.h"
#include "game/level/LevelData.h"
#include "game/mods/CanvasExport.h"
#include "game/mods/ModImages.h"
#include "game/mods/ModModels.h"
#include "game/mods/ModelExport.h"

namespace Mdk::Game::Mods
{
    using HdTextures::HdSource;

    namespace
    {
        // sources.DistinctBy(s => s.Name): the first of each name, in order.
        std::vector<HdSource> DistinctByName(const std::vector<HdSource>& sources)
        {
            std::unordered_set<std::string> names;
            std::vector<HdSource> distinct;
            for (const auto& s : sources)
            {
                if (names.insert(s.Name).second)
                {
                    distinct.push_back(s);
                }
            }

            return distinct;
        }

        // A model to write: through a palette and archives.
        struct ModelLook
        {
            std::shared_ptr<Formats::Model> Model;
            std::shared_ptr<Formats::Palette> Palette;
            std::vector<std::shared_ptr<Formats::TextureArchive>> Archives;
        };
    }

    std::string AssetExport::Run(const std::shared_ptr<Formats::MdkData>& data, const std::string& folder)
    {
        int textures = 0, images = 0, models = 0;
        const auto sprites = Formats::Bni::Load(data->PathOf("TRAVERSE/TRAVSPRT.BNI"));
        for (const auto number : HdTextures::HdOptions::AllLevels)
        {
            Level::LevelData level(*data, number);
            const auto cmi = Formats::Cmi::Load(data->PathOf(std::format("TRAVERSE/LEVEL{}/LEVEL{}.CMI", number, number)));
            const auto levelFolder = ModImages::LevelFolder(number);

            // Each name once: the first arena's palette.
            auto all = HdTextures::TextureExport::Collect(level, *cmi);
            const auto bank = Audio::SoundBank::ForLevel(*data, number);
            for (auto& kurt : HdTextures::TextureExport::Kurt(level, *sprites, [&](const std::string& name) { return bank->Animation(name); }))
            {
                all.push_back(std::move(kurt));
            }

            const auto textureFolder = Path::Combine(folder, ModImages::FolderOf(ModImages::Kind::Textures), levelFolder);
            for (const auto& source : DistinctByName(all))
            {
                WriteTexture(textureFolder, source.Name, *source.Texture, *source.Palette);
                textures++;
            }

            const auto imageFolder = Path::Combine(folder, ModImages::FolderOf(ModImages::Kind::Images), levelFolder);
            for (const auto& source : DistinctByName(CanvasExport::Level(level, *sprites)))
            {
                WriteTexture(imageFolder, source.Name, *source.Texture, *source.Palette);
                images++;
            }

            const auto modelFolder = Path::Combine(folder, ModModels::FolderName, levelFolder);
            models += WriteModels(modelFolder, level, *cmi);
            Console::WriteLine(std::format("LEVEL{}: {} textures, {} images, {} models so far", number, textures, images, models));
        }

        for (const auto& source : DistinctByName(CanvasExport::Menus(data, CanvasExport::Fonts::Listed)))
        {
            WriteTexture(Path::Combine(folder, ModImages::FolderOf(ModImages::Kind::Images)), source.Name, *source.Texture, *source.Palette);
            images++;
        }

        return std::format("{} textures, {} images, {} models", textures, images, models);
    }

    void AssetExport::WriteTexture(const std::string& folder, const std::string& name, const Formats::Texture& texture,
        const Formats::Palette& palette)
    {
        Directory::CreateDirectory(folder);
        for (int f = 0; f < texture.FrameCount; f++)
        {
            const auto file = texture.FrameCount == 1 ? std::format("{}.png", name) : std::format("{}_{}.png", name, f);
            const auto rgba = HdTextures::TextureExport::Frame(texture, palette, f);
            File::WriteAllBytes(Path::Combine(folder, file), Formats::Png::Encode(texture.Width, texture.Height, rgba, Formats::Png::Channels::Rgba));
        }
    }

    int AssetExport::WriteModels(const std::string& folder, Level::LevelData& level, Formats::Cmi& cmi)
    {
        Directory::CreateDirectory(folder);
        std::unordered_set<std::string> written;
        std::vector<ModelLook> looks;
        for (const auto& [name, offset] : cmi.ModelOffsets)
        {
            if (auto model = cmi.GetModel(name))
            {
                looks.push_back(ModelLook{std::move(model), level.Dti->Palette, {level.LevelTextures}});
            }
        }

        for (const auto& a : level.Arenas)
        {
            for (const auto& [name, m] : a->Models)
            {
                looks.push_back(ModelLook{m, level.PaletteOf(*a), level.ArchivesOf(*a)});
            }
        }

        for (const auto& [model, palette, archives] : looks)
        {
            // HashSet<string>(StringComparer.OrdinalIgnoreCase): the upper-cased name as key.
            if (!written.insert(Str::ToUpper(model->Name)).second)
            {
                continue;
            }

            const auto bytes = Formats::Glb::Write(ModelExport::Scene(*model, *palette, archives));
            File::WriteAllBytes(Path::Combine(folder, std::format("{}.glb", model->Name)), bytes);
        }

        return static_cast<int>(written.size());
    }
}
