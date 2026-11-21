#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "Asserts.h"
#include "core/DotNet.h"
#include "core/Numerics.h"
#include "formats/Dti.h"
#include "formats/Model.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/Debris.h"
#include "game/scripts/Effects.h"
#include "game/scripts/Fans.h"

using namespace Mdk;
using namespace Mdk::Formats;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;

// Sparks, pieces, sprite effects and fans (debris.gd, effects.gd, fans.gd), without a level.

namespace
{
    const std::string Arena = "TEST";
    constexpr int Seed = 1;
    // Fire sparks' palette colours.
    constexpr int FireBase = 0x30;
    constexpr int FireRange = 0x10;
    // A fan: hotspot 3 lifting from z 0 to 20, rising through it in 2.5 s (type 6).
    constexpr int Hotspot = 3;
    constexpr int Timed = 6;
    constexpr float RiseTime = 2.5f;
    constexpr Vector3 BoxStart(-10.f, -10.f, 0.f);
    constexpr Vector3 BoxEnd(10.f, 10.f, 20.f);
    constexpr float Dt = 1.f / 30.f;

    Debris NewDebris() { return Debris(std::make_shared<Random>(Seed)); }

    Effects NewEffects(int frames = 4)
    {
        Effects effects(std::make_shared<Random>(Seed));
        effects.FrameCount = [frames](const std::string&, const std::string&) { return frames; };
        return effects;
    }

    std::shared_ptr<Fans> NewFans()
    {
        Dti::Record record{7, Hotspot, 0.f, BoxStart, BoxEnd, ""};
        auto fans = std::make_shared<Fans>(std::vector<Dti::ArenaEntry>{Dti::ArenaEntry{Arena, 0.f, {record}}},
            std::make_shared<Random>(Seed));
        fans->Create(Arena, Hotspot, "FAN1", 0, Timed, RiseTime);
        return fans;
    }
}

TEST_CASE("EffectsTests.SparksAreTetrahedraInTheirColours")
{
    auto debris = NewDebris();
    debris.Spark(Arena, Vector3::Zero(), 16, 1.f, FireBase, FireRange);

    CHECK(debris.PieceCount() == 16);
    CHECK(debris.HasPieces(Arena));
    for (auto& piece : debris.Pieces())
    {
        CHECK(piece->IsSpark());
        CHECK(piece->Corners.size() == 12);
        CHECK(piece->ColourBase == FireBase);
        CHECK(piece->ColourRange == FireRange);
        CHECK(piece->Ticks >= Debris::LifeMin);
        CHECK(piece->Ticks <= Debris::LifeMin + 63);

        // Thrown at up to 1 unit per tick across, -0.125 to 1.875 up.
        CHECK(MathF::Abs(piece->Velocity.X) >= 0.f);
        CHECK(MathF::Abs(piece->Velocity.X) <= 1.f);
        CHECK(piece->Velocity.Z >= -0.125f);
        CHECK(piece->Velocity.Z <= 1.875f);
    }
}

TEST_CASE("EffectsTests.StillSparksStandAtTheirPoint")
{
    auto debris = NewDebris();
    Vector3 point(1.f, 2.f, 3.f);
    debris.Spark(Arena, point, 1, 0.5f, FireBase, FireRange, 1.f, Debris::Launch::Still);

    REQUIRE(debris.Pieces().size() == 1);
    auto& spark = debris.Pieces()[0];
    CHECK(spark->Center == point);
    CHECK(spark->Velocity == Vector3::Zero());
    CHECK(spark->TrailEvery == 0);
}

TEST_CASE("EffectsTests.PiecesAreLimited")
{
    auto debris = NewDebris();
    debris.Spark(Arena, Vector3::Zero(), Debris::MaxPieces + 10, 0.5f, FireBase, FireRange);
    CHECK(debris.PieceCount() == Debris::MaxPieces);
}

TEST_CASE("EffectsTests.PiecesFallThenVanish")
{
    auto debris = NewDebris();
    debris.Spark(Arena, Vector3::Zero(), 1, 0.5f, FireBase, FireRange, 1.f, Debris::Launch::Still);
    auto spark = debris.Pieces()[0];
    auto ticks = spark->Ticks;

    debris.Update(1.f);
    CHECK_PRECISION(-Debris::Gravity, spark->Velocity.Z, 4);

    for (int i = 1; i < ticks; i++)
    {
        debris.Update(1.f);
    }

    CHECK(debris.PieceCount() == 0);
}

TEST_CASE("EffectsTests.PiecesBounceOffTheArena")
{
    // A floor at z 0: the piece stops on it, keeps 40% of its speed into it, reversed, and loses 20 ticks.
    auto debris = NewDebris();
    debris.Ray = [](const std::string&, Vector3 from, Vector3 to) -> std::optional<RayHit>
    {
        if (to.Z < 0.f)
        {
            return RayHit{Vector3(to.X, to.Y, 0.f), Vector3::UnitZ(), Arena, 0};
        }

        return std::nullopt;
    };
    debris.Spark(Arena, Vector3(0.f, 0.f, 0.5f), 1, 0.5f, FireBase, FireRange, 1.f, Debris::Launch::Still);
    auto spark = debris.Pieces()[0];
    spark->Velocity = Vector3(0.f, 0.f, -1.f);
    auto ticks = spark->Ticks;

    debris.Update(1.f);

    CHECK_PRECISION(0.4f, spark->Velocity.Z, 4);
    CHECK(spark->Ticks == ticks - 21);
    CHECK(spark->Center.Z == 0.f);
}

TEST_CASE("EffectsTests.FansLiftSparksThroughWhatTheyHit")
{
    // A grate at z 1, hit from either side (as the arena's segment test), above a spark lifted
    // at 1 unit per tick: it bounces off once, then passes from the contact (0x4061d8, 0x421470).
    constexpr float Grate = 1.f;
    auto debris = NewDebris();
    debris.Ray = [](const std::string&, Vector3 from, Vector3 to) -> std::optional<RayHit>
    {
        if ((from.Z - Grate) * (to.Z - Grate) <= 0.f)
        {
            return RayHit{Vector3(to.X, to.Y, Grate), Vector3::UnitZ(), Arena, 0};
        }

        return std::nullopt;
    };
    debris.Updraft = [](const std::string&, Vector3, float, float) { return 30.f; };
    debris.Spark(Arena, Vector3(0.f, 0.f, 0.75f), 1, 0.5f, FireBase, FireRange, 1.f, Debris::Launch::Still);

    for (int i = 0; i < 4; i++)
    {
        debris.Update(1.f);
    }

    INFO("spark at " << debris.Pieces()[0]->Center.Z);
    CHECK(debris.Pieces()[0]->Center.Z > Grate + 1.f);
}

TEST_CASE("EffectsTests.FansLiftPieces")
{
    auto debris = NewDebris();
    debris.Updraft = [](const std::string&, Vector3, float, float) { return 30.f; };
    debris.Spark(Arena, Vector3::Zero(), 1, 0.5f, FireBase, FireRange, 1.f, Debris::Launch::Still);

    debris.Update(1.f);

    // 30 u/s is 1 unit per tick.
    CHECK_PRECISION(1.f, debris.Pieces()[0]->Velocity.Z, 4);
}

TEST_CASE("EffectsTests.ShatteredTrianglesAreSplitSmall")
{
    auto debris = NewDebris();
    Debris::Facet facet{Vector3(0.f, 0.f, 0.f), Vector3(8.f, 0.f, 0.f), Vector3(0.f, 8.f, 0.f), Vector2::Zero(),
        Vector2(64.f, 0.f), Vector2(0.f, 64.f), 0};
    debris.GroupFacets = [facet](const std::string&, int) -> std::optional<Debris::Group>
    {
        return Debris::Group{std::make_shared<const std::vector<std::string>>(std::vector<std::string>{"WALL"}), {facet}};
    };
    constexpr float Size = 2.f;

    debris.Shatter(Arena, 1, 2.f, Size, 1.f, Vector3(0.f, 0.f, -1.f), Vector3::Zero());

    CHECK(debris.PieceCount() > 1);
    for (auto& piece : debris.Pieces())
    {
        CHECK_FALSE(piece->IsSpark());
        auto cross = Vector3::Cross(piece->Corners[1] - piece->Corners[0], piece->Corners[1] - piece->Corners[2]);
        CHECK(cross.LengthSquared() <= Size * Size * 0.25f);
        CHECK(piece->Ticks == 60);

        // Away from the point below the group.
        CHECK(piece->Velocity.Z >= 0.f);
    }
}

TEST_CASE("EffectsTests.BreakUpThrowsTheModelsParts")
{
    auto debris = NewDebris();
    auto part = std::make_shared<Model::Part>();
    part->Name = "ARM";
    part->Vertices = {Vector3::Zero(), Vector3::UnitX(), Vector3::UnitY()};
    part->TriangleIndices = {0, 1, 2};
    part->TriangleMaterials = {0};
    part->TriangleUvs = {Vector2::Zero(), Vector2::UnitX(), Vector2::UnitY()};
    Model model;
    model.Name = "XGD";
    model.Materials = {"SKIN"};
    model.PartList = {part, part};
    auto obj = std::make_shared<MdkObject>();
    obj->Arena = Arena;
    obj->Position = Vector3(5.f, 0.f, 0.f);
    obj->PreviousPosition = Vector3(4.f, 0.f, 0.f);

    debris.BreakUp(*obj, model);

    CHECK(debris.PieceCount() == 2);
    for (auto& p : debris.Pieces())
    {
        CHECK(p->Center == Vector3(5.f, 0.f, 2.f));
        CHECK(p->Velocity.X >= 0.f);
        CHECK(p->Velocity.X <= 2.f);
    }
}

TEST_CASE("EffectsTests.DropsFallAndEnd")
{
    auto effects = NewEffects();
    effects.SpawnDrop(Arena, Vector3::Zero(), Vector3(0.f, 0.f, -0.1f), 5.f);
    REQUIRE(effects.All().size() == 1);
    auto drop = effects.All()[0];

    // 4 frames over 4 ticks each: the first frame first.
    CHECK(drop->Frame() == 0);
    CHECK(drop->Life == 15.f);

    effects.Update(1.f);
    CHECK(drop->Position.Z < 0.f);

    for (int i = 0; i < 15; i++)
    {
        effects.Update(1.f);
    }

    CHECK(effects.All().empty());
}

TEST_CASE("EffectsTests.EffectsNeedTheirTexture")
{
    auto effects = NewEffects(0);
    effects.SpawnBubble(Arena, Vector3::Zero());
    CHECK_FALSE(effects.HasEffects(Arena));
}

TEST_CASE("EffectsTests.EffectsAreLimited")
{
    auto effects = NewEffects();
    for (int i = 0; i < Effects::MaxEffects + 5; i++)
    {
        effects.SpawnTrail(Arena, Vector3::Zero());
    }

    CHECK(effects.All().size() == Effects::MaxEffects);
}

TEST_CASE("EffectsTests.WoundsBleedUntilTheObjectDies")
{
    auto effects = NewEffects();
    auto obj = std::make_shared<MdkObject>();
    obj->Arena = Arena;
    effects.Attach(*obj, 0, 0);
    effects.Attach(*obj, 0, 0);
    CHECK(effects.All().size() == 1);

    for (int i = 0; i < 30; i++)
    {
        effects.Update(1.f);
    }

    auto has = [&](Effects::Kind kind)
    {
        for (auto& e : effects.All())
        {
            if (e->Kind == kind)
            {
                return true;
            }
        }

        return false;
    };
    CHECK(has(Effects::Kind::Drop));

    obj->Dead = true;
    effects.Update(1.f);
    CHECK_FALSE(has(Effects::Kind::Wound));
}

TEST_CASE("EffectsTests.DetachStopsTheWound")
{
    auto effects = NewEffects();
    auto obj = std::make_shared<MdkObject>();
    obj->Arena = Arena;
    effects.Attach(*obj, 1, 0);
    effects.Detach(*obj, 1);
    CHECK(effects.All().empty());
}

TEST_CASE("EffectsTests.BubblesRiseThenPop")
{
    auto effects = NewEffects();
    effects.SpawnBubble(Arena, Vector3::Zero());
    auto bubble = effects.All()[0];
    for (int i = 0; i < 64; i++)
    {
        effects.Update(1.f);
    }

    CHECK(bubble->Kind == Effects::Kind::Pop);
    CHECK(bubble->Position.Z > 0.f);
}

TEST_CASE("EffectsTests.FansLiftWhatIsInTheirBox")
{
    auto fans = NewFans();

    // Strength: 20 units in 2 s (2.5 - 0.5); from rest the speed grows by (10 + 64) × dt; a
    // faster one is halved towards 10.
    auto vz = fans->Query(Arena, Vector3(0.f, 0.f, 5.f), 0.f, Fans::MaskKurt, Dt);
    CHECK_PRECISION((10.f + 64.f) * Dt, vz, 4);
    CHECK_PRECISION(30.f, fans->Query(Arena, Vector3(0.f, 0.f, 5.f), 50.f, Fans::MaskKurt, Dt), 4);

    CHECK(std::isnan(fans->Query(Arena, Vector3(11.f, 0.f, 5.f), 0.f, Fans::MaskKurt, Dt)));
    CHECK(std::isnan(fans->Query(Arena, Vector3(0.f, 0.f, 26.f), 0.f, Fans::MaskKurt, Dt)));
    CHECK(std::isnan(fans->Query("OTHER", Vector3(0.f, 0.f, 5.f), 0.f, Fans::MaskKurt, Dt)));
}

TEST_CASE("EffectsTests.DisabledFansStillLiftObjects")
{
    // fan_enable 0 clears bit 0 (Kurt) of a mask that starts full.
    auto fans = NewFans();
    fans->Enable(Arena, "FAN1", Fans::Power::Off);
    CHECK(std::isnan(fans->Query(Arena, Vector3(0.f, 0.f, 5.f), 0.f, Fans::MaskKurt, Dt)));
    CHECK_FALSE(std::isnan(fans->Query(Arena, Vector3(0.f, 0.f, 5.f), 0.f, Fans::MaskObjects, Dt)));

    fans->Enable(Arena, "FAN1", Fans::Power::On);
    CHECK_FALSE(std::isnan(fans->Query(Arena, Vector3(0.f, 0.f, 5.f), 0.f, Fans::MaskKurt, Dt)));

    fans->Remove(Arena, "FAN1");
    CHECK(fans->Count() == 0);
}

TEST_CASE("EffectsTests.FansLetOutSparksInLiveArenas")
{
    auto fans = NewFans();
    std::vector<Vector3> sparks;
    fans->Spark = [&sparks](const std::string&, Vector3 point) { sparks.push_back(point); };
    for (int i = 0; i < 400; i++)
    {
        fans->Update();
    }

    // About one tick in 8, just above the bottom of the box.
    CHECK(sparks.size() >= 25);
    CHECK(sparks.size() <= 75);
    for (auto& p : sparks)
    {
        CHECK_PRECISION(BoxStart.Z - 0.5f + 0.25f, p.Z, 4);
    }

    sparks.clear();
    fans->IsLive = [](const std::string&) { return false; };
    fans->Update();
    CHECK(sparks.empty());
}
