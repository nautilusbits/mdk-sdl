#pragma once

#include "core/DotNet.h"

namespace Mdk::Game
{
    // C#'s float.DegreesToRadians / float.RadiansToDegrees (same float formulas).
    constexpr float DegreesToRadians(float degrees) { return degrees * MathF::PI / 180.f; }

    constexpr float RadiansToDegrees(float radians) { return radians * 180.f / MathF::PI; }
}
