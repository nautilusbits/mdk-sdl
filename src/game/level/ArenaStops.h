#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/DotNet.h"
#include "core/Numerics.h"
#include "formats/Dti.h"
#include "game/collision/ArenaSpace.h"
#include "game/level/LevelData.h"

namespace Mdk::Game::Level
{
    // Where Kurt can be put in an arena (the soak test's tour, the console's teleport): just
    // above a floor of that arena, under its first cover spot, else waypoint, alien or connection
    // record; one with no other arena's floor above that floor first (LEVEL4: MEAT_5's floor lies 2
    // above CMEAT_5's by door #1999; Kurt would stand outside his arena).
    //   record ●
    //          │ ≤ 200
    //   floor ─┴─── ◄── stop (4 above)
    struct ArenaStops
    {
        // The stop of an arena, or null (unknown, unreachable, no floor under its records).
        static std::optional<Vector3> Find(const LevelData& level, const Collision::ArenaSpace& space, const std::string& arena);

    private:
        static constexpr uint DtiAlien = 2;
        static constexpr uint DtiCover = 5;
        static constexpr uint DtiWaypoint = 8;
        // Kurt lands from slightly above the floor.
        static constexpr float Lift = 4.f;
        // A record is at most this high above its floor.
        static constexpr float MaxDrop = 200.f;
        // A doorway's stop is this far into its arena.
        static constexpr float DoorwayInset = 4.f;
        // Floors this close are one height (the soak's astray reach).
        static constexpr float SameFloor = 1.f;
        static constexpr std::array<uint, 4> Preferred = { DtiCover, DtiWaypoint, DtiAlien, LevelData::Connection };

        // Whether another arena's floor may lie between a stop and its arena's floor.
        enum class Overlap { Clear, Allowed };

        // Which way a doorway (DTI connection) leaves its arena; hatches go up or down.
        enum class Doorway { MinusX, PlusX, MinusY, PlusY };

        static std::optional<Vector3> FirstFloor(const Collision::ArenaSpace& space, const std::string& arena,
            const std::vector<Vector3>& points, Overlap overlap);

        // A record's point; for a doorway, its middle a step into the arena: its corner lies
        // on the arena's edge by a wall (LEVEL3 HMO_1 1000, LEVEL7 DANT_6 1009 and 1010).
        static Vector3 StopOf(const Formats::Dti::Record& record);

        // The floor of that arena under a point (not one of an overlapping arena).
        static std::optional<Vector3> FloorBelow(const Collision::ArenaSpace& space, const std::string& arena, Vector3 point);

        // Whether Kurt, dropped onto that floor, meets another arena's floor first.
        static bool IsCovered(const Collision::ArenaSpace& space, const std::string& arena, Vector3 floor);
    };
}
