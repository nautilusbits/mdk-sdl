#include "game/flow/LevelFlow.h"

#include <algorithm>

#include "game/flow/GameState.h"

namespace Mdk::Game::Flow
{
    LevelFlow::After LevelFlow::AfterLevel(int level, Scripts::ScriptRuntime::GameOver over)
    {
        auto index = GameState::IndexOf(level);
        if (over == Scripts::ScriptRuntime::GameOver::Yes || index < 0 || index + 1 >= static_cast<int>(GameState::Order.size()))
        {
            return After::Menu;
        }

        return index <= LastStatisticsIndex ? After::Statistics : After::LastLevel;
    }

    int LevelFlow::Next(int level)
    {
        auto index = std::clamp(GameState::IndexOf(level) + 1, 0, static_cast<int>(GameState::Order.size()) - 1);
        return GameState::Order[index];
    }

    std::string LevelFlow::SaveName(int level)
    {
        return std::to_string(GameState::IndexOf(level) + 1);
    }
}
