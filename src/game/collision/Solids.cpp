#include "game/collision/Solids.h"

#include <algorithm>

#include "core/DotNet.h"
#include "game/objects/MdkObject.h"

namespace Mdk::Game::Collision
{
    namespace
    {
        // C#'s Vector3 indexer (0 X, 1 Y, 2 Z).
        float& At(Vector3& v, int i)
        {
            return i == 0 ? v.X : (i == 1 ? v.Y : v.Z);
        }

        float At(const Vector3& v, int i)
        {
            return i == 0 ? v.X : (i == 1 ? v.Y : v.Z);
        }
    }

    std::optional<Vector3> Solids::Walk(Vector3 a, Vector3 b, Vector3 half, std::span<const Solid> solids)
    {
        std::optional<Vector3> end;
        for (const auto& solid : solids)
        {
            auto box = Formats::Box{solid.Box.Min - half, solid.Box.Max + half};
            if (solid.Footing == Footing::Floor || a.Z < box.Min.Z || a.Z > box.Max.Z || Inside(box, a))
            {
                continue;
            }

            auto target = end.value_or(b);
            auto hit = Entry(box, a, target);
            if (!hit.has_value())
            {
                continue;
            }

            auto [t, axis] = *hit;
            auto entry = a + (target - a) * t;
            At(entry, axis) -= MathF::Sign(At(target, axis) - At(a, axis)) * Gap;
            if (end.has_value())
            {
                end = entry;
                continue;
            }

            // The first hit slides along the face.
            auto slid = b - entry;
            At(slid, axis) = 0.f;
            end = entry + slid;
        }

        return end;
    }

    std::optional<std::pair<float, Objects::MdkObject*>> Solids::Platform(Vector3 feet, float bottom, std::span<const Solid> solids)
    {
        std::optional<std::pair<float, Objects::MdkObject*>> best;
        auto from = feet.Z + PlatformReach;
        auto to = MathF::Min(bottom, feet.Z - PlatformReach);
        for (const auto& solid : solids)
        {
            const auto& box = solid.Box;
            if (solid.Footing == Footing::Wall || feet.X < box.Min.X || feet.X > box.Max.X || feet.Y < box.Min.Y || feet.Y > box.Max.Y
                || box.Min.Z > from || box.Max.Z < to)
            {
                continue;
            }

            auto top = Surface(solid, Vector3(feet.X, feet.Y, from), Vector3(feet.X, feet.Y, to));
            if (!top.has_value() || *top < to || (best.has_value() && best->first >= *top))
            {
                continue;
            }

            best = std::pair{*top, solid.Owner};
        }

        return best;
    }

    std::optional<float> Solids::Surface(const Solid& solid, Vector3 from, Vector3 to)
    {
        // MdkObject inherits ISurface: C#'s "Owner is ISurface { HasMesh: true }".
        ISurface* surface = solid.Owner;
        if (surface != nullptr && surface->HasMesh())
        {
            return surface->Floor(solid.Part, from, to);
        }

        if (solid.Box.Max.Z <= from.Z)
        {
            return solid.Box.Max.Z;
        }

        return std::nullopt;
    }

    std::vector<Objects::MdkObject*> Solids::Touching(Vector3 a, Vector3 half, std::span<const Solid> solids)
    {
        std::vector<Objects::MdkObject*> owners;
        Touching(a, half, solids, owners);
        return owners;
    }

    void Solids::Touching(Vector3 a, Vector3 half, std::span<const Solid> solids, std::vector<Objects::MdkObject*>& owners)
    {
        auto reach = half + Vector3(Gap * 2.f);
        for (const auto& s : solids)
        {
            auto box = Formats::Box{s.Box.Min - reach, s.Box.Max + reach};
            if (a.X >= box.Min.X && a.X <= box.Max.X && a.Y >= box.Min.Y && a.Y <= box.Max.Y && a.Z >= box.Min.Z && a.Z <= box.Max.Z
                && std::find(owners.begin(), owners.end(), s.Owner) == owners.end())
            {
                owners.push_back(s.Owner);
            }
        }
    }

    std::optional<std::pair<float, int>> Solids::Entry(const Formats::Box& box, Vector3 a, Vector3 b)
    {
        auto enter = 0.f;
        auto leave = 1.f;
        auto axis = -1;
        for (auto i = 0; i < 2; i++)
        {
            auto step = At(b, i) - At(a, i);
            if (step == 0.f)
            {
                if (At(a, i) <= At(box.Min, i) || At(a, i) >= At(box.Max, i))
                {
                    return std::nullopt;
                }

                continue;
            }

            auto t0 = (At(box.Min, i) - At(a, i)) / step;
            auto t1 = (At(box.Max, i) - At(a, i)) / step;
            // On the face (t = 0) counts as outside: entered at once (0x45f588: A ≤ min).
            auto near = MathF::Min(t0, t1);
            if (near > enter || (near == enter && axis < 0))
            {
                enter = near;
                axis = i;
            }

            leave = MathF::Min(leave, MathF::Max(t0, t1));
            if (enter >= leave)
            {
                return std::nullopt;
            }
        }

        if (axis < 0)
        {
            return std::nullopt;
        }

        return std::pair{enter, axis};
    }
}
