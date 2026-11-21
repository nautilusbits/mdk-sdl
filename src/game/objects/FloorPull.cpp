#include "game/objects/FloorPull.h"

#include "core/DotNet.h"
#include "game/kurt/KurtSprite.h"
#include "game/objects/MdkObject.h"

namespace Mdk::Game::Objects
{
    namespace
    {
        // .NET's Matrix4x4.CreateScale(scale, centerPoint), missing from Core.
        Matrix4x4 ScaleAbout(float scale, Vector3 center)
        {
            auto m = Matrix4x4::CreateScale(scale);
            m.SetTranslation(center * (1.f - scale));
            return m;
        }

        // .NET's Vector3.Lerp(a, b, Vector3 amount): per component, a·(1 - t) + b·t.
        Vector3 LerpEach(Vector3 a, Vector3 b, Vector3 t)
        {
            return a * (Vector3::One() - t) + b * t;
        }
    }

    Matrix4x4 FloorPull::Of(const MdkObject& board, Matrix4x4 transform, Vector3 eye, Vector3 forward, Collision::ArenaSpace& space)
    {
        if (board.Model == nullptr)
        {
            return transform;
        }

        // Floors count where the rays cross them within the board's length of the point: the
        // floor it sank into, not a crest farther towards the eye.
        auto box = board.Model->Bounds;
        auto reach = Vector3::Distance(Vector3::Transform(box.Min, transform), Vector3::Transform(box.Max, transform));
        auto scale = 1.f;
        for (auto i = 0; i < Grid * Grid * Layers; i++)
        {
            // Point i: x from i % 3, y from i / 3 % 3 (min, middle, max), z from i / 9 (bottom, top).
            auto at = Vector3(static_cast<float>(i % Grid), static_cast<float>(i / Grid % Grid), static_cast<float>(i / (Grid * Grid)))
                / Vector3(Grid - 1, Grid - 1, Layers - 1);
            auto local = LerpEach(box.Min, box.Max, at);
            auto corner = Vector3::Transform(local, transform);
            auto floor = space.FloorCrossing(eye, corner);
            if (!floor.has_value() || Vector3::Distance(corner, floor->Point) > reach)
            {
                continue;
            }

            // A corner on the eye's plane has no depth to pull to.
            auto depth = Vector3::Dot(corner - eye, forward);
            if (depth <= MinDepth)
            {
                continue;
            }

            scale = MathF::Min(scale, Kurt::KurtSprite::InFront(depth, Vector3::Dot(floor->Point - eye, forward)));
        }

        return scale == 1.f ? transform : transform * ScaleAbout(scale, eye);
    }
}
