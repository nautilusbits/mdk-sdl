#pragma once

#include <string>
#include <vector>

namespace Mdk::Game::DevTools
{
    // The lines run in the console, oldest first; Up and Down walk them. It lasts the session.
    //   [god, pos] ◄─Previous── (new line) ──Next─► ""
    class CommandHistory
    {
    public:
        // A line was run (a repeat of the last one isn't kept twice).
        void Add(const std::string& line);

        std::string Previous();

        std::string Next();

    private:
        static constexpr int MaxLines = 100;

        std::vector<std::string> _lines;
        // The line shown; _lines.size() is the new line.
        int _cursor = 0;

        std::string Current() const { return _cursor < static_cast<int>(_lines.size()) ? _lines[_cursor] : ""; }
    };
}
