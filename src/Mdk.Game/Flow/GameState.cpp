#include "Mdk.Game/Flow/GameState.h"

#include <algorithm>
#include <ctime>
#include <format>

#include "Mdk.Game/Flow/LevelFlow.h"
#include "Mdk.Game/Scripts/GameStats.h"

namespace Mdk::Game::Flow
{
    namespace
    {
        // DateTime.Now.ToString("s"): 2026-11-20T05:36:00, local time.
        std::string SortableNow()
        {
            auto now = std::time(nullptr);
            std::tm local{};
#if defined(_WIN32)
            localtime_s(&local, &now);
#else
            localtime_r(&now, &local);
#endif
            return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                local.tm_hour, local.tm_min, local.tm_sec);
        }
    }

    GameState::GameState() : Stats(std::make_shared<Scripts::GameStats>())
    {
    }

    int GameState::IndexOf(int level)
    {
        auto found = std::find(Order.begin(), Order.end(), level);
        return found == Order.end() ? -1 : static_cast<int>(found - Order.begin());
    }

    void GameState::NewGame(int index)
    {
        Level = Order[index];
        Deaths = 0;
        StrikeUsed = false;
    }

    void GameState::Load(const SaveGame& save)
    {
        Level = save.Level;
        Deaths = save.Deaths;
        StrikeUsed = save.StrikeUsed;
        Snapshot = save.Kind == SaveKind::Snapshot ? save.State : std::nullopt;
    }

    void GameState::EnterLastLevel(int health)
    {
        Level = LevelFlow::Next(Level);
        Carry = FallCarry{health, {}};
    }

    SaveGame GameState::Save(SaveKind kind) const
    {
        return SaveGame{kind, Level, SaveGame::FullHealth, Deaths, StrikeUsed, SortableNow()};
    }
}
