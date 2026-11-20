#pragma once

#include "Mdk.Core/Numerics.h"

namespace Mdk::Engine::Render
{
    enum class Primitive;

    // The original draws without a depth buffer: a poster drawn after its wall covers it, a
    // glass pane's outline covers the edge it lies on. Here they'd flicker or be hidden by halves, so
    // they're drawn pulled towards the eye by a share of their distance: the same place on screen (no
    // cracks to their neighbours), nearer in depth.
    //   eye ●─────────────────────○── wall
    //                           ◄─┘ poster: 1/256 of the distance a layer
    //                         ◄───┘ outline: 1/128 (half a pixel off the edge at a grazing angle)
    class DepthPull
    {
    public:
        // World to world: scaled towards the eye.
        static Matrix4x4 Of(Vector3 eye, Primitive primitive, int layer);

    private:
        static constexpr float LayerShare = 1.0f / 256.0f;
        static constexpr float LineShare = 1.0f / 128.0f;
    };
}
