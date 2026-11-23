#pragma once

#include <memory>
#include <string>

namespace Mdk::Formats
{
    class Cmi;
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
    // The game's assets as a mod's files, for modders to start from (--export-assets): read from the
    // user's game files, written where asked, in the layout and names the loader reads (copy what you
    // change into mods/<name>/). Each level's own, through its palettes (the first arena's that shows
    // it); the menus' 2D images once.
    //   textures/LEVELn/NAME.png   (animated: NAME_0.png, NAME_1.png...; Kurt: K_RUN_0.png...)
    //   images/LEVELn/NAME.png     the level's HUD        images/NAME.png   menus, fonts
    //   models/LEVELn/NAME.glb     parts as named nodes, rest pose, textures embedded
    struct AssetExport
    {
        static std::string Run(const std::shared_ptr<Formats::MdkData>& data, const std::string& folder);

        // A texture through a palette as PNG: one file, or one per frame (index 0 clear unless zeroOpaque).
        static void WriteTexture(const std::string& folder, const std::string& name, const Formats::Texture& texture,
            const Formats::Palette& palette, bool zeroOpaque = false);

    private:
        // The level's models (the scripts' and the arenas'), each name once, through the first
        // arena's look (the scripts' through the level's palette).
        static int WriteModels(const std::string& folder, Level::LevelData& level, Formats::Cmi& cmi);
    };
}
