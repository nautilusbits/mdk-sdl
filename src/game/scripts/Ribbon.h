#pragma once

#include <array>
#include <deque>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Numerics.h"

namespace Mdk::Game::Scripts
{
    // A twister's ribbon (trail 0x4390ec/0x4392a4, drawn by 0x439690): each tick the twister's
    // position joins a ring of the last 5; each one holds a vertical edge 2 units high around it. The
    // edges are drawn full size for the newest two only: older ones are scaled by
    // (i + 1 + 5 − n) / 4 in integers, which is 0, so the ribbon narrows to a point behind.
    // Consecutive edges make quads, textured with WMIBTEX's rows 0-16 (first), 16-32 (middle)
    // and 32-48 (last).
    //
    //   oldest ●──────●──────┬──────┬ newest      (n = 5: scales 0, 0, 0, 1, 1)
    //                        │      │
    //                        ┴──────┴
    class Ribbon
    {
    public:
        static constexpr int Length = 5;
        static constexpr std::string_view Texture = "WMIBTEX";

        // C#'s (Vector3 Position, Vector2 Uv) tuple.
        struct Corner
        {
            Vector3 Position;
            Vector2 Uv;

            bool operator==(const Corner&) const = default;
        };

        // The positions kept, oldest first.
        const std::deque<Vector3>& Centres() const { return _centres; }

        // A tick's position (the oldest goes once 5 are kept).
        void Push(Vector3 centre);

        // The scale of edge index (oldest 0) of count.
        static float ScaleOf(int index, int count)
        {
            return index < count - 2 ? static_cast<float>((index + 1 + Length - count) / (Length - 1)) : 1.0f;
        }

        // The ribbon's triangles (three corners each), UVs in texels of Texture.
        std::vector<Corner> Triangles();

        // Triangles() into corners (emptied first).
        void Triangles(std::vector<Corner>& corners);

    private:
        struct Row
        {
            float Top;
            float Bottom;
        };

        struct Edge
        {
            Vector3 Bottom;
            Vector3 Top;
        };

        static constexpr float HalfHeight = 1.0f;
        // The texel columns across the edge and rows of each segment (0x491d7c...0x491dd8).
        static constexpr float Left = 0.5f;
        static constexpr float Right = 15.5f;
        static constexpr std::array<Row, 3> Rows = {{{0.5f, 16.0f}, {16.0f, 32.0f}, {32.0f, 47.5f}}};

        std::deque<Vector3> _centres;
        std::array<Edge, Length> _edges{};
    };
}
