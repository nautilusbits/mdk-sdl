#include "Mdk.Game/Scripts/ScriptMath.h"

namespace Mdk::Game::Scripts::ScriptMath
{
    namespace
    {
        // Vector3's indexer (C#'s v[axis]).
        float Axis(Vector3 v, int axis)
        {
            return axis == 0 ? v.X : axis == 1 ? v.Y : v.Z;
        }

        const OperandList EmptyList{};
    }

    Vector2 FromAngle(float degrees)
    {
        auto radians = ToRadians(degrees);
        return Vector2(MathF::Cos(radians), MathF::Sin(radians));
    }

    Vector2 Rotated(Vector2 v, float degrees)
    {
        auto [s, c] = MathF::SinCos(ToRadians(degrees));
        return Vector2(v.X * c - v.Y * s, v.X * s + v.Y * c);
    }

    Vector3 RotatedZ(Vector3 v, float degrees)
    {
        auto xy = Rotated(Vector2(v.X, v.Y), degrees);
        return Vector3(xy.X, xy.Y, v.Z);
    }

    float F(const Operand& operand)
    {
        if (auto f = std::get_if<float>(&operand))
        {
            return *f;
        }

        if (auto i = std::get_if<int>(&operand))
        {
            return static_cast<float>(*i);
        }

        return 0.0f;
    }

    int I(const Operand& operand)
    {
        if (auto i = std::get_if<int>(&operand))
        {
            return *i;
        }

        if (auto f = std::get_if<float>(&operand))
        {
            return ToInt(*f);
        }

        return 0;
    }

    const OperandList& L(const Operand& operand)
    {
        if (auto list = std::get_if<OperandList>(&operand))
        {
            return *list;
        }

        return EmptyList;
    }

    Vector3 V(const OperandList& list, int start)
    {
        return Vector3(F(list.at(start)), F(list.at(start + 1)), F(list.at(start + 2)));
    }

    bool Intersects(const Box& a, const Box& b)
    {
        return a.Min.X < b.Max.X && a.Max.X > b.Min.X && a.Min.Y < b.Max.Y && a.Max.Y > b.Min.Y && a.Min.Z < b.Max.Z && a.Max.Z > b.Min.Z;
    }

    bool Contains(const Box& box, Vector3 p)
    {
        return p.X >= box.Min.X && p.Y >= box.Min.Y && p.Z >= box.Min.Z && p.X <= box.Max.X && p.Y <= box.Max.Y && p.Z <= box.Max.Z;
    }

    Box Grow(const Box& box, float amount)
    {
        return Box{box.Min - Vector3(amount), box.Max + Vector3(amount)};
    }

    Box Expand(const Box& box, Vector3 p)
    {
        return Box{Vector3::Min(box.Min, p), Vector3::Max(box.Max, p)};
    }

    std::optional<Vector3> SegmentEntry(const Box& box, Vector3 from, Vector3 to)
    {
        auto d = to - from;
        auto enter = 0.0f;
        auto leave = 1.0f;
        for (auto axis = 0; axis < 3; axis++)
        {
            auto start = Axis(from, axis);
            auto step = Axis(d, axis);
            if (step == 0.0f)
            {
                if (start < Axis(box.Min, axis) || start > Axis(box.Max, axis))
                {
                    return std::nullopt;
                }

                continue;
            }

            auto t0 = (Axis(box.Min, axis) - start) / step;
            auto t1 = (Axis(box.Max, axis) - start) / step;
            enter = MathF::Max(enter, MathF::Min(t0, t1));
            leave = MathF::Min(leave, MathF::Max(t0, t1));
            if (enter > leave)
            {
                return std::nullopt;
            }
        }

        return from + d * enter;
    }
}
