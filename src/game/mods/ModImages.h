#pragma once

#include <map>
#include <memory>
#include <span>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/DotNet.h"
#include "formats/Png.h"
#include "game/hd_textures/HdImage.h"
#include "game/mods/ModCatalog.h"
#include "game/mods/ModFiles.h"

namespace Mdk::Formats
{
    class Palette;
    class Texture;
}

namespace Mdk::Game::Mods
{
    // The enabled mods' images for one level (or the menus: no level): a texture's or a 2D
    // image's replacement, of any size, premultiplied for the renderer (HdImage).
    // Mods are searched in order; within one, the exact view first (NAME@key: the texture
    // through that palette, HdKey; any subfolder), then the level's folder, then all
    // levels'. Names ignore case. Animated textures: one strip of the frames stacked downwards, or one
    // image per frame (NAME_0, NAME_1...), all of one size. The level's folders are
    // decoded at opening, on every core; the rest when asked.
    //   mods/m/textures/WALL@9f3a....png   exact view (HD textures)
    //   mods/m/textures/LEVEL3/WALL.png    LEVEL3 only
    //   mods/m/textures/WALL.png           every level
    //   mods/m/images/SC_STAT.png          a 2D image (HUD, menus)
    class ModImages
    {
    public:
        // What a folder of a mod holds.
        enum class Kind { Textures, Images };

        // The names replaced so far (each once).
        const std::vector<std::string>& Found() const { return _found; }

        // The images of `mods` (first wins) for a level (null: no level's folder).
        static std::shared_ptr<ModImages> Open(const std::vector<std::shared_ptr<ModFiles>>& mods, std::optional<int> level);

        static std::string FolderOf(Kind kind) { return kind == Kind::Textures ? "textures" : "images"; }

        // The level's folder name (LEVEL3).
        static std::string LevelFolder(int level) { return "LEVEL" + std::to_string(level); }

        // Decodes, on every core, the files first in line to replace these single-frame
        // textures (as Texture would look them up), so that their lookups find them.
        void Prefetch(const std::vector<std::pair<std::string, std::shared_ptr<Formats::Texture>>>& textures,
            const Formats::Palette& palette);

        // Forgets the decoded files (a level's load is done: the renderer holds what it was
        // given until it's on the GPU, then drops it). A later lookup decodes its file again.
        void Trim();

        // A 3D texture's replacement (arenas, models, Kurt's frames), or null.
        std::optional<HdTextures::HdImage> Texture(const std::string& name, const Formats::Texture& texture,
            const Formats::Palette& palette)
        {
            return Find(Kind::Textures, name, texture, palette);
        }

        // A 2D image's replacement (HUD, menus, fonts), or null.
        std::optional<HdTextures::HdImage> Image(const std::string& name, const Formats::Texture& texture,
            const Formats::Palette& palette)
        {
            return Find(Kind::Images, name, texture, palette);
        }

        // Straight alpha to premultiplied, in place (the renderer's colour textures).
        static void Premultiply(std::span<byte> rgba);

    private:
        static constexpr int Channels = 4;
        static constexpr int Alpha = 3;
        static constexpr const char* PngExtension = ".png";
        static constexpr char KeyMark = '@';
        static constexpr char FrameMark = '_';
        static constexpr char Separator = '/';

        using FilesByName = std::map<std::string, ModFile, FolderLess>;

        // One kind of a mod's files: by key, in the level's folder, for all levels.
        struct Index
        {
            FilesByName Keys;
            FilesByName Level;
            FilesByName Names;
        };

        std::vector<std::unordered_map<Kind, std::shared_ptr<Index>>> _mods;
        // Decoded files (premultiplied), null when broken.
        std::unordered_map<ModFile, std::shared_ptr<const Formats::Png::Image>, ModFileHash> _decoded;
        std::vector<std::string> _found;

        ModImages() = default;

        static std::shared_ptr<Index> Scan(const std::shared_ptr<ModFiles>& mod, const std::string& folder,
            std::optional<int> level, std::vector<ModFile>& levelFiles);

        void Decode(const std::vector<ModFile>& paths);

        // A file decoded and premultiplied, or null (broken).
        static std::shared_ptr<const Formats::Png::Image> Load(const ModFile& path);

        std::shared_ptr<const Formats::Png::Image> Decoded(const ModFile& path);

        // A texture's first file to try (the exact view, the level's, all levels').
        std::optional<ModFile> FirstFile(const std::string& name, const Formats::Texture& texture, const Formats::Palette& palette);

        std::optional<HdTextures::HdImage> Find(Kind kind, const std::string& name, const Formats::Texture& texture,
            const Formats::Palette& palette);

        HdTextures::HdImage Report(const std::string& name, HdTextures::HdImage image);

        // A file of the name (a strip of the frames), else one per frame.
        std::optional<HdTextures::HdImage> Frames(const FilesByName& files, const std::string& name, int frames);

        // One file of all the frames stacked downwards.
        std::optional<HdTextures::HdImage> Strip(const ModFile& path, int frames);
    };
}
