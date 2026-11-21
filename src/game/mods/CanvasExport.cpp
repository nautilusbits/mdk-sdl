#include "game/mods/CanvasExport.h"

#include <format>

#include "core/Files.h"
#include "formats/Bni.h"
#include "formats/Font.h"
#include "formats/Fti.h"
#include "formats/MdkData.h"
#include "formats/Palette.h"
#include "formats/Texture.h"
#include "game/hd_textures/HdGenerator.h"
#include "game/hd_textures/HdKey.h"
#include "game/hud/BomberOverlay.h"
#include "game/hud/FontView.h"
#include "game/hud/HudView.h"
#include "game/hud/SniperOverlay.h"
#include "game/level/LevelData.h"

namespace Mdk::Game::Mods
{
    using HdTextures::HdSource;

    std::vector<HdSource> CanvasExport::Level(Level::LevelData& level, Formats::Bni& sprites)
    {
        const auto palette = level.Dti->Palette;
        auto images = Hud::HudView::Images(sprites);
        for (auto& image : Hud::SniperOverlay::Images(sprites))
        {
            images.push_back(std::move(image));
        }

        for (auto& image : Hud::BomberOverlay::Images(sprites))
        {
            images.push_back(std::move(image));
        }

        std::vector<HdSource> sources;
        for (const auto& [name, texture] : images)
        {
            sources.push_back(Source(level.Number, name, texture, palette));
        }

        return sources;
    }

    std::vector<HdSource> CanvasExport::Menus(const std::shared_ptr<Formats::MdkData>& data, Fonts fonts)
    {
        std::vector<HdSource> sources;
        const auto options = Formats::Bni::Load(data->PathOf(Options));
        if (options->Has(Background))
        {
            const auto offset = options->Entries().At(Background).Offset;
            const auto& bytes = *options->Bytes();
            sources.push_back(Source(NoLevel, Background, Formats::Texture::Parse(Background, bytes, offset + Formats::Palette::Size * RgbSize),
                PaletteAt(bytes, offset)));
        }

        for (const auto level : HdTextures::HdOptions::AllLevels)
        {
            const auto path = data->PathOf(std::format("MISC/LOAD_{}.LBB", level));
            if (!File::Exists(path))
            {
                continue;
            }

            const auto bytes = File::ReadAllBytes(path);
            if (std::ssize(bytes) > Formats::Palette::Size * RgbSize)
            {
                sources.push_back(Source(NoLevel, std::format("LOAD_{}", level), Formats::Texture::Parse("LOAD", bytes, Formats::Palette::Size * RgbSize),
                    PaletteAt(bytes, 0)));
            }
        }

        const auto falls = Formats::Bni::Load(data->PathOf(Falls));
        for (int n = 1; n <= FallCount; n++)
        {
            const auto palette = PaletteAt(*falls->Bytes(), falls->Entries().At(std::format("{}{}", FallPalette, n)).Offset);
            for (const auto& [name, texture] : Hud::HudView::Images(*falls))
            {
                sources.push_back(Source(NoLevel, name, texture, palette));
            }
        }

        if (fonts == Fonts::Skipped)
        {
            return sources;
        }

        const auto fti = Formats::Fti::Load(data->PathOf("MISC/MDKFONT.FTI"));
        const auto system = Formats::Palette::FromRgb(fti->GetBytes(SystemPalette));
        for (const std::string name : {Hud::FontView::Big, Hud::FontView::Small})
        {
            sources.push_back(Source(NoLevel, name, Hud::FontView::Atlas(*Formats::Font::Parse(fti->GetBytes(name), 0), name), system));
        }

        return sources;
    }

    std::shared_ptr<Formats::Palette> CanvasExport::PaletteAt(std::span<const byte> bytes, int offset)
    {
        return Formats::Palette::FromRgb(bytes.subspan(static_cast<size_t>(offset), Formats::Palette::Size * RgbSize));
    }

    HdSource CanvasExport::Source(int level, const std::string& name, const std::shared_ptr<Formats::Texture>& texture,
        const std::shared_ptr<Formats::Palette>& palette)
    {
        return HdSource{level, name, HdTextures::HdKey::Of(*texture, *palette), texture, palette, HdTextures::HdAlpha::Soft, ModImages::Kind::Images};
    }
}
