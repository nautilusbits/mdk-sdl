#include "game/hd_textures/TextureExport.h"

#include <algorithm>
#include <unordered_set>

#include "core/Dictionary.h"
#include "engine/render/ColourMips.h"
#include "formats/Arena.h"
#include "formats/Bni.h"
#include "formats/Cmi.h"
#include "formats/Model.h"
#include "formats/Palette.h"
#include "formats/SpriteAnimation.h"
#include "formats/Texture.h"
#include "formats/TextureArchive.h"
#include "game/hd_textures/HdKey.h"
#include "game/kurt/KurtSprite.h"
#include "game/level/LevelData.h"

namespace Mdk::Game::HdTextures
{
    std::vector<HdSource> TextureExport::Collect(Level::LevelData& level, Formats::Cmi& cmi)
    {
        Dictionary<std::string, HdSource> sources;
        std::vector<std::shared_ptr<Formats::Model>> levelModels;
        for (const auto& [name, offset] : cmi.ModelOffsets)
        {
            if (auto model = cmi.GetModel(name))
            {
                levelModels.push_back(std::move(model));
            }
        }

        for (const auto& arena : level.Arenas)
        {
            const auto palette = level.PaletteOf(*arena);
            const auto archives = level.ArchivesOf(*arena);
            std::vector<std::string> names(arena->Materials.begin(), arena->Materials.end());
            for (const auto& [modelName, model] : arena->Models)
            {
                names.insert(names.end(), model->Materials.begin(), model->Materials.end());
            }

            for (const auto& model : levelModels)
            {
                names.insert(names.end(), model->Materials.begin(), model->Materials.end());
            }

            // names.Distinct(): first seen first.
            std::unordered_set<std::string> seen;
            for (const auto& name : names)
            {
                if (!seen.insert(name).second)
                {
                    continue;
                }

                const auto texture = Find(name, archives);
                if (texture == nullptr)
                {
                    continue;
                }

                const auto key = HdKey::Of(*texture, *palette);
                sources.TryAdd(key, HdSource{level.Number, name, key, texture, palette});
            }
        }

        std::vector<HdSource> collected;
        for (const auto& [key, source] : sources)
        {
            collected.push_back(source);
        }

        return collected;
    }

    std::vector<HdSource> TextureExport::Kurt(Level::LevelData& level, Formats::Bni& sprites,
        const std::function<std::shared_ptr<Formats::SpriteAnimation>(const std::string&)>& levelAnimation)
    {
        Dictionary<std::string, HdSource> sources;
        const auto palette = level.Dti->Palette;
        for (const auto& name : Kurt::KurtSprite::Drawn())
        {
            const auto& levelAnimations = Kurt::KurtSprite::LevelAnimations();
            const auto own = std::find(levelAnimations.begin(), levelAnimations.end(), name) != levelAnimations.end() ? levelAnimation(name) : nullptr;
            const auto animation = own != nullptr ? own : (sprites.Has(name) ? sprites.GetAnimation(name) : nullptr);
            const auto frames = animation != nullptr ? animation->FrameCount() : 0;
            for (int frame = 0; frame < frames; frame++)
            {
                const auto texture = animation->GetFrame(frame).Image;
                const auto key = HdKey::Of(*texture, *palette);
                sources.TryAdd(key, HdSource{level.Number, Kurt::KurtSprite::FrameName(name, frame), key, texture, palette, HdAlpha::Soft});
            }
        }

        std::vector<HdSource> collected;
        for (const auto& [key, source] : sources)
        {
            collected.push_back(source);
        }

        return collected;
    }

    std::shared_ptr<Formats::Texture> TextureExport::Find(const std::string& name,
        const std::vector<std::shared_ptr<Formats::TextureArchive>>& archives)
    {
        for (const auto& archive : archives)
        {
            if (const auto* texture = archive->Textures.Find(name))
            {
                return *texture;
            }
        }

        return nullptr;
    }

    std::vector<byte> TextureExport::Frame(const Formats::Texture& texture, const Formats::Palette& palette, int frame)
    {
        const auto size = static_cast<size_t>(texture.Width * texture.Height);
        return Engine::Render::ColourMips::Expand(std::span<const byte>(texture.Indices).subspan(static_cast<size_t>(frame) * size, size), palette.Rgba);
    }
}
