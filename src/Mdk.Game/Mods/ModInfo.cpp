#include "Mdk.Game/Mods/ModInfo.h"

#include <format>
#include <map>

#include "Mdk.Core/Console.h"
#include "Mdk.Core/IoErrors.h"
#include "Mdk.Game/Mods/ModCatalog.h"
#include "Mdk.Game/Mods/ModFiles.h"

namespace Mdk::Game::Mods
{
    namespace
    {
        // values.GetValueOrDefault(key, fallback).
        std::string ValueOr(const std::map<std::string, std::string, FolderLess>& values, const std::string& key, const std::string& fallback)
        {
            const auto found = values.find(key);
            return found != values.end() ? found->second : fallback;
        }
    }

    ModInfo ModInfo::Load(ModFiles& files)
    {
        try
        {
            return Parse(files.Name, files.Has(FileName) ? Text(files.Read(FileName)) : "");
        }
        catch (const std::exception& e)
        {
            if (!IsReadError(e))
            {
                throw;
            }

            Console::Error().WriteLine(std::format("Mod {}: {}", files.Name, e.what()));
            return Parse(files.Name, "");
        }
    }

    std::string ModInfo::Text(std::span<const byte> bytes)
    {
        // UTF-8, its BOM dropped (StreamReader's default).
        constexpr byte Bom[] = {0xEF, 0xBB, 0xBF};
        size_t start = 0;
        if (bytes.size() >= std::size(Bom) && bytes[0] == Bom[0] && bytes[1] == Bom[1] && bytes[2] == Bom[2])
        {
            start = std::size(Bom);
        }

        return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(start), bytes.end());
    }

    ModInfo ModInfo::Parse(const std::string& folder, const std::string& text)
    {
        std::map<std::string, std::string, FolderLess> values;
        for (const auto& line : Str::Split(text, '\n'))
        {
            const auto parts = Str::SplitCount(Str::Trim(line), '=', 2);
            if (parts.size() != 2 || Str::StartsWith(parts[0], std::string_view(&Comment, 1)))
            {
                continue;
            }

            values[Str::Trim(parts[0])] = Str::Trim(parts[1]);
        }

        int p = 0;
        const auto found = values.find("priority");
        const auto priority = found != values.end() && Str::TryParseInt(found->second, p) ? p : 0;
        const auto name = ValueOr(values, "name", "");
        return ModInfo{folder, !name.empty() ? name : folder, ValueOr(values, "author", ""),
            ValueOr(values, "version", ""), ValueOr(values, "description", ""), priority};
    }

    std::string ModInfo::Format() const
    {
        return std::format("name={}\nauthor={}\nversion={}\ndescription={}\npriority={}\n", Name, Author, Version, Description, Priority);
    }
}
