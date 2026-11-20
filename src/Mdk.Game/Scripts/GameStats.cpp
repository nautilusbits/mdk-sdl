#include "Mdk.Game/Scripts/GameStats.h"

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/Flow/GameState.h"

namespace Mdk::Game::Scripts
{
    const std::unordered_set<std::string> GameStats::EnemyTypes = {
        "XB", "XB1", "XB2", "XB3", "XBSHARK", "XBSHIP", "XBT", "XC", "XCARGO", "XD", "XD6GUN", "XE", "XEARTH", "XF",
        "XFORK", "XG", "XG_BOMB1", "XGEN", "XGHTARG", "XGSNOW", "XGSMOKE", "XG_MISS", "XGTARG", "XGUNTA", "XM3",
        "XMART", "XPER", "XS", "XT", "XTANK", "XTGUN", "XTUR", "XU",
    };

    bool GameStats::IsEnemy(const std::string& type)
    {
        return EnemyTypes.contains(Str::ToUpper(type));
    }

    void GameStats::CountEnemy(const std::string& type, Kill kill)
    {
        if (!EnemyTypes.contains(Str::ToUpper(type)))
        {
            return;
        }

        if (kill == Kill::Killed)
        {
            Kills++;
        }
        else
        {
            Enemies++;
        }
    }

    int GameStats::IndexOf(int level)
    {
        return Flow::GameState::IndexOf(level);
    }
}
