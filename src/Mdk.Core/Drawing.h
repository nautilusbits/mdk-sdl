#pragma once

// System.Drawing's RectangleF / PointF / SizeF (value types, float, y down).

#include <algorithm>

namespace Mdk
{
    struct PointF
    {
        float X = 0;
        float Y = 0;

        constexpr bool operator==(const PointF&) const = default;
    };

    struct SizeF
    {
        float Width = 0;
        float Height = 0;

        constexpr bool operator==(const SizeF&) const = default;
    };

    struct RectangleF
    {
        float X = 0;
        float Y = 0;
        float Width = 0;
        float Height = 0;

        constexpr RectangleF() = default;
        constexpr RectangleF(float x, float y, float width, float height) : X(x), Y(y), Width(width), Height(height) {}
        constexpr RectangleF(PointF location, SizeF size) : X(location.X), Y(location.Y), Width(size.Width), Height(size.Height) {}

        static constexpr RectangleF Empty() { return {}; }
        static constexpr RectangleF FromLTRB(float left, float top, float right, float bottom) { return {left, top, right - left, bottom - top}; }

        constexpr float Left() const { return X; }
        constexpr float Top() const { return Y; }
        constexpr float Right() const { return X + Width; }
        constexpr float Bottom() const { return Y + Height; }
        constexpr PointF Location() const { return {X, Y}; }
        constexpr SizeF Size() const { return {Width, Height}; }
        constexpr bool IsEmpty() const { return Width <= 0 || Height <= 0; }

        constexpr bool Contains(float x, float y) const { return X <= x && x < X + Width && Y <= y && y < Y + Height; }
        constexpr bool Contains(PointF p) const { return Contains(p.X, p.Y); }
        constexpr bool Contains(const RectangleF& r) const
        {
            return X <= r.X && r.X + r.Width <= X + Width && Y <= r.Y && r.Y + r.Height <= Y + Height;
        }

        constexpr bool IntersectsWith(const RectangleF& r) const
        {
            return r.X < X + Width && X < r.X + r.Width && r.Y < Y + Height && Y < r.Y + r.Height;
        }

        constexpr void Offset(float x, float y) { X += x; Y += y; }
        constexpr void Offset(PointF p) { Offset(p.X, p.Y); }

        constexpr void Inflate(float x, float y)
        {
            X -= x;
            Y -= y;
            Width += 2 * x;
            Height += 2 * y;
        }

        static constexpr RectangleF Inflate(RectangleF r, float x, float y)
        {
            r.Inflate(x, y);
            return r;
        }

        // Empty when they don't overlap.
        static constexpr RectangleF Intersect(const RectangleF& a, const RectangleF& b)
        {
            float x1 = std::max(a.X, b.X);
            float x2 = std::min(a.X + a.Width, b.X + b.Width);
            float y1 = std::max(a.Y, b.Y);
            float y2 = std::min(a.Y + a.Height, b.Y + b.Height);
            if (x2 >= x1 && y2 >= y1)
            {
                return {x1, y1, x2 - x1, y2 - y1};
            }

            return Empty();
        }

        constexpr bool operator==(const RectangleF&) const = default;
    };
}
