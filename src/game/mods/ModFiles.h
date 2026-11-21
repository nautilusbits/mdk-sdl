#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/DotNet.h"

namespace Mdk::Game::Mods
{
    // A mod's files, by '/'-separated path from the mod's top (textures/LEVEL3/WALL.png):
    // a folder of mods/, or a zip (ZipFiles). Reading may run on every core.
    //   mods/walls/textures/WALL.png  ─┐
    //   mods/walls.zip:textures/WALL.png ─┴─► "textures/WALL.png"
    class ModFiles
    {
    public:
        virtual ~ModFiles() = default;

        // The mod's name in the settings (the folder's, the zip's without .zip).
        std::string Name;

        virtual bool Has(const std::string& path) = 0;

        // The files under a folder, at any depth.
        virtual std::vector<std::string> List(const std::string& folder) = 0;

        // A file's bytes; IOException or InvalidDataException when it can't.
        virtual std::vector<byte> Read(const std::string& path) = 0;

        // A file's place, for messages.
        virtual std::string Where(const std::string& path) = 0;

        // Lets go of open files (a later read opens them again).
        virtual void Close() {}

        static std::shared_ptr<ModFiles> Folder(const std::string& folder);

        // A zip's files, or null (not a zip, or refused: says why).
        static std::shared_ptr<ModFiles> Zip(const std::string& path);

    protected:
        explicit ModFiles(std::string name) : Name(std::move(name)) {}
    };

    // One file of a mod (a key: same mod, same path).
    struct ModFile
    {
        std::shared_ptr<ModFiles> Mod;
        std::string Path;

        bool operator==(const ModFile&) const = default;

        std::vector<byte> Read() const { return Mod->Read(Path); }

        std::string ToString() const { return Mod->Where(Path); }
    };

    // ModFile as a hash key: the mod (by reference) and the path.
    struct ModFileHash
    {
        std::size_t operator()(const ModFile& file) const
        {
            return std::hash<const void*>()(file.Mod.get()) ^ (std::hash<std::string>()(file.Path) << 1);
        }
    };

    // A mod that is a folder.
    class FolderFiles : public ModFiles
    {
    public:
        explicit FolderFiles(const std::string& folder);

        bool Has(const std::string& path) override;

        std::vector<std::string> List(const std::string& folder) override;

        std::vector<byte> Read(const std::string& path) override;

        std::string Where(const std::string& path) override;

    private:
        static constexpr char Separator = '/';

        std::string _root;
    };
}
