#pragma once

#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/Numerics.h"
#include "formats/Model.h"

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Collision
{
    // Kurt against the solid objects of his arena (damp_collide_move 0x465e34, object pass;
    // damp_platform_floor 0x41d2c4). Each visible part of an object is a box; only the XY of a walk is
    // changed. Platforms (0x100, no 0x10) are stood on: a ray from 3 above the feet to 3 below meets
    // their triangles (0x414668). See godot-mdk docs/bsp.md "Kurt's move".
    //      part box grown by Kurt's box     segment A ──► B (box centres, XY slabs, z only rejects)
    //   ┌───────────────────────┐
    //   │                       │ ◄──── A·──────────► B     first hit: B = entry, the rest slid along
    //   │                       │                           the face; later hits: B = entry
    //   └───────────────────────┘
    struct Solids
    {
        // A wall, a platform (a wall stood on), or a floor only (standable, else passed through).
        enum class Footing { Wall, Platform, Floor };

        // A part's box in the world, its object, whether Kurt may stand on it, and the part.
        // C# types the owner as object; every owner is an MdkObject.
        struct Solid
        {
            Formats::Box Box;
            Objects::MdkObject* Owner = nullptr;
            Solids::Footing Footing = Footing::Wall;
            int Part = 0;

            bool operator==(const Solid&) const = default;
        };

        // An owner whose parts' triangles a ray meets (0x414668).
        class ISurface
        {
        public:
            virtual ~ISurface() = default;

            // Whether it has triangles (else the ray meets the box's top).
            virtual bool HasMesh() const = 0;

            // Where the segment from → to first meets the part's triangles (world z), or null.
            virtual std::optional<float> Floor(int part, Vector3 from, Vector3 to) = 0;
        };

        // A ray from 3 above the feet to 3 below finds the platform (damp_platform_floor).
        static constexpr float PlatformReach = 3.f;

        // The walk from box centre a to b against the boxes (grown by Kurt's half extents half).
        // Returns the new end, or null when no box was hit. Boxes Kurt is already inside are
        // ignored (an object moved into him).
        static std::optional<Vector3> Walk(Vector3 a, Vector3 b, Vector3 half, std::span<const Solid> solids);

        // The highest platform surface under the feet, met by the ray from 3 above to
        // bottom (at least 3 below), or null. Owners without triangles (ISurface) are met at
        // their box's top.
        static std::optional<std::pair<float, Objects::MdkObject*>> Platform(Vector3 feet, float bottom, std::span<const Solid> solids);

        // The owners of the boxes (grown by half and twice the gap) that hold the box centre a:
        // what a walk stopped against, or what Kurt is in.
        static std::vector<Objects::MdkObject*> Touching(Vector3 a, Vector3 half, std::span<const Solid> solids);

        // Touching added to owners, each once.
        static void Touching(Vector3 a, Vector3 half, std::span<const Solid> solids, std::vector<Objects::MdkObject*>& owners);

    private:
        // Kurt stops this far before a face, so the next move doesn't start inside the box.
        static constexpr float Gap = 0.01f;

        // Where the ray meets the solid: its triangles, or its box's top.
        static std::optional<float> Surface(const Solid& solid, Vector3 from, Vector3 to);

        static bool Inside(const Formats::Box& box, Vector3 p)
        {
            return p.X > box.Min.X && p.X < box.Max.X && p.Y > box.Min.Y && p.Y < box.Max.Y;
        }

        // Where the segment enters the box in XY: the fraction and the axis (0 X, 1 Y) of the face.
        static std::optional<std::pair<float, int>> Entry(const Formats::Box& box, Vector3 a, Vector3 b);
    };
}
