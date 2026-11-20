#pragma once

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"

namespace Mdk::Game::Level
{
    // How the scripts want the background drawn (0x574304, opcode 202; sky_draw 0x475b4c,
    // godot-mdk level.gd show_sky): 0 the sky, 1 black, anything else nothing (the last frame
    // stays, e.g. level 3's closed HMO_3).
    struct SkyModes
    {
        static constexpr int Shown = 0;
        static constexpr int Black = 1;

        static Engine::Render::Backdrop BackdropOf(int mode)
        {
            switch (mode)
            {
                case Shown:
                    return Engine::Render::Backdrop::Sky;
                case Black:
                    return Engine::Render::Backdrop::Clear;
                default:
                    return Engine::Render::Backdrop::Keep;
            }
        }

        // The clear colour: black in mode 1, else sky (above the panorama).
        static Vector4 ClearOf(int mode, Vector4 sky) { return mode == Black ? Vector4::UnitW() : sky; }
    };
}
