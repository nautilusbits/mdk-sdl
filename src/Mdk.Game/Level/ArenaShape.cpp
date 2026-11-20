#include "Mdk.Game/Level/ArenaShape.h"

#include "Mdk.Core/DotNet.h"

namespace Mdk::Game::Level
{
    Cover ArenaShape::CoverOf(const Formats::Arena& arena)
    {
        auto floors = 0.f;
        auto ceilings = 0.f;
        for (auto t = 0; t < arena.TriangleCount(); t++)
        {
            auto face = Face(arena, t);
            if (!face.has_value())
            {
                continue;
            }

            floors += face->first.Z > Horizontal ? face->second : 0.f;
            ceilings += face->first.Z < -Horizontal ? face->second : 0.f;
        }

        return ceilings > 0.f && ceilings >= floors * CoveredShare ? Cover::Covered : Cover::Open;
    }

    float ArenaShape::MeanUp(const Formats::Arena& arena)
    {
        auto sum = 0.f;
        auto total = 0.f;
        for (auto t = 0; t < arena.TriangleCount(); t++)
        {
            if (auto face = Face(arena, t))
            {
                sum += face->first.Z * face->second;
                total += face->second;
            }
        }

        return total > 0.f ? sum / total : 0.f;
    }

    float ArenaShape::MeanFacing(const Formats::Arena& arena, Vector3 light)
    {
        auto towards = -Vector3::Normalize(light);
        auto sum = 0.f;
        auto total = 0.f;
        for (auto t = 0; t < arena.TriangleCount(); t++)
        {
            if (auto face = Face(arena, t))
            {
                sum += MathF::Max(Vector3::Dot(face->first, towards), 0.f) * face->second;
                total += face->second;
            }
        }

        return total > 0.f ? sum / total : 0.f;
    }

    std::optional<std::pair<Vector3, float>> ArenaShape::Face(const Formats::Arena& arena, int triangle)
    {
        if (arena.ClipFlag != 0 && (arena.TriangleFlags[triangle] & arena.ClipFlag) != 0)
        {
            return std::nullopt;
        }

        auto a = arena.Vertices[arena.TriangleIndices[triangle * 3]];
        auto b = arena.Vertices[arena.TriangleIndices[triangle * 3 + 1]];
        auto c = arena.Vertices[arena.TriangleIndices[triangle * 3 + 2]];
        auto cross = Vector3::Cross(b - a, c - a);
        auto length = cross.Length();
        if (length <= 0.f)
        {
            return std::nullopt;
        }

        return std::pair{-cross / length, length * 0.5f};
    }
}
