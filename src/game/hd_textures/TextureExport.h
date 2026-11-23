#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/DotNet.h"
#include "game/hd_textures/UpscaleImages.h"
#include "game/mods/ModImages.h"

namespace Mdk::Formats
{
    class Bni;
    class Cmi;
    class Palette;
    class SpriteAnimation;
    class Texture;
    class TextureArchive;
}

namespace Mdk::Game::Level
{
    class LevelData;
}

namespace Mdk::Game::HdTextures
{
    // A texture as a level shows it: through an arena's palette; its HD image's cover; a
    // texture or a 2D image (CanvasExport; level 0: the menus').
    struct HdSource
    {
        int Level = 0;
        std::string Name;
        std::string Key;
        std::shared_ptr<Formats::Texture> Texture;
        std::shared_ptr<Formats::Palette> Palette;
        HdAlpha Alpha = HdAlpha::Hard;
        Mods::ModImages::Kind Kind = Mods::ModImages::Kind::Textures;

        bool operator==(const HdSource&) const = default;
    };

    // The textures the enhanced look draws lit, as the game resolves them (MaterialResolver):
    // each arena's and corridor's surfaces and models through its palette, and the level's models
    // (the scripts' objects) through every arena's palette, each distinct image once (HdKey);
    // and Kurt's sprite frames through the level's palette (Kurt; cut-outs: index 0 clear).
    // Not exported: the sky, the HUD, the fonts, the 2D screens, the fall's and the stream's Kurt (the
    // original look only).
    //   arena ─► its materials, its models' ─┐
    //   level models (CMI) ──────────────────┴─► names ─► archives (arena, level, other arenas) ─► texture × palette
    struct TextureExport
    {
        static std::vector<HdSource> Collect(Level::LevelData& level, Formats::Cmi& cmi);

        // Kurt's frames as a level draws them (KurtSprite): the level's own first (`levelAnimation`:
        // the snowboard's, the slide's), else TRAVSPRT.BNI's; each distinct image once.
        static std::vector<HdSource> Kurt(Level::LevelData& level, Formats::Bni& sprites,
            const std::function<std::shared_ptr<Formats::SpriteAnimation>(const std::string&)>& levelAnimation);

        // A frame's RGBA8 as the enhanced look expands it: index 0 is clear unless zeroOpaque.
        static std::vector<byte> Frame(const Formats::Texture& texture, const Formats::Palette& palette, int frame,
            bool zeroOpaque = false);

    private:
        // The first archive's texture of that name (the resolver's order).
        static std::shared_ptr<Formats::Texture> Find(const std::string& name,
            const std::vector<std::shared_ptr<Formats::TextureArchive>>& archives);
    };
}
