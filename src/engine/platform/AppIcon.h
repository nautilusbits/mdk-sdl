#pragma once

struct SDL_Surface;

namespace Mdk::Engine::Platform
{
    // The program's icon (assets/icon.png, made by tools/gen_icon.py), embedded in the engine.
    struct AppIcon
    {
        // The icon as an SDL surface (the caller destroys it), or null with SDL's error.
        static SDL_Surface* Load();

    private:
        static constexpr const char* Resource = "icon.png";
    };
}
