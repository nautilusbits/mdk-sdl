#pragma once

#include <vector>

#include "core/DotNet.h"
#include "core/Numerics.h"
#include "formats/Arena.h"

namespace Mdk::Game::Level
{
    // The depth layer of each arena triangle. The original draws without a depth buffer, back
    // to front, so a poster in its wall's plane simply covers it; with a depth buffer the two flicker.
    // A triangle overlapping bigger ones of its plane is one layer above the highest of them, and the
    // renderer draws it that much nearer (Material.DepthLayer). Its corners stay put: moving it off
    // the wall would open cracks to its neighbours.
    //   wall ──────────────  layer 0
    //   poster   ────        layer 1    (also when only partly on the wall)
    //   sticker   ──         layer 2
    struct Layers
    {
        // The highest layer: the pull towards the eye stays small (DepthPull).
        static constexpr int Highest = 3;

        static std::vector<int> Of(const Formats::Arena& arena);

    private:
        // Planes are told apart to 1/64 of a unit.
        static constexpr float PlaneSteps = 64.f;
        // Overlaps below this area (square units) are rounding: neighbours sharing an edge.
        static constexpr float MinOverlap = 1e-3f;
        static constexpr int Corners = 3;

        struct PlaneTriangle
        {
            int Triangle = 0;
            float Area = 0;
            Vector3 Normal{};
        };

        // The triangles of each plane, biggest first; of equal ones, the earlier in the data.
        static std::vector<std::vector<PlaneTriangle>> Planes(const Formats::Arena& arena, int count);

        // Each triangle goes one layer above the highest bigger one it overlaps.
        static void Stack(const Formats::Arena& arena, const std::vector<PlaneTriangle>& plane, std::vector<int>& layers);

        static int Step(float value) { return ToInt(MathF::Round(value * PlaneSteps)); }

        static Vector3 Corner(const Formats::Arena& arena, int t, int k) { return arena.Vertices[arena.TriangleIndices[t * Corners + k]]; }

        // The area two triangles of a plane share: one clipped by the other's edges
        // (Sutherland-Hodgman), in the plane's 2D coordinates around the first corner (small numbers).
        static float Overlap(const Formats::Arena& arena, int t, int other, Vector3 normal);

        // The part of a polygon on the inner side of the edge from a to b.
        static std::vector<Vector2> ClipBy(const std::vector<Vector2>& polygon, Vector2 a, Vector2 b, float winding);

        static float Cross(Vector2 a, Vector2 b) { return a.X * b.Y - a.Y * b.X; }
    };
}
