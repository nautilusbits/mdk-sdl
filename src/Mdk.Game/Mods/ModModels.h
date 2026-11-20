#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Mdk.Game/Mods/ModCatalog.h"
#include "Mdk.Game/Mods/ModFiles.h"

namespace Mdk::Formats
{
    class Model;
}

namespace Mdk::Game::Mods
{
    class ModelSwap;

    // The enabled mods' model replacements for a level (ModelSwap): per model, the first mod's
    // models/LEVELn/NAME.glb, else its models/NAME.glb (names ignore case). Read at the level's load;
    // a broken file keeps the original (and says why).
    class ModModels
    {
    public:
        static constexpr const char* FolderName = "models";

        // The models replaced, by name.
        const std::vector<std::string>& Found() const { return _found; }

        static std::shared_ptr<ModModels> Load(const std::vector<std::shared_ptr<ModFiles>>& mods, int level,
            const std::vector<std::shared_ptr<Formats::Model>>& models);

        std::shared_ptr<ModelSwap> Of(const Formats::Model& model) const;

    private:
        static constexpr const char* Extension = ".glb";
        static constexpr char Separator = '/';

        // Keyed by the model (C#: by reference).
        std::unordered_map<const Formats::Model*, std::shared_ptr<ModelSwap>> _swaps;
        std::vector<std::string> _found;

        // A mod's models by name: the level's folder first.
        static std::map<std::string, ModFile, FolderLess> Files(const std::shared_ptr<ModFiles>& mod, int level);

        static std::shared_ptr<ModelSwap> Read(const ModFile& path, const Formats::Model& model);
    };
}
