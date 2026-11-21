#include "game/scripts/BulletHoles.h"

#include <limits>

#include "core/DotNet.h"

namespace Mdk::Game::Scripts
{
    std::optional<BulletHoles::Face> BulletHoles::Nearest(const Formats::Model::Part& part, std::span<const Vector3> vertices, Vector3 point,
        const std::function<bool(int)>& textured)
    {
        std::optional<Face> best;
        auto bestDistance = std::numeric_limits<float>::max();
        for (auto t = 0; t < static_cast<int>(part.TriangleMaterials.size()); t++)
        {
            if (!textured(part.TriangleMaterials[t]))
            {
                continue;
            }

            auto a = vertices[part.TriangleIndices[t * 3]];
            auto b = vertices[part.TriangleIndices[t * 3 + 1]];
            auto c = vertices[part.TriangleIndices[t * 3 + 2]];
            auto normal = Vector3::Cross(b - a, c - a);
            if (normal == Vector3::Zero())
            {
                continue;
            }

            normal = Vector3::Normalize(normal);
            auto weights = Weights(point - normal * Vector3::Dot(normal, point - a), a, b, c);
            auto distance = Vector3::DistanceSquared(a * weights.X + b * weights.Y + c * weights.Z, point);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = Face{t, weights};
            }
        }

        return best;
    }

    Vector3 BulletHoles::Weights(Vector3 p, Vector3 a, Vector3 b, Vector3 c)
    {
        constexpr float Tiny = 1e-6f;
        auto v0 = b - a;
        auto v1 = c - a;
        auto v2 = p - a;
        auto d00 = Vector3::Dot(v0, v0);
        auto d01 = Vector3::Dot(v0, v1);
        auto d11 = Vector3::Dot(v1, v1);
        auto d20 = Vector3::Dot(v2, v0);
        auto d21 = Vector3::Dot(v2, v1);
        auto denominator = d00 * d11 - d01 * d01;
        auto v = (d11 * d20 - d01 * d21) / denominator;
        auto w = (d00 * d21 - d01 * d20) / denominator;
        auto weights = Vector3::Clamp(Vector3(1.0f - v - w, v, w), Vector3::Zero(), Vector3::One());
        return weights / MathF::Max(weights.X + weights.Y + weights.Z, Tiny);
    }

    void BulletHoles::Stamp(Formats::Texture& texture, Vector2 uv, const Formats::Texture& hole)
    {
        auto left = ToInt(MathF::RoundAwayFromZero(uv.X)) - hole.Width / 2;
        auto top = ToInt(MathF::RoundAwayFromZero(uv.Y)) - hole.Height / 2;
        for (auto y = 0; y < hole.Height; y++)
        {
            for (auto x = 0; x < hole.Width; x++)
            {
                auto index = hole.Indices[y * hole.Width + x];
                if (index == 0)
                {
                    continue;
                }

                auto tx = Wrap(left + x, texture.Width);
                auto ty = Wrap(top + y, texture.Height);
                texture.Indices[ty * texture.Width + tx] = index;
            }
        }
    }
}
