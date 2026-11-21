#pragma once

#include <optional>
#include <string>
#include <vector>

#include "engine/platform/Window.h"

namespace Mdk::Game::Flow
{
    // What a screen asks the game to do next (see Game).
    enum class Event
    {
        // The screen goes on.
        None,
        Quit,
        // The main menu (with the splash when GameState.Splash).
        Menu,
        // Load and play GameState.Level.
        Play,
        // The briefing of GameState.Level, then the level (a new game, a save made before a level).
        Briefing,
        // The screens after the level GameState.Level (statistics, save prompt, next briefing).
        Statistics,
        // The level ended: the tornado is over.
        LevelEnded,
        // The level ended badly (the town flattened).
        GameOver,
        KurtDied,
        // The end of the game (event 81): the end movies.
        GameFinished,
        // The fall before GameState.Level, then the level.
        Fall,
        // The stream after the level GameState.Level.
        Stream,
        // The stream is over: the statistics, or the last level after LEVEL8.
        StreamEnded,
    };

    // A screen of the game: the menu, the loading screen, a level, the statistics...
    class IScreen
    {
    public:
        virtual ~IScreen() = default;

        // One frame of `elapsed` seconds: input, update, draw and present (saving it to `screenshot`
        // when given; empty: none).
        virtual Event Frame(float elapsed, const std::optional<std::string>& screenshot) = 0;

        // The debug overlay's lines about the screen: its name, its state.
        virtual std::vector<std::string> Status() const = 0;

        // A touch screen's keyboard: shown while the screen reads typed text (a save's name).
        virtual Engine::Platform::Typing Typing() const { return Engine::Platform::Typing::Off; }

        // IDisposable kept as a call: Game frees a screen's resources before the next one, while
        // the level's Viewer stays readable for its counts (destruction would come too late).
        virtual void Dispose() = 0;
    };
}
