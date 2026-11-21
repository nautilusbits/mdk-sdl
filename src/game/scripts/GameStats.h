#pragma once

#include <string>
#include <unordered_set>

namespace Mdk::Game::Scripts
{
    // The level's counts for the Score-O-matic (game_state.gd's stats): chain gun shots (6
    // per tick of fire, 0x573c3c) and ticks on target (0x573c40), sniper rounds (0x573c44) and their
    // hits (0x573c48), head shots (0x573c4c), enemies created (0x573c50) and killed by Kurt
    // (0x573c54). A level starts with none.
    class GameStats
    {
    public:
        enum class Kill { Created, Killed };

        int Shots = 0;
        int ShotHits = 0;
        int SniperShots = 0;
        int SniperHits = 0;
        int HeadShots = 0;
        int Enemies = 0;
        int Kills = 0;
        // The global flags when the level ended (which towns were flattened).
        int TownFlags = 0;
        bool StrikeUsed = false;

        // Whether an object type counts as an enemy (any case).
        static bool IsEnemy(const std::string& type);

        // An enemy created (0x43bc20) or killed by Kurt (0x43357c).
        void CountEnemy(const std::string& type, Kill kill);

        // The index (0-5) of a LEVELn number in the order of play.
        static int IndexOf(int level);

        // Accuracy in percent (truncated), 0 without shots.
        static int Percent(int hits, int shots) { return shots > 0 ? hits * 100 / shots : 0; }

    private:
        // The object types that count as enemies (0x491c38).
        static const std::unordered_set<std::string> EnemyTypes;
    };
}
