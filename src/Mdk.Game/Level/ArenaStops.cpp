#include "Mdk.Game/Level/ArenaStops.h"

#include <bit>

namespace Mdk::Game::Level
{
    using Collision::Bsp;

    std::optional<Vector3> ArenaStops::Find(const LevelData& level, const Collision::ArenaSpace& space, const std::string& arena)
    {
        const Formats::Dti::ArenaEntry* entry = nullptr;
        for (const auto& a : level.Dti->Arenas)
        {
            if (a.Name == arena)
            {
                entry = &a;
                break;
            }
        }

        if (entry == nullptr || !level.IsReachable(arena))
        {
            return std::nullopt;
        }

        // Arenas covered by another's floor everywhere (LEVEL4 MEAT_6, CMEAT_6) still get one.
        std::vector<Vector3> points;
        for (auto type : Preferred)
        {
            for (const auto& record : entry->Records)
            {
                if (record.Type == type)
                {
                    points.push_back(StopOf(record));
                }
            }
        }

        auto floor = FirstFloor(space, arena, points, Overlap::Clear);
        if (!floor.has_value())
        {
            floor = FirstFloor(space, arena, points, Overlap::Allowed);
        }

        if (!floor.has_value())
        {
            return std::nullopt;
        }

        return *floor + Vector3(0.f, 0.f, Lift);
    }

    std::optional<Vector3> ArenaStops::FirstFloor(const Collision::ArenaSpace& space, const std::string& arena,
        const std::vector<Vector3>& points, Overlap overlap)
    {
        for (const auto& p : points)
        {
            auto f = FloorBelow(space, arena, p);
            if (f.has_value() && (overlap == Overlap::Allowed || !IsCovered(space, arena, *f)))
            {
                return f;
            }
        }

        return std::nullopt;
    }

    Vector3 ArenaStops::StopOf(const Formats::Dti::Record& record)
    {
        if (record.Type != LevelData::Connection)
        {
            return record.Position;
        }

        // The record's direction leaves the arena: -x, +x, -y, +y (hatches: none).
        Vector3 inward;
        switch (static_cast<Doorway>(std::bit_cast<int>(record.Angle)))
        {
            case Doorway::MinusX:
                inward = Vector3::UnitX();
                break;
            case Doorway::PlusX:
                inward = -Vector3::UnitX();
                break;
            case Doorway::MinusY:
                inward = Vector3::UnitY();
                break;
            case Doorway::PlusY:
                inward = -Vector3::UnitY();
                break;
            default:
                inward = Vector3::Zero();
                break;
        }

        auto middle = (record.Position + record.BoxEnd) / 2.f;
        middle.Z = record.Position.Z;
        return middle + inward * DoorwayInset;
    }

    std::optional<Vector3> ArenaStops::FloorBelow(const Collision::ArenaSpace& space, const std::string& arena, Vector3 point)
    {
        auto from = point + Vector3(0.f, 0.f, Lift);
        Bsp* bsp = nullptr;
        for (auto* b : space.At(from))
        {
            if (b->Arena->Name == arena)
            {
                bsp = b;
                break;
            }
        }

        auto to = from - Vector3(0.f, 0.f, MaxDrop);
        Vector3 floor;
        if (bsp != nullptr && bsp->Segment(from, to, Bsp::SegmentMode::Floor, floor) != Bsp::None)
        {
            return floor;
        }

        return std::nullopt;
    }

    bool ArenaStops::IsCovered(const Collision::ArenaSpace& space, const std::string& arena, Vector3 floor)
    {
        auto from = floor + Vector3(0.f, 0.f, Lift);
        auto to = floor + Vector3(0.f, 0.f, SameFloor);
        for (auto* b : space.At(from))
        {
            Vector3 hit;
            if (b->Arena->Name != arena && b->Segment(from, to, Bsp::SegmentMode::Floor, hit) != Bsp::None)
            {
                return true;
            }
        }

        return false;
    }
}
