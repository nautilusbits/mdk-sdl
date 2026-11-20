#include "Mdk.Formats/Mto.h"

#include <algorithm>
#include <stdexcept>

#include "Mdk.Core/Files.h"

namespace Mdk::Formats
{
    Mto::Mto(SharedBytes bytes) : _bytes(std::move(bytes))
    {
        BinReader r(*_bytes, DirectoryOffset);
        auto count = r.U32();
        for (uint i = 0; i < count; i++)
        {
            auto name = r.Name(8);
            ArenaOffsets.emplace_back(name, static_cast<int>(r.U32()));
        }
    }

    std::shared_ptr<Mto> Mto::Load(const std::string& path) { return std::shared_ptr<Mto>(new Mto(Share(File::ReadAllBytes(path)))); }

    void Mto::Add(const std::shared_ptr<Arena>& arena)
    {
        ArenaOffsets.emplace_back(arena->Name, 0);
        _arenas[arena->Name] = arena;
    }

    std::vector<std::string> Mto::ArenaNames() const
    {
        std::vector<std::string> names;
        for (auto& entry : ArenaOffsets)
        {
            names.push_back(entry.first);
        }

        return names;
    }

    bool Mto::Has(const std::string& name) const
    {
        for (auto& entry : ArenaOffsets)
        {
            if (entry.first == name)
            {
                return true;
            }
        }

        return false;
    }

    std::shared_ptr<Arena> Mto::GetArena(const std::string& name)
    {
        auto found = _arenas.find(name);
        if (found != _arenas.end())
        {
            return found->second;
        }

        auto entry = std::find_if(ArenaOffsets.begin(), ArenaOffsets.end(), [&](const auto& e) { return e.first == name; });
        if (entry == ArenaOffsets.end())
        {
            throw InvalidOperationException("Sequence contains no matching element");
        }

        return _arenas[name] = Arena::Parse(name, _bytes, entry->second);
    }
}
