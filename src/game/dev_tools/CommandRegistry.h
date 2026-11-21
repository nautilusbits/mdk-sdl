#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "game/mods/ModCatalog.h"

namespace Mdk::Game::DevTools
{
    // A console command: its name, how it's used, what it does (given its arguments, it
    // returns its output, "" for none) and its other names.
    struct Command
    {
        std::string Name;
        std::string Usage;
        std::function<std::string(const std::vector<std::string>&)> Run;
        std::optional<std::vector<std::string>> Aliases;

        std::vector<std::string> Names() const;
    };

    // The console's commands by name (any case), and completion of their names.
    class CommandRegistry
    {
    public:
        const std::vector<std::shared_ptr<Command>>& Commands() const { return _commands; }

        void Add(const Command& command);

        // Runs a line's commands in turn; returns their output lines.
        std::vector<std::string> Run(const std::string& line);

        // Completes a command's name: one match is completed (and a space added), several
        // are completed to their common start and listed. Arguments aren't completed.
        //   "he" ──► "health "        "g" ──► "g" [give, god]
        std::tuple<std::string, std::vector<std::string>> Complete(const std::string& line);

    private:
        // By name, any case (only looked up).
        std::map<std::string, std::shared_ptr<Command>, Mods::FolderLess> _byName;
        std::vector<std::shared_ptr<Command>> _commands;

        static std::string CommonStart(const std::vector<std::string>& names);
    };
}
