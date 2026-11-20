#pragma once

#include <array>
#include <set>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/Arena.h"

namespace Mdk::Game::Level
{
    // Which flagged edges Outlines::Of draws.
    enum class OutlineEdges
    {
        // Every flagged edge, as the original.
        Flagged,
        // Not those two coplanar triangles share (the enhanced look): the editor flagged some
        // diagonals inside glass panes, e.g. the fans of level 7's DANT_7 columns.
        Frame,
    };

    // Outlined arena triangles (0x40b7f0; godot-mdk mdk_mesh_builder.gd _add_outlines):
    // flag bit 23 draws lines in the triangle's own colour along the edges bits 20-22 pick, so the
    // untextured glass panes get coloured frames.
    //        v2
    //   bit 22 ╱╲ bit 21
    //        ╱    ╲
    //      v0──────v1
    //        bit 20
    struct Outlines
    {
        static constexpr uint Outlined = 1u << 23;

        // The outlined edges of triangles, two points each.
        static std::vector<Vector3> Of(const Formats::Arena& arena, std::span<const int> triangles, OutlineEdges edges = OutlineEdges::Flagged);

    private:
        struct EdgeBit
        {
            uint Bit;
            int From;
            int To;
        };

        static constexpr std::array<EdgeBit, 3> Edges = { EdgeBit{ 1u << 20, 0, 1 }, EdgeBit{ 1u << 21, 1, 2 }, EdgeBit{ 1u << 22, 2, 0 } };
        // Normals at most ~2.5° apart: one flat surface.
        static constexpr float CoplanarCos = 0.999f;

        // The edges (vertex index pairs) two coplanar triangles of triangles share.
        //   v2 ┌──┐ v3     v1-v2 is inner: both halves lie in one plane
        //      │╲ │
        //   v0 └──┘ v1
        static std::set<std::pair<int, int>> InnerEdges(const Formats::Arena& arena, const std::vector<int>& triangles);

        static Vector3 Normal(const Formats::Arena& arena, int t);

        static std::pair<int, int> Key(int a, int b) { return a < b ? std::pair(a, b) : std::pair(b, a); }
    };
}
