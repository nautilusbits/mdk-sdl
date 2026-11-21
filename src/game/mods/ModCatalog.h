#pragma once

#include <memory>
#include <optional>
#include <string>
#include <map>
#include <string_view>
#include <vector>

#include "core/DotNet.h"
#include "game/mods/ModInfo.h"

namespace Mdk::Game::Flow
{
    class Settings;
}

namespace Mdk::Game::Mods
{
    class ModFiles;
    class ModImages;

    // A mod switched on or off in the settings (mod.<folder>=Off).
    enum class ModState { On, Off };

    // Mods' folders: StringComparer.OrdinalIgnoreCase as a map's order (C++ takes the comparer as a type).
    struct FolderLess
    {
        using is_transparent = void;

        bool operator()(std::string_view a, std::string_view b) const { return Str::CompareOrdinalIgnoreCase(a, b) < 0; }
    };

    using FilesByFolder = std::map<std::string, std::shared_ptr<ModFiles>, FolderLess>;

    // The mods in the user folder's mods/: each subfolder or zip is one (hidden ones aren't; a
    // folder wins over a zip of its name), ordered by priority (highest first), then by folder name.
    // The first enabled mod with a file for something wins. New mods are on.
    //   mods/
    //     hd-textures/      mod.txt  textures/...           (made by "Make HD textures")
    //     clean-walls/      mod.txt  textures/LEVEL3/...  images/...  models/...
    //     clean-walls.zip   ignored: the folder wins
    //     hud.zip           mod.txt  images/...  (or hud/mod.txt  hud/images/...)
    class ModCatalog
    {
    public:
        static constexpr const char* FolderName = "mods";

        // Every mod found, in order.
        std::vector<ModInfo> Mods;

        // The mods folder in a user folder.
        static std::string FolderIn(const std::string& userFolder);

        static std::shared_ptr<ModCatalog> Scan(const std::string& folder);

        // The mods the settings leave on, in order.
        std::vector<ModInfo> Enabled(const Flow::Settings& settings) const;

        // The enabled mods' files, in order.
        std::vector<std::shared_ptr<ModFiles>> Files(const Flow::Settings& settings) const;

        // The enabled mods' images for a level (null: the menus).
        std::shared_ptr<Game::Mods::ModImages> ModImages(const Flow::Settings& settings, std::optional<int> level) const;

        // Lets go of the mods' open zips (a scan again replaces this catalogue).
        void Close();

        // Only these mods on (--mod; tests), the others off.
        void Only(Flow::Settings& settings, const std::vector<std::string>& folders);

        ModCatalog(FilesByFolder files, std::vector<ModInfo> mods)
            : Mods(std::move(mods)), _files(std::move(files)) {}

    private:
        static constexpr char Hidden = '.';

        // The mods' files by folder name.
        FilesByFolder _files;

        // The zips of the mods folder, after the folders: a folder of the name wins.
        static void AddZips(const std::string& folder, FilesByFolder& files);

        static bool Hides(const std::string& path);
    };
}
