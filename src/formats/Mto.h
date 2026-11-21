#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "formats/Arena.h"
#include "formats/BinReader.h"

namespace Mdk::Formats
{
    // LEVELnO.MTO: the arenas of a level, parsed on demand.
    class Mto
    {
    public:
        // Arena name (HMO_1...) to the file offset of its block, in file order.
        std::vector<std::pair<std::string, int>> ArenaOffsets;

        // Arenas read from elsewhere (the 1996 demo's loose files, BetaDemo).
        Mto() = default;

        static std::shared_ptr<Mto> Load(const std::string& path);

        // Adds an arena read from elsewhere.
        void Add(const std::shared_ptr<Arena>& arena);

        std::vector<std::string> ArenaNames() const;

        bool Has(const std::string& name) const;

        std::shared_ptr<Arena> GetArena(const std::string& name);

    private:
        static constexpr int DirectoryOffset = 0x14;

        SharedBytes _bytes = Share({});
        std::unordered_map<std::string, std::shared_ptr<Arena>> _arenas;

        explicit Mto(SharedBytes bytes);
    };
}
