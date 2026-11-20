#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Mdk.Game/Flow/SaveGames.h"

namespace Mdk::Game::Scripts
{
    class GameStats;
}

namespace Mdk::Game::Flow
{
    // Kurt's health and the pickups (model names, in order) he took in the fall.
    struct FallCarry
    {
        int Health = 0;
        std::vector<std::string> Pickups;

        bool operator==(const FallCarry&) const = default;
    };

    // What the game keeps between its screens (game_state.gd): the level to play, the deaths,
    // the air strike, the last level's counts, the splash and the debug keys.
    class GameState
    {
    public:
        // The order the levels are played in (0x490030): the level index (0x574268, 0-5) picks
        // the TRAVERSE/LEVELn directory. Gunter's level, LEVEL5, is the last.
        static constexpr std::array<int, 6> Order = {7, 6, 3, 4, 8, 5};

        // LEVELn to play (3-8).
        int Level = Order[0];
        // Deaths so far (0x574407).
        int Deaths = 0;
        // The one air strike of the last two levels was used (0x57440b).
        bool StrikeUsed = false;
        // The last level's counts and town flags, for the statistics.
        std::shared_ptr<Scripts::GameStats> Stats;
        // The INTRO1A splash shows when the main menu opens next: at start, after Kurt
        // died and after the end movies.
        bool Splash = true;
        // The cheat SEETHEWHOLEGAME (0x5742bc): keys 3-8 in the main menu start that
        // level, D the statistics with random counts.
        bool DebugKeys = false;
        // What the fall hands on to the next level: Kurt's health and the pickups he took.
        std::optional<FallCarry> Carry;
        // The level's state of a full save being loaded (JSON), until the level restores it.
        std::optional<std::string> Snapshot;
        // This session's last full save (its name), quick load's first choice.
        std::optional<std::string> QuickSlot;
        // A HUD message the next level shows when it starts ("Game loaded"), once.
        std::optional<std::string> Notice;
        // The console's map: the arena the next level starts in (on a floor of it), once.
        std::optional<std::string> StartArena;

        // Stats = new().
        GameState();

        // The index (0-5) of a LEVELn number in the order of play, -1 if none.
        static int IndexOf(int level);

        // A new game from the level at `index` of the order.
        void NewGame(int index);

        // The state of a save: its level, deaths and air strike.
        void Load(const SaveGame& save);

        // LEVEL5 after the Gunter stream: Kurt keeps the stream's health, not the pickups
        // (0x4325b0 clears the inventory).
        void EnterLastLevel(int health);

        // A light save of the level to play (health 100, as the original's).
        SaveGame Save(SaveKind kind) const;
    };
}
