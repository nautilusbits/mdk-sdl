#include "formats/LocalPaths.h"

#include "core/DotNet.h"
#include "core/Files.h"

namespace Mdk::Formats
{
    std::string LocalPaths::Get(const std::string& key)
    {
        auto path = Path::Combine(Path::BaseDirectory(), FileName);
        return File::Exists(path) ? Parse(File::ReadAllText(path), key) : "";
    }

    std::string LocalPaths::Parse(const std::string& text, const std::string& key)
    {
        std::string section;
        for (auto& raw : Str::Split(text, '\n'))
        {
            auto line = Str::Trim(raw);
            if (line.empty() || line.front() == ';' || line.front() == '#')
            {
                continue;
            }

            if (line.front() == '[' && line.back() == ']')
            {
                section = Str::Trim(std::string_view(line).substr(1, line.size() - 2));
                continue;
            }

            auto equals = Str::IndexOf(line, '=');
            if (section != Section || equals < 0 || Str::Trim(std::string_view(line).substr(0, equals)) != key)
            {
                continue;
            }

            return Unquote(Str::Trim(std::string_view(line).substr(equals + 1)));
        }

        return "";
    }

    std::string LocalPaths::Unquote(const std::string& value)
    {
        if (value.size() < 2 || value.front() != Quote || value.back() != Quote)
        {
            return value;
        }

        return Str::Replace(Str::Replace(value.substr(1, value.size() - 2), "\\\"", "\""), "\\\\", "\\");
    }
}
