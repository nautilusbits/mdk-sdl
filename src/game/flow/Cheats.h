#pragma once

#include <string>

namespace Mdk::Game::Flow
{
    class Settings;
    class GameState;

    // Cheats typed in a level (0x42c5f0, main.gd _type): TOOSCARYFORME turns gore on or off for the
    // rest of the game (the settings, not saved), SEETHEWHOLEGAME the main menu's debug keys.
    class Cheats
    {
    public:
        enum class Cheat { None, Gore, DebugKeys };

        Cheats(Settings& settings, GameState& state) : _settings(settings), _state(state) {}

        // One more letter (A-Z); returns the cheat it completes, applied.
        Cheat Type(char letter);

    private:
        static constexpr const char* GoreCheat = "TOOSCARYFORME";
        static constexpr const char* DebugCheat = "SEETHEWHOLEGAME";

        Settings& _settings;
        GameState& _state;

        // The last letters typed, as long as the longest cheat.
        std::string _typed;
    };
}
