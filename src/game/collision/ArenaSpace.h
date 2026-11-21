#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/Numerics.h"
#include "formats/Arena.h"
#include "game/collision/Bsp.h"
#include "game/collision/Solids.h"

namespace Mdk::Game::Collision
{
    // The arenas Kurt can be in, and his moves through their BSPs (damp_collide_move
    // 0x465e34): his arena with sliding, then, if that ended free, the arena he moves into without.
    // The arena around a point is the smallest whose bounds (grown by 2) hold it.
    //   feet ──► box (0.6, 0.6, 2.5) at feet + 0.5 (walking) │ (0.4, 0.4, 2.5) at feet + 0.01 (falling)
    //        ──► Kurt's arena: 4 slides ──free?──► the other arena: no slide ──► new feet
    //
    // The queries of Kurt's ledge grab (damp_ledge_grab 0x469868), the camera's clearance
    // (camera_clearance 0x417ee8) and his fall out of the arena (damp_gravity 0x469efc), in his arena
    // (the first solid one) and, for some, the second (ArenaSpace.Probes).
    class ArenaSpace
    {
    public:
        // Which of Kurt's two boxes a move uses.
        enum class Motion { Walk, Fall };

        // A move's result: where the feet end, and the triangle and node (plane) it stopped on.
        // Bsp is non-owning: the space owns its arenas.
        struct Result
        {
            Vector3 Feet{};
            int Triangle = 0;
            Collision::Bsp* Bsp = nullptr;
            int Node = 0;

            bool operator==(const Result&) const = default;

            bool Hit() const { return Triangle != Bsp::None; }

            // The plane's normal of the contact, or up without one.
            Vector3 Normal() const { return Bsp == nullptr || Node < 0 ? Vector3::UnitZ() : Bsp->Arena->Nodes[Node].Normal; }
        };

        // An up-facing face a segment crossed: its arena, triangle and the point.
        struct Surface
        {
            Collision::Bsp* Bsp = nullptr;
            int Triangle = 0;
            Vector3 Point{};

            bool operator==(const Surface&) const = default;

            Vector3 Normal() const { return Bsp->PlaneOf(Triangle).Normal; }
        };

        // Kurt collides with his arena and the active second one only (0x465e34); rays and
        // objects still see every arena.
        void SetSolid(std::span<const std::string> arenas);

        // Adds an arena once (a teleport may add one no connection leads to).
        void Add(const std::shared_ptr<Formats::Arena>& arena);

        // The arenas around a point, smallest first.
        std::vector<Bsp*> At(Vector3 point) const;

        // The name of the arena around a point, or null.
        std::optional<std::string> ArenaAt(Vector3 point) const;

        // Moves Kurt's feet by delta, sliding along faces met obliquely enough
        // ((D·n)² ≤ slideK·|D|²).
        Result Move(Vector3 feet, Vector3 delta, Motion motion, float slideK);

        // The same move, then against the solid objects around (damp_collide_move's object
        // pass): they change only the XY, and the BSP is swept again to the new end.
        Result Move(Vector3 feet, Vector3 delta, Motion motion, float slideK, std::span<const Solids::Solid> solids);

        // How far the falling box at feet lies below the floor plane a move stopped on (0: above
        // it, or no floor, |nz| < 0.5). Its lowest corner counts: on a slope that is more than
        // under the centre.
        static float Sunk(const Result& result, Vector3 feet);

        // The owners of the solids Kurt's walking box at feet touches or is in.
        static void Touching(Vector3 feet, std::span<const Solids::Solid> solids, std::vector<Objects::MdkObject*>& owners)
        {
            Solids::Touching(feet + Vector3(0.f, 0.f, WalkBox().Z + WalkLift), WalkBox(), solids, owners);
        }

        // The nearest floor a segment crosses (0x421708), in any arena around its ends.
        bool Floor(Vector3 from, Vector3 to, Vector3& point);

        // --- ArenaSpace.Probes ---

        // The first up-facing face (|nz| ≥ 0.5) the segment crosses (0x421708): in Kurt's arena,
        // then the second.
        std::optional<Surface> FloorCrossing(Vector3 from, Vector3 to);

        // The edge of a surface crossed by the line from its point back to back:
        // the point on it and its direction, or null.
        static std::optional<std::pair<Vector3, Vector3>> Edge(const Surface& surface, Vector3 back)
        {
            return surface.Bsp->Edge(surface.Triangle, surface.Point, back);
        }

        // Whether a box of half extents half sweeps from from to to (centres) through Kurt's
        // arena without a hit.
        bool Free(Vector3 from, Vector3 to, Vector3 half);

        // The first face a box of half extents half meets from from to to, in Kurt's arena then
        // the second: the box's centre there and the face's plane.
        std::optional<std::pair<Vector3, Formats::BspNode>> Sight(Vector3 from, Vector3 to, Vector3 half);

        // Whether the segment crosses a face of Kurt's arena (0x421680).
        bool Crosses(Vector3 from, Vector3 to);

        // The lowest point of Kurt's arena (arena+0x44e), or of an arena around the feet (Kurt's
        // arena is known a step after a teleport), or null.
        std::optional<float> Bottom(Vector3 feet);

    private:
        static Vector3 WalkBox() { return Vector3(0.6f, 0.6f, 2.5f); }
        static Vector3 FallBox() { return Vector3(0.4f, 0.4f, 2.5f); }
        // The box's bottom above the feet: anything lower is walked over.
        static constexpr float WalkLift = 0.5f;
        static constexpr float FallLift = 0.01f;
        static constexpr int Slides = 4;
        static constexpr float BoundsMargin = 2.f;
        // Faces at least this upright are floors (0x421708).
        static constexpr float FloorNz = 0.5f;

        struct Bounded
        {
            std::shared_ptr<Collision::Bsp> Bsp;
            Vector3 Min{};
            Vector3 Max{};
            float Volume = 0;
        };

        // The arenas, smallest first (equal volumes in the order added).
        std::vector<Bounded> _arenas;
        // The arenas Kurt collides with, his first (empty: those around him by bounds).
        std::vector<Bsp*> _solid;
        // The queries' arenas around points (kept: no list per query).
        std::vector<Bsp*> _around;
        std::vector<Bsp*> _kurts;

        Bsp* Named(const std::string& name) const;

        // Adds the arenas around a point not in arenas yet, smallest first.
        void AddAt(Vector3 point, std::vector<Bsp*>& arenas) const;

        // The smallest arena around a point, or null.
        Bsp* First(Vector3 point) const;

        static bool Contains(Vector3 min, Vector3 max, Vector3 p)
        {
            return p.X >= min.X - BoundsMargin && p.Y >= min.Y - BoundsMargin && p.Z >= min.Z - BoundsMargin
                && p.X <= max.X + BoundsMargin && p.Y <= max.Y + BoundsMargin && p.Z <= max.Z + BoundsMargin;
        }

        // Kurt's arena and the second (or those around the point).
        std::vector<Bsp*>& Kurts(Vector3 point);

        // Kurt's arena (or the smallest around the point).
        Bsp* Kurt(Vector3 point) const { return _solid.size() > 0 ? _solid[0] : First(point); }
    };
}
