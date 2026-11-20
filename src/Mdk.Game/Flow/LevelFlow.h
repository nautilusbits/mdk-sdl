#pragma once

#include <string>

#include "Mdk.Game/Scripts/ScriptRuntime.h"

namespace Mdk::Game::Flow
{
    // What follows a level (the original's states, main loop 0x401cb8; see godot-mdk
    // docs/gameplay.md "Level flow"):
    //   index 0-3: tornado ─► stream ─► statistics, save prompt, next briefing ─► [fall] ─► next level
    //   index 4:   tornado ─► Gunter stream ─► save prompt ─► LEVEL5 (no statistics, no fall)
    //   index 5:   event 81 ─► end movies ─► menu
    //   game over (the town flattened) or a level outside the order ─► menu
    struct LevelFlow
    {
        enum class After { Menu, Statistics, LastLevel };

        static After AfterLevel(int level, Scripts::ScriptRuntime::GameOver over);

        // The level after `level` in the order of play (the last stays).
        static int Next(int level);

        // The name the save prompt offers: the level's number in the order of play (1-6).
        static std::string SaveName(int level);

    private:
        // The last level index followed by the statistics.
        static constexpr int LastStatisticsIndex = 3;
    };
}
