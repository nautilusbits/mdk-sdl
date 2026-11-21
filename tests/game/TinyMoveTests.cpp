#include <string>

#include <doctest.h>

#include "core/Numerics.h"
#include "formats/Mto.h"
#include "game/collision/ArenaSpace.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Game::Collision;
using Mdk::Tests::GameData;

// Found by the soak test: a walk move of a hundred-thousandth of a unit (Kurt's speed
// fading out) against level 4's MEAT_3 slope threw his feet 5 units away, into the rock, and he
// fell through the level.
namespace
{
    constexpr int Level = 4;
    constexpr float WalkSlide = 0.75f;
    // A move ends at most this far beyond its length (the push off a face).
    constexpr float Tolerance = 0.1f;
}

DATA_TEST_CASE("TinyMoveTests.TinyWalkMoveStaysPut")
{
    auto number = std::to_string(Level);
    auto mto = Formats::Mto::Load(GameData::Data()->PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + "O.MTO"));
    ArenaSpace space;
    space.Add(mto->GetArena("MEAT_3"));
    Vector3 feet(36.463993f, 2648.5732f, -502.07703f);
    Vector3 move(3.6144081E-06f, -1.18592025E-05f, 0.f);

    auto result = space.Move(feet, move, ArenaSpace::Motion::Walk, WalkSlide);

    float distance = Vector3::Distance(feet, result.Feet);
    CHECK(distance >= 0.f);
    CHECK(distance <= move.Length() + Tolerance);
}
