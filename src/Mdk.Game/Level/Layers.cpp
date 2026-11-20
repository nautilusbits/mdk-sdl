#include "Mdk.Game/Level/Layers.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace Mdk::Game::Level
{
    std::vector<int> Layers::Of(const Formats::Arena& arena)
    {
        auto count = static_cast<int>(arena.TriangleIndices.size()) / Corners;
        std::vector<int> layers(count);
        for (const auto& plane : Planes(arena, count))
        {
            Stack(arena, plane, layers);
        }

        return layers;
    }

    std::vector<std::vector<Layers::PlaneTriangle>> Layers::Planes(const Formats::Arena& arena, int count)
    {
        // The planes in the order first seen (C#'s Dictionary without removals): a key to its list's index.
        std::map<std::tuple<int, int, int, int>, size_t> keys;
        std::vector<std::vector<PlaneTriangle>> planes;
        for (auto t = 0; t < count; t++)
        {
            auto a = Corner(arena, t, 0);
            auto b = Corner(arena, t, 1);
            auto c = Corner(arena, t, 2);
            auto cross = Vector3::Cross(c - a, b - a);
            auto area = cross.Length();
            if (area == 0.f)
            {
                continue;
            }

            auto normal = cross / area;
            auto key = std::tuple{Step(normal.X), Step(normal.Y), Step(normal.Z), Step(Vector3::Dot(normal, a))};
            auto [it, added] = keys.try_emplace(key, planes.size());
            if (added)
            {
                planes.emplace_back();
            }

            planes[it->second].push_back(PlaneTriangle{t, area, normal});
        }

        for (auto& plane : planes)
        {
            std::stable_sort(plane.begin(), plane.end(), [](const PlaneTriangle& x, const PlaneTriangle& y)
            {
                if (x.Area != y.Area)
                {
                    return x.Area > y.Area;
                }

                return x.Triangle < y.Triangle;
            });
        }

        return planes;
    }

    void Layers::Stack(const Formats::Arena& arena, const std::vector<PlaneTriangle>& plane, std::vector<int>& layers)
    {
        for (size_t i = 1; i < plane.size(); i++)
        {
            auto t = plane[i].Triangle;
            for (size_t j = 0; j < i; j++)
            {
                auto under = plane[j].Triangle;
                if (layers[under] >= layers[t] && Overlap(arena, t, under, plane[i].Normal) > MinOverlap)
                {
                    layers[t] = Math::Min(layers[under] + 1, Highest);
                }
            }
        }
    }

    float Layers::Overlap(const Formats::Arena& arena, int t, int other, Vector3 normal)
    {
        auto origin = Corner(arena, t, 0);
        auto u = Vector3::Normalize(Vector3::Cross(normal, MathF::Abs(normal.X) < 0.9f ? Vector3::UnitX() : Vector3::UnitY()));
        auto v = Vector3::Cross(normal, u);
        auto flat = [&](Vector3 p) { return Vector2(Vector3::Dot(p - origin, u), Vector3::Dot(p - origin, v)); };

        std::vector<Vector2> polygon{flat(origin), flat(Corner(arena, t, 1)), flat(Corner(arena, t, 2))};
        Vector2 clip[] = {flat(Corner(arena, other, 0)), flat(Corner(arena, other, 1)), flat(Corner(arena, other, 2))};
        auto winding = static_cast<float>(MathF::Sign(Cross(clip[1] - clip[0], clip[2] - clip[0])));
        for (auto e = 0; e < Corners && !polygon.empty(); e++)
        {
            polygon = ClipBy(polygon, clip[e], clip[(e + 1) % Corners], winding);
        }

        auto twice = 0.f;
        for (size_t k = 0; k < polygon.size(); k++)
        {
            twice += Cross(polygon[k], polygon[(k + 1) % polygon.size()]);
        }

        return MathF::Abs(twice) / 2.f;
    }

    std::vector<Vector2> Layers::ClipBy(const std::vector<Vector2>& polygon, Vector2 a, Vector2 b, float winding)
    {
        auto side = [&](Vector2 p) { return winding * Cross(b - a, p - a); };

        std::vector<Vector2> kept;
        for (size_t k = 0; k < polygon.size(); k++)
        {
            auto p = polygon[k];
            auto q = polygon[(k + 1) % polygon.size()];
            auto sp = side(p);
            auto sq = side(q);
            if (sp >= 0.f)
            {
                kept.push_back(p);
            }

            if ((sp >= 0.f) != (sq >= 0.f))
            {
                kept.push_back(p + (q - p) * (sp / (sp - sq)));
            }
        }

        return kept;
    }
}
