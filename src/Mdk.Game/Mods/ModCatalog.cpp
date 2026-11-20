#include "Mdk.Game/Mods/ModCatalog.h"

#include <algorithm>
#include <format>

#include "Mdk.Core/Console.h"
#include "Mdk.Core/Files.h"
#include "Mdk.Game/Flow/Settings.h"
#include "Mdk.Game/Mods/ModFiles.h"
#include "Mdk.Game/Mods/ModImages.h"
#include "Mdk.Game/Mods/ZipFiles.h"

namespace Mdk::Game::Mods
{
    std::string ModCatalog::FolderIn(const std::string& userFolder)
    {
        return Path::Combine(userFolder, FolderName);
    }

    std::shared_ptr<ModCatalog> ModCatalog::Scan(const std::string& folder)
    {
        if (!Directory::Exists(folder))
        {
            return std::make_shared<ModCatalog>(FilesByFolder{}, std::vector<ModInfo>{});
        }

        FilesByFolder files;
        for (const auto& directory : Directory::GetDirectories(folder))
        {
            if (Hides(directory))
            {
                continue;
            }

            files.try_emplace(Path::GetFileName(directory), ModFiles::Folder(directory));
        }

        AddZips(folder, files);
        std::vector<ModInfo> mods;
        for (const auto& [name, mod] : files)
        {
            mods.push_back(ModInfo::Load(*mod));
        }

        std::stable_sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b)
        {
            if (a.Priority != b.Priority)
            {
                return a.Priority > b.Priority;
            }

            return Str::CompareOrdinalIgnoreCase(a.Folder, b.Folder) < 0;
        });
        return std::make_shared<ModCatalog>(std::move(files), std::move(mods));
    }

    void ModCatalog::AddZips(const std::string& folder, FilesByFolder& files)
    {
        std::vector<std::string> zips;
        for (const auto& f : Directory::GetFiles(folder))
        {
            if (!Hides(f) && Str::EndsWithIgnoreCase(f, ZipFiles::Extension))
            {
                zips.push_back(f);
            }
        }

        std::sort(zips.begin(), zips.end());
        for (const auto& zip : zips)
        {
            const auto name = Path::GetFileNameWithoutExtension(zip);
            if (files.contains(name))
            {
                Console::Error().WriteLine(std::format("Mod {}: the folder wins over {}", name, Path::GetFileName(zip)));
                continue;
            }

            if (auto mod = ModFiles::Zip(zip))
            {
                files[name] = std::move(mod);
            }
        }
    }

    bool ModCatalog::Hides(const std::string& path)
    {
        return Str::StartsWith(Path::GetFileName(path), std::string_view(&Hidden, 1));
    }

    std::vector<ModInfo> ModCatalog::Enabled(const Flow::Settings& settings) const
    {
        std::vector<ModInfo> enabled;
        for (const auto& m : Mods)
        {
            if (settings.StateOf(m.Folder) == ModState::On)
            {
                enabled.push_back(m);
            }
        }

        return enabled;
    }

    std::vector<std::shared_ptr<ModFiles>> ModCatalog::Files(const Flow::Settings& settings) const
    {
        std::vector<std::shared_ptr<ModFiles>> files;
        for (const auto& m : Enabled(settings))
        {
            files.push_back(_files.at(m.Folder));
        }

        return files;
    }

    std::shared_ptr<Game::Mods::ModImages> ModCatalog::ModImages(const Flow::Settings& settings, std::optional<int> level) const
    {
        return Game::Mods::ModImages::Open(Files(settings), level);
    }

    void ModCatalog::Close()
    {
        for (const auto& [name, files] : _files)
        {
            files->Close();
        }
    }

    void ModCatalog::Only(Flow::Settings& settings, const std::vector<std::string>& folders)
    {
        for (const auto& mod : Mods)
        {
            const auto on = std::any_of(folders.begin(), folders.end(), [&](const std::string& f) { return Str::EqualsIgnoreCase(f, mod.Folder); });
            settings.Mods[mod.Folder] = on ? ModState::On : ModState::Off;
        }
    }
}
