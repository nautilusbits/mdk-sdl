#include "game/collision/ArenaSpace.h"

#include <algorithm>
#include <limits>

#include "core/DotNet.h"

namespace Mdk::Game::Collision
{
    void ArenaSpace::SetSolid(std::span<const std::string> arenas)
    {
        _solid.clear();
        for (size_t i = 0; i < arenas.size(); i++)
        {
            if (auto* bsp = Named(arenas[i]))
            {
                _solid.push_back(bsp);
            }
        }
    }

    Bsp* ArenaSpace::Named(const std::string& name) const
    {
        for (const auto& arena : _arenas)
        {
            if (arena.Bsp->Arena->Name == name)
            {
                return arena.Bsp.get();
            }
        }

        return nullptr;
    }

    void ArenaSpace::Add(const std::shared_ptr<Formats::Arena>& arena)
    {
        if (arena->Vertices.empty()
            || std::any_of(_arenas.begin(), _arenas.end(), [&](const Bounded& a) { return a.Bsp->Arena.get() == arena.get(); }))
        {
            return;
        }

        auto min = arena->Vertices[0];
        auto max = arena->Vertices[0];
        for (size_t i = 1; i < arena->Vertices.size(); i++)
        {
            min = Vector3::Min(min, arena->Vertices[i]);
            max = Vector3::Max(max, arena->Vertices[i]);
        }

        auto size = max - min;
        auto volume = size.X * size.Y * size.Z;
        auto at = _arenas.size();
        while (at > 0 && _arenas[at - 1].Volume > volume)
        {
            at--;
        }

        _arenas.insert(_arenas.begin() + static_cast<std::ptrdiff_t>(at), Bounded{std::make_shared<Bsp>(arena), min, max, volume});
    }

    std::vector<Bsp*> ArenaSpace::At(Vector3 point) const
    {
        std::vector<Bsp*> around;
        AddAt(point, around);
        return around;
    }

    void ArenaSpace::AddAt(Vector3 point, std::vector<Bsp*>& arenas) const
    {
        for (const auto& arena : _arenas)
        {
            if (Contains(arena.Min, arena.Max, point) && std::find(arenas.begin(), arenas.end(), arena.Bsp.get()) == arenas.end())
            {
                arenas.push_back(arena.Bsp.get());
            }
        }
    }

    Bsp* ArenaSpace::First(Vector3 point) const
    {
        for (const auto& arena : _arenas)
        {
            if (Contains(arena.Min, arena.Max, point))
            {
                return arena.Bsp.get();
            }
        }

        return nullptr;
    }

    std::optional<std::string> ArenaSpace::ArenaAt(Vector3 point) const
    {
        auto* first = First(point);
        if (first == nullptr)
        {
            return std::nullopt;
        }

        return first->Arena->Name;
    }

    ArenaSpace::Result ArenaSpace::Move(Vector3 feet, Vector3 delta, Motion motion, float slideK)
    {
        auto box = motion == Motion::Walk ? WalkBox() : FallBox();
        auto lift = motion == Motion::Walk ? WalkLift : FallLift;
        auto a = feet + Vector3(0.f, 0.f, box.Z + lift);
        auto* arenas = &_solid;
        if (arenas->empty())
        {
            arenas = &_around;
            arenas->clear();
            AddAt(feet, *arenas);
            AddAt(feet + delta, *arenas);
        }

        if (arenas->empty())
        {
            return Result{feet + delta, Bsp::None, nullptr, Bsp::None};
        }

        auto* own = (*arenas)[0];
        Vector3 end;
        int node = 0;
        auto triangle = own->SweepBox(a, a + delta, box, Slides, slideK, end, node);
        auto* hit = own;
        for (size_t i = 1; i < arenas->size(); i++)
        {
            auto* other = (*arenas)[i];
            if (triangle != Bsp::None)
            {
                break;
            }

            triangle = other->SweepBox(a, end, box, 0, slideK, end, node);
            hit = other;
        }

        return Result{feet + (end - a), triangle, triangle == Bsp::None ? nullptr : hit, node};
    }

    ArenaSpace::Result ArenaSpace::Move(Vector3 feet, Vector3 delta, Motion motion, float slideK, std::span<const Solids::Solid> solids)
    {
        auto result = Move(feet, delta, motion, slideK);
        auto box = motion == Motion::Walk ? WalkBox() : FallBox();
        auto lift = motion == Motion::Walk ? WalkLift : FallLift;
        auto a = feet + Vector3(0.f, 0.f, box.Z + lift);
        if (solids.empty())
        {
            return result;
        }

        auto end = Solids::Walk(a, a + (result.Feet - feet), box, solids);
        if (!end.has_value())
        {
            return result;
        }

        auto again = Move(feet, Vector3(end->X - a.X, end->Y - a.Y, 0.f), motion, slideK);
        again.Feet = Vector3(again.Feet.X, again.Feet.Y, result.Feet.Z);
        return again;
    }

    float ArenaSpace::Sunk(const Result& result, Vector3 feet)
    {
        if (result.Bsp == nullptr || result.Node < 0)
        {
            return 0.f;
        }

        // The plane, facing up.
        const auto& plane = result.Bsp->Arena->Nodes[result.Node];
        auto n = plane.Normal.Z < 0.f ? -plane.Normal : plane.Normal;
        auto d = plane.Normal.Z < 0.f ? -plane.D : plane.D;
        if (n.Z < FloorNz)
        {
            return 0.f;
        }

        // The box's reach towards the plane, less the centre's height above it, upright.
        auto centre = feet + Vector3(0.f, 0.f, FallBox().Z + FallLift);
        auto reach = MathF::Abs(FallBox().X * n.X) + MathF::Abs(FallBox().Y * n.Y) + FallBox().Z * n.Z;
        return MathF::Max(0.f, (reach - (Vector3::Dot(n, centre) + d)) / n.Z);
    }

    bool ArenaSpace::Floor(Vector3 from, Vector3 to, Vector3& point)
    {
        point = to;
        auto best = std::numeric_limits<float>::max();
        _around.clear();
        AddAt(from, _around);
        AddAt(to, _around);
        for (auto* bsp : _around)
        {
            Vector3 hit;
            if (bsp->Segment(from, to, Bsp::SegmentMode::Floor, hit) == Bsp::None)
            {
                continue;
            }

            auto distance = Vector3::DistanceSquared(from, hit);
            if (distance < best)
            {
                best = distance;
                point = hit;
            }
        }

        return best < std::numeric_limits<float>::max();
    }
}
