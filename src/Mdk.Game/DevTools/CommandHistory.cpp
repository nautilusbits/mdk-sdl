#include "Mdk.Game/DevTools/CommandHistory.h"

#include <algorithm>

namespace Mdk::Game::DevTools
{
    void CommandHistory::Add(const std::string& line)
    {
        if (_lines.empty() || _lines.back() != line)
        {
            _lines.push_back(line);
        }

        if (static_cast<int>(_lines.size()) > MaxLines)
        {
            _lines.erase(_lines.begin());
        }

        _cursor = static_cast<int>(_lines.size());
    }

    std::string CommandHistory::Previous()
    {
        _cursor = std::max(_cursor - 1, 0);
        return Current();
    }

    std::string CommandHistory::Next()
    {
        _cursor = std::min(_cursor + 1, static_cast<int>(_lines.size()));
        return Current();
    }
}
