#pragma once

#include <numbers>
#include <optional>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Formats/Scripts/ScriptDecoder.h"

// Angles in degrees (MDK: yaw 0 = +X, 90 = +Y), boxes and operand conversions shared by
// the script runtime, the VM and the object motion.
// A namespace rather than a struct so .cpp files can `using namespace` it like C#'s `using static`.
namespace Mdk::Game::Scripts::ScriptMath
{
    using Formats::Box;
    using Formats::Scripts::Operand;
    using Formats::Scripts::OperandList;

    inline constexpr float FullTurn = 360.0f;
    inline constexpr float HalfTurn = 180.0f;

    // float.DegreesToRadians / RadiansToDegrees.
    inline float ToRadians(float degrees) { return degrees * std::numbers::pi_v<float> / 180.0f; }
    inline float ToDegrees(float radians) { return radians * 180.0f / std::numbers::pi_v<float>; }

    // An angle in [0, 360).
    inline float Wrap360(float degrees) { return degrees - FullTurn * MathF::Floor(degrees / FullTurn); }

    // An angle in [-180, 180).
    inline float WrapAngle(float degrees) { return degrees - FullTurn * MathF::Floor((degrees + HalfTurn) / FullTurn); }

    inline float MoveToward(float from, float to, float delta)
    {
        return MathF::Abs(to - from) <= delta ? to : from + MathF::Sign(to - from) * delta;
    }

    // The unit vector of a heading.
    Vector2 FromAngle(float degrees);

    Vector2 Rotated(Vector2 v, float degrees);

    // Turned around Z (the yaw).
    Vector3 RotatedZ(Vector3 v, float degrees);

    // Degrees of the heading from a to b (XY).
    inline float Heading(Vector3 from, Vector3 to) { return ToDegrees(MathF::Atan2(to.Y - from.Y, to.X - from.X)); }

    inline float Distance2D(Vector3 a, Vector3 b) { return Vector2(a.X - b.X, a.Y - b.Y).Length(); }

    // Rounds halves away from zero, like the Godot port.
    inline int Round(float value) { return ToInt(MathF::RoundAwayFromZero(value)); }

    // A numeric operand (int or float) as a float.
    float F(const Operand& operand);

    // A numeric operand (int or float) as an int.
    int I(const Operand& operand);

    // A list operand.
    const OperandList& L(const Operand& operand);

    // Three floats of a list from start.
    Vector3 V(const OperandList& list, int start = 0);

    // Boxes (Godot's AABB rules: intersections are strict, points inside inclusive).

    inline Vector3 Center(const Box& box) { return (box.Min + box.Max) * 0.5f; }

    inline Vector3 Size(const Box& box) { return box.Max - box.Min; }

    bool Intersects(const Box& a, const Box& b);

    bool Contains(const Box& box, Vector3 p);

    Box Grow(const Box& box, float amount);

    Box Expand(const Box& box, Vector3 p);

    // Where a segment enters a box (its start when inside), or null.
    std::optional<Vector3> SegmentEntry(const Box& box, Vector3 from, Vector3 to);
}
