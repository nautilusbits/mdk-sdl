#pragma once

#include "Mdk.Core/Numerics.h"
#include "Mdk.Game/Collision/ArenaSpace.h"

namespace Mdk::Game::Objects
{
    class MdkObject;

    // The ridden board in front of the floors it stands above. The original draws an object
    // after the floor of its BSP leaf, so the board shows whole even where it dips into the slope
    // (uphill its feet lag a step under it, banked turns sink an edge); with a depth buffer that part
    // hid. As Kurt's sprite (KurtSprite.InFront) it's scaled towards the eye: the same
    // on screen, nearer than the floors the eye's rays to its box cross near it (a crest farther on,
    // hiding its nose, doesn't count). It can sink deeper than it is thick (a flat floor, a bank, a
    // slope's change), whole under the floor.
    //   eye ●────────────────────╱── floor
    //                     board ▭  (under the floor at a corner) ─► pulled in front of it
    struct FloorPull
    {
        // The board's transform scaled towards the eye looking along forward.
        static Matrix4x4 Of(const MdkObject& board, Matrix4x4 transform, Vector3 eye, Vector3 forward, Collision::ArenaSpace& space);

    private:
        // The box's points: a grid of 3 along x and y, its bottom and top; the middles catch
        // a bump under the board or a tube's floor, its ends and edges above it.
        static constexpr int Grid = 3;
        static constexpr int Layers = 2;
        static constexpr float MinDepth = 1e-3f;
    };
}
