#include "Mdk.Game/Stream/TubeBasis.h"

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/Angles.h"

namespace Mdk::Game::Stream
{
    TubeBasis TubeBasis::Rotation(Vector3 axis, float degrees)
    {
        auto angle = DegreesToRadians(degrees);
        auto [sin, cos] = MathF::SinCos(angle);
        auto turn = [&](Vector3 v) { return v * cos + Vector3::Cross(axis, v) * sin + axis * (Vector3::Dot(axis, v) * (1.f - cos)); };
        return TubeBasis{turn(Vector3::UnitX()), turn(Vector3::UnitY()), turn(Vector3::UnitZ())};
    }
}
