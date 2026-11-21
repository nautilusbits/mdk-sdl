#include "game/level/Outlines.h"

#include <map>

namespace Mdk::Game::Level
{
    std::vector<Vector3> Outlines::Of(const Formats::Arena& arena, std::span<const int> triangles, OutlineEdges edges)
    {
        std::vector<Vector3> lines;
        std::vector<int> list(triangles.begin(), triangles.end());
        auto inner = edges == OutlineEdges::Frame ? InnerEdges(arena, list) : std::set<std::pair<int, int>>{};
        for (auto t : list)
        {
            auto flags = arena.TriangleFlags[t];
            if ((flags & Outlined) == 0)
            {
                continue;
            }

            for (const auto& [bit, from, to] : Edges)
            {
                auto a = arena.TriangleIndices[t * 3 + from];
                auto b = arena.TriangleIndices[t * 3 + to];
                if ((flags & bit) == 0 || inner.contains(Key(a, b)))
                {
                    continue;
                }

                lines.push_back(arena.Vertices[a]);
                lines.push_back(arena.Vertices[b]);
            }
        }

        return lines;
    }

    std::set<std::pair<int, int>> Outlines::InnerEdges(const Formats::Arena& arena, const std::vector<int>& triangles)
    {
        std::map<std::pair<int, int>, Vector3> normals;
        std::set<std::pair<int, int>> inner;
        for (auto t : triangles)
        {
            auto normal = Normal(arena, t);
            for (const auto& edge : Edges)
            {
                auto key = Key(arena.TriangleIndices[t * 3 + edge.From], arena.TriangleIndices[t * 3 + edge.To]);
                auto [it, added] = normals.try_emplace(key, normal);
                if (!added && MathF::Abs(Vector3::Dot(it->second, normal)) > CoplanarCos)
                {
                    inner.insert(key);
                }
            }
        }

        return inner;
    }

    Vector3 Outlines::Normal(const Formats::Arena& arena, int t)
    {
        auto v0 = arena.Vertices[arena.TriangleIndices[t * 3]];
        auto v1 = arena.Vertices[arena.TriangleIndices[t * 3 + 1]];
        auto v2 = arena.Vertices[arena.TriangleIndices[t * 3 + 2]];
        return Vector3::Normalize(Vector3::Cross(v1 - v0, v2 - v0));
    }
}
