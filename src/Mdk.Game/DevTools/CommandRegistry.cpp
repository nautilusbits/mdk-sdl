#include "Mdk.Game/DevTools/CommandRegistry.h"

#include <algorithm>
#include <cctype>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/DevTools/CommandText.h"

namespace Mdk::Game::DevTools
{
    std::vector<std::string> Command::Names() const
    {
        std::vector<std::string> names{Name};
        if (Aliases)
        {
            names.insert(names.end(), Aliases->begin(), Aliases->end());
        }

        return names;
    }

    void CommandRegistry::Add(const Command& command)
    {
        auto shared = std::make_shared<Command>(command);
        _commands.push_back(shared);
        for (const auto& name : shared->Names())
        {
            _byName[name] = shared;
        }
    }

    std::vector<std::string> CommandRegistry::Run(const std::string& line)
    {
        std::vector<std::string> output;
        for (const auto& words : CommandText::Parse(line))
        {
            auto found = _byName.find(words[0]);
            auto text = found != _byName.end()
                ? found->second->Run(std::vector<std::string>(words.begin() + 1, words.end()))
                : std::format("Unknown command: {}", words[0]);
            auto lines = Str::Split(text, '\n', Str::SplitOptions::RemoveEmpty);
            output.insert(output.end(), lines.begin(), lines.end());
        }

        return output;
    }

    std::tuple<std::string, std::vector<std::string>> CommandRegistry::Complete(const std::string& line)
    {
        if (Str::Contains(line, " ") || line.empty())
        {
            return {line, {}};
        }

        std::vector<std::string> matches;
        for (const auto& [name, command] : _byName)
        {
            if (Str::StartsWithIgnoreCase(name, line))
            {
                matches.push_back(name);
            }
        }

        std::sort(matches.begin(), matches.end());
        switch (matches.size())
        {
            case 0:
                return {line, matches};
            case 1:
                return {matches[0] + " ", matches};
            default:
                return {CommonStart(matches), matches};
        }
    }

    std::string CommandRegistry::CommonStart(const std::vector<std::string>& names)
    {
        size_t length = names[0].size();
        for (const auto& n : names)
        {
            length = std::min(length, n.size());
        }

        size_t common = 0;
        auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
        auto allMatch = [&]
        {
            return std::all_of(names.begin(), names.end(), [&](const std::string& n) { return lower(n[common]) == lower(names[0][common]); });
        };
        while (common < length && allMatch())
        {
            common++;
        }

        return names[0].substr(0, common);
    }
}
