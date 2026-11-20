#include "Mdk.Game/Flow/Cheats.h"

#include <algorithm>
#include <cstring>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/Flow/GameState.h"
#include "Mdk.Game/Flow/Settings.h"

namespace Mdk::Game::Flow
{
    Cheats::Cheat Cheats::Type(char letter)
    {
        auto typed = _typed + letter;
        auto keep = std::max<int>(0, static_cast<int>(_typed.size()) + 1 - static_cast<int>(std::strlen(DebugCheat)));
        _typed = typed.substr(static_cast<size_t>(keep));
        if (Str::EndsWith(_typed, GoreCheat))
        {
            _typed = "";
            _settings.Gore = !_settings.Gore;
            return Cheat::Gore;
        }

        if (Str::EndsWith(_typed, DebugCheat))
        {
            _typed = "";
            _state.DebugKeys = !_state.DebugKeys;
            return Cheat::DebugKeys;
        }

        return Cheat::None;
    }
}
