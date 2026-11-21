#include <doctest.h>

#include "game/level/SkyModes.h"

#include <utility>

using namespace Mdk;
using namespace Mdk::Engine::Render;
using namespace Mdk::Game::Level;

// The scripts' sky modes (opcode 202, level.gd show_sky).
namespace
{
    const Vector4 Blue{0.0f, 0.0f, 1.0f, 1.0f};
}

TEST_CASE("SkyTests.ModesPickTheBackdrop")
{
    const std::pair<int, Backdrop> cases[] = {{0, Backdrop::Sky}, {1, Backdrop::Clear}, {-1, Backdrop::Keep}};
    for (const auto& [mode, backdrop] : cases)
    {
        CAPTURE(mode);
        CHECK(SkyModes::BackdropOf(mode) == backdrop);
    }
}

TEST_CASE("SkyTests.BlackModeClearsToBlack")
{
    CHECK(SkyModes::ClearOf(SkyModes::Black, Blue) == Vector4(0.0f, 0.0f, 0.0f, 1.0f));
    CHECK(SkyModes::ClearOf(SkyModes::Shown, Blue) == Blue);
}
