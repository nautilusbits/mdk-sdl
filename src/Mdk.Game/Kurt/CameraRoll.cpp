#include "Mdk.Game/Kurt/CameraRoll.h"

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/Angles.h"

namespace Mdk::Game::Kurt
{
    // Running and turning with the keys (positive: right, forward). Without both it doesn't change.
    void CameraRoll::Walk(float right, float forward, float delta)
    {
        if (forward == 0.f || right == 0.f)
        {
            return;
        }

        _held = true;
        float step = WalkRate * delta;
        if (right * forward > 0.f)
        {
            Roll += step;
            Roll = MathF::Min(Roll < 0.f ? Roll + WalkReverse : Roll, WalkLimit);
            return;
        }

        Roll -= step;
        Roll = MathF::Max(Roll > 0.f ? Roll - WalkReverse : Roll, -WalkLimit);
    }

    // Sliding: eases towards 90° − atan2(n.z, n.x·f.y − n.y·f.x) for the floor normal
    // and the facing, a tenth per tick.
    void CameraRoll::Slide(Vector3 normal, Vector2 facing, float delta)
    {
        _held = true;
        float angle = RadiansToDegrees(MathF::Atan2(normal.Z, normal.X * facing.Y - normal.Y * facing.X));
        Roll = MathF::Lerp(SlideLevel - angle, Roll, MathF::Pow(SlideKeep, Kurt::Ticks * delta));
    }

    // On the board: towards its bank at 45°/s.
    void CameraRoll::Follow(float bank, float delta)
    {
        _held = true;
        float step = BoardRate * delta;
        Roll = MathF::Abs(bank - Roll) <= step ? bank : Roll + MathF::Sign(bank - Roll) * step;
    }

    // At the start of a step: levels out unless the last step set it.
    void CameraRoll::Settle(float delta)
    {
        if (_held)
        {
            _held = false;
            return;
        }

        float step = Math::Clamp(DecayFactor * MathF::Abs(Roll), DecayMin, DecayMax) * Kurt::Ticks * delta;
        Roll = MathF::Abs(Roll) <= step ? 0.f : Roll - MathF::Sign(Roll) * step;
    }
}
