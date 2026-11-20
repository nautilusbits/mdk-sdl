#include <string>

#include <doctest.h>

#include "Mdk.Game/Flow/Cheats.h"
#include "Mdk.Game/Flow/GameState.h"
#include "Mdk.Game/Flow/Settings.h"

using namespace Mdk::Game::Flow;

// Letters typed in a level (main.gd _type).

namespace
{
    // Types every letter; returns the last letter's cheat.
    Cheats::Cheat TypeAll(Cheats&& cheats, const std::string& text)
    {
        auto cheat = Cheats::Cheat::None;
        for (char letter : text)
        {
            cheat = cheats.Type(letter);
        }

        return cheat;
    }
}

TEST_CASE("CheatsTests.GoreCheatLastsTheSession")
{
    Settings settings;
    GameState state;

    // Typed in one level, it holds in the next (the settings live as long as the game).
    CHECK(Cheats::Cheat::Gore == TypeAll(Cheats(settings, state), "XTOOSCARYFORME"));
    CHECK_FALSE(settings.Gore);

    CHECK(Cheats::Cheat::Gore == TypeAll(Cheats(settings, state), "TOOSCARYFORME"));
    CHECK(settings.Gore);
}

TEST_CASE("CheatsTests.DebugCheatTogglesTheDebugKeys")
{
    GameState state;
    Settings settings;
    CHECK(Cheats::Cheat::DebugKeys == TypeAll(Cheats(settings, state), "SEETHEWHOLEGAME"));
    CHECK(state.DebugKeys);
}
