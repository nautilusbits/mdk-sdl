#pragma once

#include <string>
#include <vector>

namespace Mdk::Game::DevTools
{
    // A console line: commands split by ';', words by spaces.
    //   "tp ARENA_1; god"  ──►  [tp, ARENA_1], [god]
    struct CommandText
    {
        static std::vector<std::vector<std::string>> Parse(const std::string& line);

    private:
        static constexpr char CommandSeparator = ';';
    };
}
