#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/HdTextures/TextureExport.h"

namespace Mdk::Formats
{
    class Bni;
    class MdkData;
    class Palette;
    class Texture;
}

namespace Mdk::Game::Level
{
    class LevelData;
}

namespace Mdk::Game::Mods
{
    // The canvas's 2D images as the screens draw them (CanvasImages), for the exports and the HD
    // textures: by their names in mods, through the palettes they're drawn with. Not listed (mods can
    // still replace them by name): the statistics' pages, the stream's HUD.
    //   a level (its palette): the HUD, sniper mode's screen, the bomber's sight
    //   the menus (level 0):   MDKOPT, LOAD_n, the falls' HUD (FALLPn), the fonts (exports only)
    struct CanvasExport
    {
        // The menus' images go to no level's folder.
        static constexpr int NoLevel = 0;

        // Whether the fonts are listed: exports yes, AI upscaling no (it blurs them).
        enum class Fonts { Listed, Skipped };

        static std::vector<HdTextures::HdSource> Level(Level::LevelData& level, Formats::Bni& sprites);

        static std::vector<HdTextures::HdSource> Menus(const std::shared_ptr<Formats::MdkData>& data, Fonts fonts);

    private:
        static constexpr int RgbSize = 3;
        static constexpr const char* Options = "MISC/OPTIONS.BNI";
        static constexpr const char* Background = "MDKOPT";
        static constexpr const char* Falls = "FALL3D/FALL3D.BNI";
        static constexpr const char* FallPalette = "FALLP";
        static constexpr int FallCount = 5;
        static constexpr const char* SystemPalette = "SYS_PAL";

        static std::shared_ptr<Formats::Palette> PaletteAt(std::span<const byte> bytes, int offset);

        // A 2D image: soft cut-outs (the canvas blends its edges).
        static HdTextures::HdSource Source(int level, const std::string& name, const std::shared_ptr<Formats::Texture>& texture,
            const std::shared_ptr<Formats::Palette>& palette);
    };
}
