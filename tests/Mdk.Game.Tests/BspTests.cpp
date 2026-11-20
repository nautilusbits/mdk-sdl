#include <memory>
#include <string>
#include <utility>

#include <doctest.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/Dti.h"
#include "Mdk.Formats/Mto.h"
#include "Mdk.Game/Collision/Bsp.h"
#include "Shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Game::Collision;
using Mdk::Tests::GameData;

// Collisions on real arenas: level 3's start. Its DTI position lies 1.7 under the
// ground there (the Godot port settles Kurt at z 192).

namespace
{
    constexpr int Level = 3;
    // Kurt's boxes (damp_collide_move 0x465e34): vertical sweeps 0.4/0.4/2.5 from feet + 0.01,
    // horizontal ones 0.6/0.6/2.5 from feet + 0.5.
    const Vector3 FallBox(0.4f, 0.4f, 2.5f);
    const Vector3 WalkBox(0.6f, 0.6f, 2.5f);
    constexpr float FallLift = 0.01f;
    constexpr int Iterations = 4;
    constexpr float FallSlide = 0.5f;
    constexpr float WalkSlide = 0.75f;
    constexpr float Tolerance = 0.6f;
    // The ground under level 3's start (the Godot port's Kurt: z 192).
    constexpr float GroundZ = 192.f;
    constexpr float GroundTolerance = 1.f;

    std::pair<std::shared_ptr<Bsp>, Vector3> StartArena()
    {
        auto& data = *GameData::Data();
        auto number = std::to_string(Level);
        auto dti = Formats::Dti::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + ".DTI"));
        auto mto = Formats::Mto::Load(data.PathOf("TRAVERSE/LEVEL" + number + "/LEVEL" + number + "O.MTO"));
        auto arena = mto->GetArena(dti->Arenas[dti->StartArena].Name);
        return {std::make_shared<Bsp>(arena), dti->StartPosition};
    }
}

DATA_TEST_CASE("BspTests.SegmentFindsTheFloorUnderTheStart")
{
    auto [bsp, start] = StartArena();
    Vector3 point;
    auto triangle = bsp->Segment(start + Vector3(0.f, 0.f, 5.f), start - Vector3(0.f, 0.f, 20.f), Bsp::SegmentMode::Floor, point);
    CHECK(Bsp::None != triangle);
    CHECK(point.Z >= GroundZ - GroundTolerance);
    CHECK(point.Z <= GroundZ + GroundTolerance);
}

DATA_TEST_CASE("BspTests.FallingBoxLandsOnTheFloor")
{
    auto [bsp, start] = StartArena();
    auto from = start + Vector3(0.f, 0.f, 10.f + FallBox.Z + FallLift);
    Vector3 end;
    int node = 0;
    auto triangle = bsp->SweepBox(from, from - Vector3(0.f, 0.f, 30.f), FallBox, Iterations, FallSlide, end, node);
    CHECK(Bsp::None != triangle);
    auto feet = end.Z - FallBox.Z - FallLift;
    Vector3 ground;
    bsp->Segment(end, end - Vector3(0.f, 0.f, 20.f), Bsp::SegmentMode::Floor, ground);
    CHECK(feet >= ground.Z - Tolerance);
    CHECK(feet <= ground.Z + Tolerance);
}

DATA_TEST_CASE("BspTests.WalkingFarIsStoppedByWalls")
{
    auto [bsp, start] = StartArena();
    constexpr float Far = 5000.f;
    auto from = start + Vector3(0.f, 0.f, WalkBox.Z + 0.5f);
    for (auto direction : {Vector3::UnitX(), -Vector3::UnitX(), Vector3::UnitY(), -Vector3::UnitY()})
    {
        CAPTURE(direction.X);
        CAPTURE(direction.Y);
        Vector3 end;
        int node = 0;
        bsp->SweepBox(from, from + direction * Far, WalkBox, Iterations, WalkSlide, end, node);
        CHECK(Vector3::Distance(from, end) < Far);
    }
}

DATA_TEST_CASE("BspTests.NotSolidTrianglesAreSkipped")
{
    auto [bsp, start] = StartArena();
    auto a = start + Vector3(0.f, 0.f, 5.f);
    auto b = start - Vector3(0.f, 0.f, 20.f);
    Vector3 point;
    auto triangle = bsp->Segment(a, b, Bsp::SegmentMode::Floor, point);
    REQUIRE(triangle != Bsp::None);
    bsp->Arena->TriangleFlags[triangle] |= Bsp::NotSolid;
    CHECK(triangle != bsp->Segment(a, b, Bsp::SegmentMode::Floor, point));
}
