#pragma once

#include "core/DotNet.h"
#include "core/Numerics.h"

namespace Mdk::Engine::Render
{
    // The sun's shadow map: an orthographic view along the sunlight, a square of
    // 2 × radius around a centre (the camera), deep enough for casters far towards the sun.
    // The centre snaps to whole texels so that shadows don't shimmer when the camera moves.
    //        sun
    //         ╲  ╲  ╲        ┌───────┐ ◄ near: Reach towards the sun
    //          ╲  ╲  ╲       │   ●   │   centre (depth 0.5)
    //   ────────▓▓──────     └───────┘ ◄ far: Reach beyond
    struct SunShadow
    {
        // How far the map reaches towards and away from the sun, from the centre (units).
        static constexpr float Reach = 4000.0f;

        // World to the shadow map's clip space (x, y -1 to 1; depth 0 to 1, growing along the light).
        static Matrix4x4 Matrix(Vector3 sunDirection, Vector3 centre, float radius, uint size);
    };
}
