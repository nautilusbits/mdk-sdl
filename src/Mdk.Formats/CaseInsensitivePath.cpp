#include "Mdk.Formats/CaseInsensitivePath.h"

#include <filesystem>
#include <optional>
#include <system_error>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Files.h"

namespace Mdk::Formats
{
    namespace
    {
        constexpr std::string_view Separators = "/\\";
    }

    std::string CaseInsensitivePath::Resolve(const std::string& dir, const std::string& relative)
    {
        auto path = dir;
        for (auto& part : Str::Split(relative, Separators, Str::SplitOptions::RemoveEmpty))
        {
            path = Path::Combine(path, Match(path, part));
        }

        return path;
    }

    std::string CaseInsensitivePath::Match(const std::string& dir, const std::string& name)
    {
        if (!Directory::Exists(dir))
        {
            return name;
        }

        std::optional<std::string> other;
        std::error_code error;
        for (auto& entry : std::filesystem::directory_iterator(std::filesystem::path(std::u8string(dir.begin(), dir.end())), error))
        {
            auto entryName = entry.path().filename().u8string();
            std::string entryText(entryName.begin(), entryName.end());
            if (entryText == name)
            {
                return name;
            }

            if (!other && Str::EqualsIgnoreCase(entryText, name))
            {
                other = entryText;
            }
        }

        return other.value_or(name);
    }
}
