#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "core/DotNet.h"
#include "core/Numerics.h"
#include "formats/Arena.h"

namespace Mdk::Game::Collision
{
    // Collisions against an arena's BSP, as the original does them (godot-mdk docs/bsp.md):
    // boxes swept with sliding (bsp_sweep_box 0x4093e0) and segments (0x421680, 0x421708).
    // Triangles flagged 0x20 (not solid) are skipped.
    //   node: plane n·p + d ── front list (faces d ≥ 0), back list (faces d < 0)
    //         ├─ negative child (d < 0)
    //         └─ positive child (d ≥ 0)          -1 = empty leaf
    class Bsp
    {
    public:
        static constexpr uint NotSolid = 0x20;
        static constexpr int None = -1;

        // Which planes and faces a segment test looks at.
        enum class SegmentMode
        {
            // Every plane, both faces (0x421680).
            Any,
            // Planes with |nz| ≥ 0.5, up-facing triangles only (0x421708).
            Floor,
        };

        // Whether a segment test stops at the triangles that only stop Kurt (Arena.ClipFlag):
        // the scripts' rays pass them.
        enum class Clip { Stops, PassesThrough };

        // A sweep's contact: the node (its plane) and triangle hit, the box centre there,
        // and whether the sweep went on sliding from it.
        struct Contact
        {
            int Node = 0;
            int Triangle = 0;
            Vector3 Point{};
            bool Final = false;

            bool operator==(const Contact&) const = default;
        };

        explicit Bsp(std::shared_ptr<Formats::Arena> arena) : Arena(std::move(arena)) {}

        std::shared_ptr<Formats::Arena> Arena;

        // Sweeps a box of half extents h from a to b (centres) in up to iterations + 1 passes, sliding
        // along faces met at least so obliquely that (D·n)² ≤ slideK·|D|². Returns the triangle
        // of the last pass's contact (None when it ended free) and where the box ends.
        int SweepBox(Vector3 a, Vector3 b, Vector3 h, int iterations, float slideK, Vector3& end, int& node,
            const std::function<void(Contact)>& contacts = nullptr);

        // The nearest triangle a segment crosses (front-to-back walk 0x421470), or None.
        int Segment(Vector3 a, Vector3 b, SegmentMode mode, Vector3& point, Clip clip = Clip::Stops);

        // The plane of the node whose list holds triangle.
        Formats::BspNode PlaneOf(int triangle) const { return Arena->Nodes[NodeOf(triangle)]; }

        // The edge of the faces in triangle's node list that the line from point to back crosses
        // (in XY) farthest towards back (damp_ledge_grab 0x469868): the point on it and its
        // direction (the triangle's winding), or null.
        std::optional<std::pair<Vector3, Vector3>> Edge(int triangle, Vector3 point, Vector3 back) const;

    private:
        // Beyond this the pass found nothing (5000 at the start, free under 100).
        static constexpr float NoHit = 5000.f;
        static constexpr float FreeTime = 100.f;
        static constexpr float MarginFactor = 2.f;
        // Faces flatter than this are climbed by a horizontal slide; steeper ones are walls.
        static constexpr float SteepNz = 0.75f;
        static constexpr float PushOut = 0.01f;
        // Coordinate planes the box-triangle test projects on: normal components at least this.
        static constexpr float ProjectionMin = 0.1f;
        static constexpr float FloorNz = 0.5f;

        enum class Axes { Xy, Xyz };

        // Sweep state (the original keeps it in globals 0x4d4e30-0x4d4eb4).
        Vector3 _a{}, _b{}, _d{}, _d0{}, _h{}, _p{}, _s{};
        float _margin = 0, _slideLimit = 0, _tBest = 0;
        bool _canSlide = false, _slide = false;
        int _hitTriangle = 0, _hitNode = 0;
        // The flags of the triangles the current segment test passes through.
        uint _passed = NotSolid;

        // One node and what lies behind it (0x409680). Children on the start's side(s) first.
        void SweepNode(int index);

        // Bands within the margin of a plane: 1 not far behind, 2 not far in front.
        int Sides(float distance) const { return (distance >= -_margin ? 1 : 0) | (distance <= _margin ? 2 : 0); }

        // Where the box touches the plane, the triangles there facing the start, and the slide.
        void TestPlane(int index, const Formats::BspNode& n, float dA, float r);

        // Not too head-on: the end moves along the face. Walls and steep up-slopes slide in XY
        // only; floors and down-facing faces are projected in 3D. Too head-on: it stops at the contact.
        void Slide(const Formats::BspNode& n, float t, float den, float r);

        // Within e of the plane: moved along the normal to exactly ±e on its side.
        static Vector3 PushOut3(Vector3 p, const Formats::BspNode& n, float e, Axes axes);

        // The first solid triangle of a node list the box at p overlaps (0x409c40).
        int TestTriangles(Vector3 p, const Formats::BspNode& n, int first, int count) const;

        // Box (centred at the origin) against a triangle, projected on each coordinate plane the
        // node's normal isn't almost parallel to (0x409de0).
        static bool BoxOverlaps(Vector3 v0, Vector3 v1, Vector3 v2, Vector3 h, Vector3 normal);

        // Exact overlap of the rectangle ±half with a triangle: outcodes, then the triangle's v
        // range within the strip |u| ≤ half.u.
        static bool RectOverlaps(Vector2 q0, Vector2 q1, Vector2 q2, Vector2 half);

        // The node whose front or back list holds the triangle.
        int NodeOf(int triangle) const;

        // Where the segment p1→p2 crosses the line q1→q2 in XY, as a fraction of p1→p2 within
        // 0-1, or -1 (0x469e68).
        static float Crossing(Vector3 p1, Vector3 p2, Vector3 q1, Vector3 q2);

        std::pair<int, Vector3> Walk(int index, Vector3 a, Vector3 b, SegmentMode mode);

        std::pair<int, Vector3> CrossingTriangle(const Formats::BspNode& n, Vector3 a, Vector3 b, float dA, SegmentMode mode);

        // The first solid triangle of a list holding a point of its plane (0x421350, 0x42de60):
        // crossings of a ray along +u, with the normal's largest axis dropped.
        int PointTriangle(Vector3 x, const Formats::BspNode& n, int first, int count) const;
    };
}
