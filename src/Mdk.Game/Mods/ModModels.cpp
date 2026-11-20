#include "Mdk.Game/Mods/ModModels.h"

#include <algorithm>
#include <format>
#include <unordered_set>

#include "Mdk.Core/Console.h"
#include "Mdk.Core/Files.h"
#include "Mdk.Core/IoErrors.h"
#include "Mdk.Formats/Glb.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Game/Mods/ModImages.h"
#include "Mdk.Game/Mods/ModelSwap.h"

namespace Mdk::Game::Mods
{
    std::shared_ptr<ModModels> ModModels::Load(const std::vector<std::shared_ptr<ModFiles>>& mods, int level,
        const std::vector<std::shared_ptr<Formats::Model>>& models)
    {
        auto loaded = std::make_shared<ModModels>();
        std::vector<std::map<std::string, ModFile, FolderLess>> files;
        for (const auto& m : mods)
        {
            files.push_back(Files(m, level));
        }

        // models.Distinct(): by reference, first seen first.
        std::unordered_set<const Formats::Model*> seen;
        for (const auto& model : models)
        {
            if (!seen.insert(model.get()).second)
            {
                continue;
            }

            const ModFile* path = nullptr;
            for (const auto& f : files)
            {
                const auto found = f.find(model->Name);
                if (found != f.end())
                {
                    path = &found->second;
                    break;
                }
            }

            if (path == nullptr)
            {
                continue;
            }

            auto swap = Read(*path, *model);
            if (swap == nullptr)
            {
                continue;
            }

            loaded->_swaps[model.get()] = std::move(swap);
            if (std::find(loaded->_found.begin(), loaded->_found.end(), model->Name) == loaded->_found.end())
            {
                loaded->_found.push_back(model->Name);
            }
        }

        return loaded;
    }

    std::shared_ptr<ModelSwap> ModModels::Of(const Formats::Model& model) const
    {
        const auto found = _swaps.find(&model);
        return found != _swaps.end() ? found->second : nullptr;
    }

    std::map<std::string, ModFile, FolderLess> ModModels::Files(const std::shared_ptr<ModFiles>& mod, int level)
    {
        std::map<std::string, ModFile, FolderLess> files;
        std::vector<std::string> all;
        for (const auto& f : mod->List(FolderName))
        {
            if (Str::EndsWithIgnoreCase(f, Extension))
            {
                all.push_back(f);
            }
        }

        const std::string directories[] = {std::string(FolderName) + Separator + ModImages::LevelFolder(level), FolderName};
        for (const auto& directory : directories)
        {
            // Directly in that folder: "models/LEVEL3/XG.glb", then "models/XG.glb".
            for (const auto& path : all)
            {
                if (!Str::EqualsIgnoreCase(path.substr(0, static_cast<size_t>(Str::LastIndexOf(path, Separator))), directory))
                {
                    continue;
                }

                files.try_emplace(Path::GetFileNameWithoutExtension(path), ModFile{mod, path});
            }
        }

        return files;
    }

    std::shared_ptr<ModelSwap> ModModels::Read(const ModFile& path, const Formats::Model& model)
    {
        try
        {
            std::vector<std::string> unknown;
            auto swap = ModelSwap::Of(Formats::Glb::Read(path.Read()), model, unknown);
            if (!unknown.empty())
            {
                Console::Error().WriteLine(std::format("Mod model {}: no part {} in {}", path.ToString(), Str::Join(", ", unknown), model.Name));
            }

            return swap;
        }
        catch (const std::exception& e)
        {
            if (!IsReadError(e))
            {
                throw;
            }

            Console::Error().WriteLine(std::format("Mod model {}: {}", path.ToString(), e.what()));
            return nullptr;
        }
    }
}
