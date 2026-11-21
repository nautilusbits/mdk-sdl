#pragma once

// The command line's options as the game's, shared by the desktop and the Android bench:
//   {"--level=7", "--fire"} ─Parse─► {level: "7", fire: ""} ─Options─► GameOptions

#include <optional>
#include <string>
#include <vector>

#include "core/Dictionary.h"
#include "game/flow/Game.h"

namespace Mdk::Game::Flow
{
    struct CommandLine
    {
        // "--name=value" or "--name" (value ""); other arguments are skipped.
        static Dictionary<std::string, std::string> Parse(const std::vector<std::string>& args);

        // The game's options; none, after printing why, when one is wrong.
        static std::optional<GameOptions> Options(const Dictionary<std::string, std::string>& options);
    };
}
