#pragma once

#include "core/Numerics.h"

namespace Mdk::Game::Stream
{
    // A 3x3 basis by its columns (MDK coordinates): X right, Y forward, Z up. As Godot's Basis,
    // which the stream's reference uses.
    struct TubeBasis
    {
        Vector3 X{};
        Vector3 Y{};
        Vector3 Z{};

        bool operator==(const TubeBasis&) const = default;

        static TubeBasis Identity() { return { Vector3::UnitX(), Vector3::UnitY(), Vector3::UnitZ() }; }

        // The basis applied to a vector: X·v.x + Y·v.y + Z·v.z.
        Vector3 Apply(Vector3 v) const { return X * v.X + Y * v.Y + Z * v.Z; }

        friend TubeBasis operator*(const TubeBasis& a, const TubeBasis& b) { return { a.Apply(b.X), a.Apply(b.Y), a.Apply(b.Z) }; }

        // A rotation by degrees about a unit axis (counter-clockwise seen from the axis's tip).
        static TubeBasis Rotation(Vector3 axis, float degrees);

        // The model transform (row vectors, as Matrix4x4) of this basis at origin.
        Matrix4x4 At(Vector3 origin) const
        {
            return Matrix4x4(X.X, X.Y, X.Z, 0.f, Y.X, Y.Y, Y.Z, 0.f, Z.X, Z.Y, Z.Z, 0.f, origin.X, origin.Y, origin.Z, 1.f);
        }
    };
}
