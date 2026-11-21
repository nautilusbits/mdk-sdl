#include "engine/platform/HeadLook.h"

#include "core/DotNet.h"

namespace Mdk::Engine::Platform
{
    namespace
    {
        // float.RadiansToDegrees.
        constexpr float StraightDegrees = 180.0f;
        float RadiansToDegrees(float radians) { return radians * StraightDegrees / MathF::PI; }
    }

    Vector2 HeadLook::FromGyro(Vector3 rate, float seconds, ScreenTurn turn)
    {
        // About up (positive: to the left) and about right (positive: nodding up).
        float sign = turn == ScreenTurn::Landscape ? 1.0f : -1.0f;
        float aboutUp = sign * rate.X;
        float aboutRight = -sign * rate.Y;

        float right = MathF::Abs(aboutUp) < Noise ? 0.0f : -aboutUp;
        float down = MathF::Abs(aboutRight) < Noise ? 0.0f : -aboutRight;
        return Vector2(right, down) * RadiansToDegrees(seconds);
    }
}
