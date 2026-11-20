#include "Mdk.Game/Mods/ModReport.h"

#include <format>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Game::Mods
{
    std::string ModReport::Summary() const
    {
        return std::format("mods {}: {} images, {} models", Mods, Images.size(), Models.size());
    }

    std::vector<std::string> ModReport::Lines() const
    {
        std::vector<std::string> lines{Summary()};
        if (!Images.empty())
        {
            lines.push_back("images: " + Str::Join(" ", Images));
        }

        if (!Models.empty())
        {
            lines.push_back("models: " + Str::Join(" ", Models));
        }

        return lines;
    }
}
