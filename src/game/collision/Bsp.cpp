#include "game/collision/Bsp.h"

#include <array>
#include <limits>

namespace Mdk::Game::Collision
{
    using Formats::BspNode;

    namespace
    {
        // C#'s Vector3 indexer (0 X, 1 Y, 2 Z).
        float At(const Vector3& v, int i)
        {
            return i == 0 ? v.X : (i == 1 ? v.Y : v.Z);
        }
    }

    int Bsp::SweepBox(Vector3 a, Vector3 b, Vector3 h, int iterations, float slideK, Vector3& end, int& node,
        const std::function<void(Contact)>& contacts)
    {
        node = None;
        end = a;
        if (Arena->Nodes.empty() || Arena->Vertices.empty())
        {
            return None;
        }

        _a = a;
        _b = b;
        _h = h;
        _d0 = b - a;
        _slideLimit = slideK * _d0.LengthSquared();
        _margin = MarginFactor * (MathF::Abs(h.X) + MathF::Abs(h.Y) + MathF::Abs(h.Z));
        _slide = true;
        while (iterations >= 0 && _slide)
        {
            _canSlide = iterations != 0;
            _slide = false;
            _hitTriangle = None;
            _tBest = NoHit;
            _d = _b - _a;
            SweepNode(0);
            if (_slide)
            {
                // Slid: the next pass goes from the contact to the slid end.
                if (contacts)
                {
                    contacts(Contact{_hitNode, _hitTriangle, _p, false});
                }

                _a = _p;
                _b = _s;
            }
            else if (_hitTriangle != None)
            {
                if (contacts)
                {
                    contacts(Contact{_hitNode, _hitTriangle, _p, true});
                }
            }

            iterations--;
        }

        if (_tBest >= FreeTime)
        {
            end = _b;
            return None;
        }

        end = _p;
        node = _hitNode;
        return _hitTriangle;
    }

    void Bsp::SweepNode(int index)
    {
        while (true)
        {
            const auto& n = Arena->Nodes[index];
            auto r = MathF::Abs(_h.X * n.Normal.X) + MathF::Abs(_h.Y * n.Normal.Y) + MathF::Abs(_h.Z * n.Normal.Z);
            auto dA = n.Distance(_a);
            auto dB = n.Distance(_b);
            if (dA >= -_margin && n.Positive >= 0)
            {
                SweepNode(n.Positive);
            }

            if (dA <= _margin && n.Negative >= 0)
            {
                SweepNode(n.Negative);
            }

            auto sA = Sides(dA);
            auto sB = Sides(dB);
            auto towards = (dA < 0.f && dA <= dB) || (dA >= 0.f && dB <= dA);
            if ((sA | sB) == 3 && towards)
            {
                TestPlane(index, n, dA, r);
            }

            // Go on only into the sides the end reaches and the start doesn't.
            auto fresh = sB & (sA ^ sB);
            auto next = (fresh & 1) != 0 ? n.Positive : (fresh & 2) != 0 ? n.Negative : None;
            if (next < 0)
            {
                return;
            }

            index = next;
        }
    }

    void Bsp::TestPlane(int index, const BspNode& n, float dA, float r)
    {
        auto offset = dA >= 0.f ? MathF::Min(dA, r) : MathF::Max(dA, -r);
        auto den = Vector3::Dot(_d, n.Normal);
        if (den == 0.f)
        {
            return;
        }

        auto t = -(dA - offset) / den;
        if (t > _tBest || t > 1.f)
        {
            return;
        }

        // Moving away though dB ≤ dA: rounding of a tiny move far from the origin (1e-5 at y 2648).
        // The contact would lie far behind the start (5 units, inside the rock).
        if (t < 0.f)
        {
            return;
        }

        _p = _a + _d * t;
        auto first = dA < 0.f ? n.BackFirst : n.FrontFirst;
        auto count = dA < 0.f ? n.BackCount : n.FrontCount;
        auto triangle = TestTriangles(_p, n, first, count);
        if (triangle == None)
        {
            // Retry where the centre crosses the plane; the contact point stays (a quirk of the original).
            auto t2 = MathF::Min(-dA / den, 1.f);
            triangle = TestTriangles(_a + _d * t2, n, first, count);
            if (triangle != None)
            {
                t = t2;
            }
        }

        if (triangle == None)
        {
            return;
        }

        _hitTriangle = triangle;
        _hitNode = index;
        if (_canSlide)
        {
            Slide(n, t, den, r);
        }

        _tBest = t;
    }

    void Bsp::Slide(const BspNode& n, float t, float den, float r)
    {
        auto along = Vector3::Dot(_d0, n.Normal);
        if (along * along > _slideLimit)
        {
            _slide = false;
            _s = _p;
            return;
        }

        auto s = (1.f - t) * den;
        auto nz = n.Normal.Z;
        _slide = true;
        _s = Vector3(_b.X - s * n.Normal.X, _b.Y - s * n.Normal.Y, _b.Z);
        auto steep = (nz >= 0.f && nz < SteepNz && den <= 0.f) || (nz > -SteepNz && nz <= 0.f && den >= 0.f);
        if (steep)
        {
            _s = PushOut3(_s, n, r + PushOut, Axes::Xy);
            return;
        }

        _s.Z = _b.Z - s * nz;
        _s = PushOut3(_s, n, r + PushOut, Axes::Xyz);
    }

    Vector3 Bsp::PushOut3(Vector3 p, const BspNode& n, float e, Axes axes)
    {
        auto d = n.Distance(p);
        if (MathF::Abs(d) > e)
        {
            return p;
        }

        auto k = d >= 0.f ? e - d : -d - e;
        auto push = n.Normal * k;
        return axes == Axes::Xy ? Vector3(p.X + push.X, p.Y + push.Y, p.Z) : p + push;
    }

    int Bsp::TestTriangles(Vector3 p, const BspNode& n, int first, int count) const
    {
        for (auto t = first; t < first + count; t++)
        {
            if ((Arena->TriangleFlags[t] & NotSolid) != 0)
            {
                continue;
            }

            auto v0 = Arena->Vertices[Arena->TriangleIndices[t * 3]] - p;
            auto v1 = Arena->Vertices[Arena->TriangleIndices[t * 3 + 1]] - p;
            auto v2 = Arena->Vertices[Arena->TriangleIndices[t * 3 + 2]] - p;
            if (BoxOverlaps(v0, v1, v2, _h, n.Normal))
            {
                return t;
            }
        }

        return None;
    }

    bool Bsp::BoxOverlaps(Vector3 v0, Vector3 v1, Vector3 v2, Vector3 h, Vector3 normal)
    {
        for (auto k = 0; k < 3; k++)
        {
            if (MathF::Abs(At(normal, k)) < ProjectionMin)
            {
                continue;
            }

            auto a = k == 0 ? 1 : (k == 1 ? 2 : 0);
            auto b = k == 0 ? 2 : (k == 1 ? 0 : 1);
            auto q0 = Vector2(At(v0, a), At(v0, b));
            auto q1 = Vector2(At(v1, a), At(v1, b));
            auto q2 = Vector2(At(v2, a), At(v2, b));
            if (!RectOverlaps(q0, q1, q2, Vector2(At(h, a), At(h, b))))
            {
                return false;
            }
        }

        return true;
    }

    bool Bsp::RectOverlaps(Vector2 q0, Vector2 q1, Vector2 q2, Vector2 half)
    {
        auto code = [&](Vector2 q)
        {
            return (q.X < -half.X ? 1 : 0) | (q.X > half.X ? 2 : 0) | (q.Y < -half.Y ? 4 : 0) | (q.Y > half.Y ? 8 : 0);
        };
        auto c0 = code(q0);
        auto c1 = code(q1);
        auto c2 = code(q2);
        if (c0 == 0 || c1 == 0 || c2 == 0)
        {
            return true;
        }

        if ((c0 & c1 & c2) != 0)
        {
            return false;
        }

        auto low = std::numeric_limits<float>::max();
        auto high = std::numeric_limits<float>::lowest();
        auto add = [&](float v)
        {
            low = MathF::Min(low, v);
            high = MathF::Max(high, v);
        };

        std::array<Vector2, 3> corners{q0, q1, q2};
        for (auto i = 0; i < 3; i++)
        {
            auto p = corners[i];
            auto q = corners[(i + 1) % 3];
            if (MathF::Abs(p.X) <= half.X)
            {
                add(p.Y);
            }

            // Edge crossings of u = -half.u and u = +half.u.
            for (auto u : {-half.X, half.X})
            {
                if ((p.X - u) * (q.X - u) < 0.f)
                {
                    add(p.Y + (q.Y - p.Y) * (u - p.X) / (q.X - p.X));
                }
            }
        }

        return low <= high && low <= half.Y && high >= -half.Y;
    }

    int Bsp::Segment(Vector3 a, Vector3 b, SegmentMode mode, Vector3& point, Clip clip)
    {
        point = b;
        _passed = clip == Clip::PassesThrough ? NotSolid | Arena->ClipFlag : NotSolid;
        if (Arena->Nodes.empty())
        {
            return None;
        }

        auto [triangle, hit] = Walk(0, a, b, mode);
        if (triangle != None)
        {
            point = hit;
        }

        return triangle;
    }

    std::optional<std::pair<Vector3, Vector3>> Bsp::Edge(int triangle, Vector3 point, Vector3 back) const
    {
        const auto& n = Arena->Nodes[NodeOf(triangle)];
        auto inFront = triangle >= n.FrontFirst && triangle < n.FrontFirst + n.FrontCount;
        auto first = inFront ? n.FrontFirst : n.BackFirst;
        auto count = inFront ? n.FrontCount : n.BackCount;
        auto best = -1.f;
        std::optional<std::pair<Vector3, Vector3>> edge;
        for (auto t = first; t < first + count; t++)
        {
            for (auto k = 0; k < 3; k++)
            {
                auto a = Arena->Vertices[Arena->TriangleIndices[t * 3 + k]];
                auto b = Arena->Vertices[Arena->TriangleIndices[t * 3 + (k + 1) % 3]];
                auto along = Crossing(point, back, a, b);
                auto at = Crossing(a, b, point, back);
                if (along <= best || at < 0.f)
                {
                    continue;
                }

                best = along;
                edge = std::pair{a + (b - a) * at, b - a};
            }
        }

        return edge;
    }

    int Bsp::NodeOf(int triangle) const
    {
        for (size_t i = 0; i < Arena->Nodes.size(); i++)
        {
            const auto& n = Arena->Nodes[i];
            if ((triangle >= n.FrontFirst && triangle < n.FrontFirst + n.FrontCount)
                || (triangle >= n.BackFirst && triangle < n.BackFirst + n.BackCount))
            {
                return static_cast<int>(i);
            }
        }

        return 0;
    }

    float Bsp::Crossing(Vector3 p1, Vector3 p2, Vector3 q1, Vector3 q2)
    {
        auto denominator = (q2.X - q1.X) * (p2.Y - p1.Y) - (q2.Y - q1.Y) * (p2.X - p1.X);
        if (denominator == 0.f)
        {
            return -1.f;
        }

        auto t = ((p1.X - q1.X) * (q2.Y - q1.Y) - (p1.Y - q1.Y) * (q2.X - q1.X)) / denominator;
        return t >= 0.f && t <= 1.f ? t : -1.f;
    }

    std::pair<int, Vector3> Bsp::Walk(int index, Vector3 a, Vector3 b, SegmentMode mode)
    {
        while (true)
        {
            const auto& n = Arena->Nodes[index];
            auto dA = n.Distance(a);
            auto dB = n.Distance(b);
            auto near = dA < 0.f ? n.Negative : n.Positive;
            if (near >= 0)
            {
                auto found = Walk(near, a, b, mode);
                if (found.first != None)
                {
                    return found;
                }
            }

            // No strict crossing: the far side isn't visited.
            if (dA * dB >= 0.f)
            {
                return {None, Vector3{}};
            }

            auto hit = CrossingTriangle(n, a, b, dA, mode);
            if (hit.first != None)
            {
                return hit;
            }

            auto far = dA < 0.f ? n.Positive : n.Negative;
            if (far < 0)
            {
                return {None, Vector3{}};
            }

            index = far;
        }
    }

    std::pair<int, Vector3> Bsp::CrossingTriangle(const BspNode& n, Vector3 a, Vector3 b, float dA, SegmentMode mode)
    {
        auto nz = n.Normal.Z;
        if (mode == SegmentMode::Floor && MathF::Abs(nz) < FloorNz)
        {
            return {None, Vector3{}};
        }

        auto d = b - a;
        auto dot = Vector3::Dot(d, n.Normal);
        auto x = a + d * (dot == 0.f ? 1.f : -dA / dot);
        if (mode == SegmentMode::Floor)
        {
            auto first = nz >= FloorNz ? n.FrontFirst : n.BackFirst;
            auto count = nz >= FloorNz ? n.FrontCount : n.BackCount;
            return {PointTriangle(x, n, first, count), x};
        }

        auto front = PointTriangle(x, n, n.FrontFirst, n.FrontCount);
        return {front != None ? front : PointTriangle(x, n, n.BackFirst, n.BackCount), x};
    }

    int Bsp::PointTriangle(Vector3 x, const BspNode& n, int first, int count) const
    {
        auto abs = Vector3::Abs(n.Normal);
        auto ua = abs.X >= abs.Y && abs.X >= abs.Z ? 1 : 0;
        auto va = abs.X >= abs.Y && abs.X >= abs.Z ? 2 : (abs.Y >= abs.Z ? 2 : 1);
        for (auto t = first; t < first + count; t++)
        {
            if ((Arena->TriangleFlags[t] & _passed) != 0)
            {
                continue;
            }

            auto inside = false;
            for (auto k = 0; k < 3; k++)
            {
                auto p = Arena->Vertices[Arena->TriangleIndices[t * 3 + k]] - x;
                auto q = Arena->Vertices[Arena->TriangleIndices[t * 3 + (k + 1) % 3]] - x;
                auto pAbove = At(p, va) >= 0.f;
                auto qAbove = At(q, va) >= 0.f;
                if (pAbove == qAbove)
                {
                    continue;
                }

                // Where the edge crosses v = 0, on the +u side?
                auto u = At(p, ua) + (At(q, ua) - At(p, ua)) * (-At(p, va)) / (At(q, va) - At(p, va));
                if (u > 0.f)
                {
                    inside = !inside;
                }
            }

            if (inside)
            {
                return t;
            }
        }

        return None;
    }
}
