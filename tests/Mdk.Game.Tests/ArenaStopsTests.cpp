#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Collision/Bsp.h"
#include "Mdk.Game/Level/ArenaStops.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using Mdk::Tests::GameData;

// LEVEL4's sub airlock: MEAT_5's floor (z -1989) lies 2 above CMEAT_5's. The stop by
// door #1999 put Kurt on MEAT_5's floor (soak tour seed 1: "Kurt stands outside his arena");
// the one by #1998 lands on CMEAT_5's own. MEAT_6 and CMEAT_6 lie under that floor everywhere
// near their records: they keep a stop.

namespace
{
    constexpr int Level = 4;
    // Floors seen below a stop (its records are at most this high above them).
    constexpr float Reach = 200.f;
    // Floors this close are one height (the soak's astray reach).
    constexpr float SameFloor = 1.f;

    std::shared_ptr<ArenaSpace> SpaceOf(const LevelData& level)
    {
        auto space = std::make_shared<ArenaSpace>();
        for (const auto& arena : level.Arenas)
        {
            if (level.IsReachable(arena->Name))
            {
                space->Add(arena);
            }
        }

        return space;
    }

    // Lazy, as the C#'s Lazy<T>: loaded by the first test that needs it.
    const LevelData& Data()
    {
        static auto data = std::make_shared<LevelData>(*GameData::Data(), Level);
        return *data;
    }

    const ArenaSpace& Space()
    {
        static auto space = SpaceOf(Data());
        return *space;
    }

    struct Floor
    {
        std::string Name;
        float Z;
    };

    // The arena whose floor Kurt lands on below the stop, that one when it ties.
    std::optional<std::string> TopFloorArena(const ArenaSpace& space, const std::string& arena, Vector3 stop)
    {
        auto to = stop - Vector3(0.f, 0.f, Reach);
        std::vector<Floor> floors;
        for (auto* b : space.At(stop))
        {
            Vector3 p;
            if (b->Segment(stop, to, Bsp::SegmentMode::Floor, p) == Bsp::None)
            {
                continue;
            }

            floors.push_back({b->Arena->Name, p.Z});
        }

        if (floors.empty())
        {
            return std::nullopt;
        }

        auto top = std::max_element(floors.begin(), floors.end(), [](const Floor& a, const Floor& b) { return a.Z < b.Z; })->Z;
        for (const auto& f : floors)
        {
            if (f.Name == arena && f.Z >= top - SameFloor)
            {
                return arena;
            }
        }

        for (const auto& f : floors)
        {
            if (f.Z == top)
            {
                return f.Name;
            }
        }

        return std::nullopt;
    }
}

DATA_TEST_CASE("ArenaStopsTests.StopLandsOnItsArena")
{
    for (std::string arena : {"CMEAT_5", "MEAT_5"})
    {
        CAPTURE(arena);
        auto stop = ArenaStops::Find(Data(), Space(), arena);
        REQUIRE(stop.has_value());
        CHECK(std::optional<std::string>(arena) == TopFloorArena(Space(), arena, *stop));
    }
}

DATA_TEST_CASE("ArenaStopsTests.CoveredArenaKeepsAStop")
{
    for (std::string arena : {"MEAT_6", "CMEAT_6"})
    {
        CAPTURE(arena);
        CHECK(ArenaStops::Find(Data(), Space(), arena).has_value());
    }
}
