#include "Mdk.Game/DevTools/CommandText.h"

#include "Mdk.Core/DotNet.h"

namespace Mdk::Game::DevTools
{
    std::vector<std::vector<std::string>> CommandText::Parse(const std::string& line)
    {
        std::vector<std::vector<std::string>> commands;
        for (const auto& command : Str::Split(line, CommandSeparator))
        {
            auto words = Str::Split(command, ' ', Str::SplitOptions::TrimRemoveEmpty);
            if (words.empty())
            {
                continue;
            }

            commands.push_back(std::move(words));
        }

        return commands;
    }
}
