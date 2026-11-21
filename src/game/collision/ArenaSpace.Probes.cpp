#include "game/collision/ArenaSpace.h"

#include <limits>

#include "core/DotNet.h"

namespace Mdk::Game::Collision
{
    std::optional<ArenaSpace::Surface> ArenaSpace::FloorCrossing(Vector3 from, Vector3 to)
    {
        for (auto* bsp : Kurts(from))
        {
            Vector3 point;
            auto triangle = bsp->Segment(from, to, Bsp::SegmentMode::Floor, point);
            if (triangle != Bsp::None)
            {
                return Surface{bsp, triangle, point};
            }
        }

        return std::nullopt;
    }

    bool ArenaSpace::Free(Vector3 from, Vector3 to, Vector3 half)
    {
        auto* own = Kurt(from);
        Vector3 end;
        int node = 0;
        return own == nullptr || own->SweepBox(from, to, half, 0, 0.f, end, node) == Bsp::None;
    }

    std::optional<std::pair<Vector3, Formats::BspNode>> ArenaSpace::Sight(Vector3 from, Vector3 to, Vector3 half)
    {
        for (auto* bsp : Kurts(from))
        {
            Vector3 end;
            int node = 0;
            if (bsp->SweepBox(from, to, half, 0, 0.f, end, node) != Bsp::None && node >= 0)
            {
                return std::pair{end, bsp->Arena->Nodes[node]};
            }
        }

        return std::nullopt;
    }

    bool ArenaSpace::Crosses(Vector3 from, Vector3 to)
    {
        auto* own = Kurt(from);
        Vector3 point;
        return own != nullptr && own->Segment(from, to, Bsp::SegmentMode::Any, point) != Bsp::None;
    }

    std::optional<float> ArenaSpace::Bottom(Vector3 feet)
    {
        std::optional<float> bottom;
        auto* own = Kurt(feet);
        for (const auto& arena : _arenas)
        {
            if (arena.Bsp.get() == own || Contains(arena.Min, arena.Max, feet))
            {
                bottom = MathF::Min(bottom.value_or(std::numeric_limits<float>::max()), arena.Min.Z);
            }
        }

        return bottom;
    }

    std::vector<Bsp*>& ArenaSpace::Kurts(Vector3 point)
    {
        if (_solid.size() > 0)
        {
            return _solid;
        }

        _kurts.clear();
        AddAt(point, _kurts);
        return _kurts;
    }
}
