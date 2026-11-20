#pragma once

#include "Mdk.Core/Numerics.h"

namespace Mdk::Engine::Platform
{
    // Which way up a landscape phone is: its right side up (Landscape) or its left side up.
    enum class ScreenTurn { Landscape, LandscapeFlipped };

    // The head's turn from a phone's gyroscope (in a VR viewer): its rates about the phone's
    // portrait axes become the look's degrees, as a mouse's would turn the view.
    //   landscape (right side up):        flipped (left side up):
    //        x ↑  (the world's up)             x ↓
    //   y ←──┘    (the world's left)           └──→ y
    struct HeadLook
    {
        // The look's turn (degrees: x right, y down, as the mouse's) over `seconds` at `rate`
        // (radians a second about the phone's x, y and z). Tilting sideways (about z) turns
        // nothing: the game's views don't roll.
        static Vector2 FromGyro(Vector3 rate, float seconds, ScreenTurn turn);

    private:
        // Slower than this (radians a second) is the gyroscope's noise on a still phone.
        static constexpr float Noise = 0.02f;
    };
}
