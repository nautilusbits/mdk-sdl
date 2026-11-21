#include <optional>
#include <utility>
#include <vector>

#include <doctest.h>

#include "Asserts.h"
#include "core/Numerics.h"
#include "formats/Model.h"
#include "game/collision/Solids.h"
#include "game/objects/MdkObject.h"

using namespace Mdk;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Objects;
using Footing = Solids::Footing;
using Solid = Solids::Solid;

// Kurt against objects' part boxes (damp_collide_move's object pass, damp_platform_floor).
namespace
{
    // Kurt's walking box (half extents).
    const Vector3 Half{0.6f, 0.6f, 2.5f};

    // The crate's owner (C#: the string "crate").
    MdkObject CrateOwner;

    // A crate: x and y 10-12, z 0-4.
    const Solid Crate{Formats::Box{Vector3(10.0f, 10.0f, 0.0f), Vector3(12.0f, 12.0f, 4.0f)}, &CrateOwner, Footing::Platform};
    constexpr float Tolerance = 0.05f;

    // What Kurt does with an object: walls block his walk, floors carry him.
    enum class Meets { PassesThrough = 0, Blocked = 1, StandsOn = 2, Solid = Blocked | StandsOn };

    std::vector<Solid> Crates() { return {Crate}; }
}

TEST_CASE("SolidsTests.HeadOnWalkStopsAtTheBox")
{
    auto end = Solids::Walk(Vector3(5.0f, 11.0f, 3.0f), Vector3(15.0f, 11.0f, 3.0f), Half, Crates());
    REQUIRE(end.has_value());
    CHECK_NEAR(10.0f - Half.X, end->X, Tolerance);
    CHECK_NEAR(11.0f, end->Y, Tolerance);
}

TEST_CASE("SolidsTests.ObliqueWalkSlidesAlongTheFace")
{
    auto end = Solids::Walk(Vector3(5.0f, 11.0f, 3.0f), Vector3(15.0f, 13.0f, 3.0f), Half, Crates());
    REQUIRE(end.has_value());
    CHECK_NEAR(10.0f - Half.X, end->X, Tolerance);
    CHECK_NEAR(13.0f, end->Y, Tolerance);
}

// Kurt on a face (as a BSP wall in its plane leaves him) is outside the box and stopped at once
// (0x45f588: A ≤ min, t = 0), e.g. LEVEL4's X4DOOR before the board (issue #25).
TEST_CASE("SolidsTests.WalkFromTheFaceStops")
{
    float face = 10.0f - Half.X;
    auto end = Solids::Walk(Vector3(face, 11.0f, 3.0f), Vector3(face + 1.0f, 11.0f, 3.0f), Half, Crates());
    REQUIRE(end.has_value());
    CHECK(end->X <= face);
}

TEST_CASE("SolidsTests.BoxesAboveOrBelowAndBoxesKurtIsInAreIgnored")
{
    CHECK_FALSE(Solids::Walk(Vector3(5.0f, 11.0f, 9.0f), Vector3(15.0f, 11.0f, 9.0f), Half, Crates()).has_value());
    CHECK_FALSE(Solids::Walk(Vector3(11.0f, 11.0f, 3.0f), Vector3(15.0f, 11.0f, 3.0f), Half, Crates()).has_value());
}

TEST_CASE("SolidsTests.PlatformTopIsFoundUnderTheFeet")
{
    auto platform = Solids::Platform(Vector3(11.0f, 11.0f, 4.5f), 3.9f, Crates());
    CHECK(platform == std::optional<std::pair<float, MdkObject*>>(std::pair<float, MdkObject*>(4.0f, &CrateOwner)));

    // Beside it, or a wall (no footing), there's none.
    CHECK_FALSE(Solids::Platform(Vector3(13.0f, 11.0f, 4.5f), 3.9f, Crates()).has_value());
    auto wall = Crate;
    wall.Footing = Footing::Wall;
    CHECK_FALSE(Solids::Platform(Vector3(11.0f, 11.0f, 4.5f), 3.9f, std::vector<Solid>{wall}).has_value());
}

// The original's rule: walls are objects without 0x10 and 0x800 (damp_collide_move 0x465e34),
// floors those with 0x100 and without 0x10 (damp_platform_floor 0x41d2c4); 0x800000 plays no
// part. E.g. the ridden snowboard (0x800900) is a floor only.
TEST_CASE("SolidsTests.FlagsDecideWallsAndFloors")
{
    const std::pair<int, Meets> cases[] = {
        {0x0, Meets::Blocked},
        {0x100, Meets::Solid},
        {0x800100, Meets::Solid},
        {0x10, Meets::PassesThrough},
        {0x800110, Meets::PassesThrough},
        {0x800, Meets::PassesThrough},
        {0x900, Meets::StandsOn},
        {0x800900, Meets::StandsOn},
        {0x910, Meets::PassesThrough},
    };
    for (const auto& [flags, expected] : cases)
    {
        CAPTURE(flags);
        MdkObject obj;
        obj.Flags = flags;
        std::vector<Solid> solids;
        if (auto footing = obj.Footing())
        {
            auto owned = Crate;
            owned.Owner = &obj;
            owned.Footing = *footing;
            solids.push_back(owned);
        }

        bool blocked = Solids::Walk(Vector3(5.0f, 11.0f, 3.0f), Vector3(15.0f, 11.0f, 3.0f), Half, solids).has_value();
        bool standsOn = Solids::Platform(Vector3(11.0f, 11.0f, 4.5f), 3.9f, solids).has_value();
        auto meets = static_cast<Meets>((blocked ? static_cast<int>(Meets::Blocked) : 0) | (standsOn ? static_cast<int>(Meets::StandsOn) : 0));
        CHECK(meets == expected);
    }
}
