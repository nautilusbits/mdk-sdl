#pragma once

#include <string>
#include <vector>

namespace Mdk::Game::Mods
{
    // What a level took from the mods: the images (textures, Kurt's frames, the HUD's) and
    // the models replaced, by name (the console's mods, the F3 overlay).
    struct ModReport
    {
        int Mods = 0;
        std::vector<std::string> Images;
        std::vector<std::string> Models;

        bool operator==(const ModReport&) const = default;

        static const ModReport& None()
        {
            static const ModReport none{0, {}, {}};
            return none;
        }

        // "mods 2: 14 images, 1 model".
        std::string Summary() const;

        // The summary, then what was replaced.
        std::vector<std::string> Lines() const;
    };
}
