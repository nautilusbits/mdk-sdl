#include <doctest.h>

#include <SDL3/SDL.h>

#include "Mdk.Engine/Platform/AppIcon.h"

using namespace Mdk::Engine::Platform;

// The window's icon: assets/icon.png, embedded in the engine (tools/gen_icon.py).

namespace
{
    constexpr int IconSize = 512;
}

TEST_CASE("AppIconTests.EmbeddedIconDecodes")
{
    SDL_Surface* surface = AppIcon::Load();
    INFO(SDL_GetError());
    REQUIRE(surface != nullptr);
    CHECK(surface->w == IconSize);
    CHECK(surface->h == IconSize);
    SDL_DestroySurface(surface);
}
