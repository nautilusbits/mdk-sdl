#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <doctest.h>

#include "Asserts.h"
#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Drawing.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/Fall/FallGround.h"
#include "Mdk.Game/Fall/FallMissile.h"
#include "Mdk.Game/Fall/FallSim.h"
#include "Mdk.Game/Fall/FallView.h"
#include "Mdk.Game/Kurt/Inventory.h"

using namespace Mdk;
using namespace Mdk::Game::Fall;
using namespace Mdk::Game::Kurt;
using Mdk::Engine::Render::Vertex;

// The fall before a level (fall.gd, fall_missile.gd): steering, the camera, the ground's
// mapping and track, the missiles, the pickups and the end.

namespace
{
    constexpr float Step = 1.f / 60.f;
    const FallSim::Animation Loop{1.f, 30};
    const std::vector<std::string> FirstPickups = {"SW_HOME", "SW_GATT", "SW_HOME"};

    std::shared_ptr<FallSim> NewFall(int index = 0, Difficulty difficulty = Difficulty::Normal, int seed = 1)
    {
        return std::make_shared<FallSim>(FallSim::Setup{index, difficulty, FirstPickups, Loop, Loop, Loop},
            std::make_shared<Random>(seed));
    }

    // Runs the sim for seconds, steering by steer.
    void Run(FallSim& sim, float seconds, const std::function<Vector2(FallSim&)>& steer = nullptr)
    {
        for (float t = 0.f; t < seconds && sim.State == FallSim::Outcome::Running; t += Step)
        {
            sim.Update(Step, steer ? steer(sim) : Vector2::Zero());
        }
    }
}

TEST_CASE("FallTests.SteeringAcceleratesBrakesAndTurnsBack")
{
    // A key: a step a tick up to the maximum.
    CHECK_PRECISION(FallSim::SteerStep, FallSim::Steer(0.f, 1, 1.f / FallSim::TicksPerSecond), 3);
    CHECK_PRECISION(FallSim::SteerMax, FallSim::Steer(FallSim::SteerMax, 1, 1.f), 3);

    // The opposite key resets the speed to one step; no key brakes to 0.
    CHECK_PRECISION(-FallSim::SteerStep, FallSim::Steer(50.f, -1, Step), 3);
    CHECK(FallSim::Steer(3.f, 0, 1.f / FallSim::TicksPerSecond) == 0.f);
    CHECK_PRECISION(50.f - FallSim::SteerStep, FallSim::Steer(50.f, 0, 1.f / FallSim::TicksPerSecond), 3);
}

TEST_CASE("FallTests.DifficultyAsGodot")
{
    struct Row
    {
        int Index;
        Difficulty Level;
        float Beam;
        int Missiles;
        float Spread;
        int MissileGap;
        int RadarGap;
    };
    const Row rows[] = {
        {0, Difficulty::Normal, 117.65f, 2, 6.5f, 32, 63},
        {4, Difficulty::Easy, 164.71f, 2, 3.5f, 28, 51},
        {4, Difficulty::Hard, 274.51f, 4, 1.5f, 12, 27},
    };

    for (auto& row : rows)
    {
        CAPTURE(row.Index);
        CAPTURE(static_cast<int>(row.Level));
        auto sim = NewFall(row.Index, row.Level);
        CHECK_PRECISION(row.Beam, sim->BeamSpeed, 2);
        CHECK(sim->MissilesPerDetection == row.Missiles);
        CHECK_PRECISION(row.Spread, sim->Spread, 3);
        CHECK(sim->MissileGap == row.MissileGap);
        CHECK(sim->RadarGap == row.RadarGap);
    }
}

TEST_CASE("FallTests.IntroLastsFiveSecondsThenKurtFalls")
{
    auto sim = NewFall();
    Run(*sim, 4.9f);
    CHECK(sim->InIntro());
    CHECK(sim->KurtVisible);

    Run(*sim, 0.2f);
    CHECK_FALSE(sim->InIntro());
    Run(*sim, 1.f);
    CHECK_PRECISION(FallSim::KurtStartZ - FallSim::KurtSpeed * sim->Time, sim->KurtPosition.Z, 0);
    CHECK_PRECISION(sim->KurtPosition.Z + 10.f, sim->CameraPosition.Z, 1);
}

TEST_CASE("FallTests.KurtStaysWithinLimits")
{
    auto sim = NewFall();
    Run(*sim, 5.f + 3.f, [](FallSim&) { return Vector2(-1.f, 1.f); });
    CHECK_PRECISION(-FallSim::Limit().X, sim->KurtPosition.X, 3);
    CHECK_PRECISION(FallSim::Limit().Y, sim->KurtPosition.Y, 3);
}

TEST_CASE("FallTests.EndsAt33SecondsPulledBackWithTheCameraStopped")
{
    auto sim = NewFall(0, Difficulty::Normal, 3);
    Run(*sim, 5.f + 29.f, [](FallSim&) { return Vector2::One(); });
    Run(*sim, 10.f);
    if (sim->State == FallSim::Outcome::GameOver)
    {
        return;
    }

    CHECK(sim->State == FallSim::Outcome::Landed);
    CHECK(sim->Time >= FallSim::EndTime);
    CHECK(sim->Time <= FallSim::EndTime + 0.1f);
    CHECK(sim->KurtPosition.X >= -0.2f);
    CHECK(sim->KurtPosition.X <= 0.2f);
    CHECK_PRECISION(3213.7f, sim->CameraPosition.Z, 0);
    CHECK(sim->Wind == 0);
}

TEST_CASE("FallTests.SteeringToThePickupsTakesThem")
{
    auto sim = NewFall();

    // Kurt steers over the next pickup rising to him (under its chute, below him).
    auto towards = [](FallSim& sim)
    {
        std::shared_ptr<FallSim::Pickup> highest;
        for (auto& p : sim.Pickups())
        {
            if (!p->Chute || p->Position.Z >= sim.KurtPosition.Z)
            {
                continue;
            }

            if (highest == nullptr || p->Position.Z > highest->Position.Z)
            {
                highest = p;
            }
        }

        if (highest == nullptr)
        {
            return Vector2::Zero();
        }

        auto target = highest->Position;
        Vector2 d(target.X - sim.KurtPosition.X, target.Y - sim.KurtPosition.Y);
        return Vector2(MathF::Abs(d.X) < 1.f ? 0.f : static_cast<float>(MathF::Sign(d.X)),
            MathF::Abs(d.Y) < 1.f ? 0.f : static_cast<float>(MathF::Sign(d.Y)));
    };

    Run(*sim, 5.f + 29.f, towards);
    CHECK_FALSE(sim->Collected().empty());
    for (auto& name : sim->Collected())
    {
        CHECK(std::find(FirstPickups.begin(), FirstPickups.end(), name) != FirstPickups.end());
    }

    CHECK_FALSE(sim->Inventory->Slots().empty());
}

TEST_CASE("FallTests.BoxCrossings")
{
    Vector3 kurt(0.f, 0.f, 100.f);
    CHECK(FallSim::CrossesBox(kurt, Vector3(0.f, 0.f, 90.f), Vector3(1.f, 1.f, 102.f)));
    CHECK(FallSim::CrossesBox(kurt, Vector3(0.f, 0.f, 90.f), Vector3(0.f, 0.f, 110.f)));
    CHECK_FALSE(FallSim::CrossesBox(kurt, Vector3(5.f, 0.f, 90.f), Vector3(5.f, 0.f, 110.f)));
    CHECK_FALSE(FallSim::CrossesBox(kurt, Vector3(0.f, 0.f, 80.f), Vector3(0.f, 0.f, 94.f)));
}

TEST_CASE("FallTests.MissileFliesFreeThenHomesThenPasses")
{
    Random random(1);
    FallMissile missile(0.f, random);
    Vector3 kurt(0.f, 0.f, 300.f);
    std::vector<FallMissile::Event> events;
    auto gone = [&] { return std::find(events.begin(), events.end(), FallMissile::Event::Gone) != events.end(); };
    for (int tick = 0; tick < 600 && !gone(); tick++)
    {
        auto happened = missile.Update(1.f / FallSim::TicksPerSecond, 1, kurt);
        if (happened != FallMissile::Event::None)
        {
            events.push_back(happened);
        }
    }

    CHECK(events == std::vector<FallMissile::Event>{FallMissile::Event::Passed, FallMissile::Event::Gone});
    CHECK(missile.Trail().size() == FallMissile::TrailPoints);
}

TEST_CASE("FallTests.MissilesHomeAsFastAt120FpsAsAt30")
{
    // The homing blends once per 1/30 s tick (0.8 v + 0.2 aim): frames between ticks don't stiffen it.
    // Free until tick 59, then 3 ticks of homing: mid-turn, where per-frame blending turned the
    // 120 fps missile about 76 degrees off the 30 fps one (about 12 with the blend per tick).
    constexpr int Ticks = 62;
    constexpr int Frames = 4;
    constexpr float MaxDegrees = 20.f;
    Vector3 kurt(0.f, 0.f, 2000.f);
    Random ticksRandom(1);
    Random framesRandom(1);
    FallMissile atTicks(0.f, ticksRandom);
    FallMissile atFrames(0.f, framesRandom);
    for (int tick = 0; tick < Ticks; tick++)
    {
        atTicks.Update(1.f / FallSim::TicksPerSecond, 1, kurt);
        for (int frame = 0; frame < Frames; frame++)
        {
            atFrames.Update(1.f / (FallSim::TicksPerSecond * Frames), frame == 0 ? 1 : 0, kurt);
        }
    }

    auto cos = Vector3::Dot(Vector3::Normalize(atTicks.Velocity), Vector3::Normalize(atFrames.Velocity));
    CHECK(MathF::Acos(Math::Clamp(cos, -1.f, 1.f)) * 180.f / MathF::PI < MaxDegrees);
}

TEST_CASE("FallTests.AHitExplodesOnKurt")
{
    // The explosion goes off at Kurt's position, not where the missile met his box (up to 4
    // units to his side), and follows him down.
    auto sim = NewFall();
    Run(*sim, 5.f);
    while (sim->Explosions().empty() && sim->Time < FallSim::SteerTime)
    {
        sim->Update(Step, Vector2::Zero());
    }

    REQUIRE(sim->Explosions().size() == 1);
    CHECK(sim->Explosions()[0]->Position == sim->KurtPosition);
}

TEST_CASE("FallTests.RadarBeamReachesItsSpot")
{
    // Rings 1-3 halve the way to the spot (0x412af8), ring 4 is the spot itself.
    Vector3 basePoint(-40.f, 0.f, 0.f);
    Vector3 spot(0.f, 0.f, 400.f);
    CHECK(FallView::RadarRing(basePoint, spot, 10.f, 0) == std::pair(Vector3(-20.f, 0.f, 200.f), 5.f));
    CHECK(FallView::RadarRing(basePoint, spot, 10.f, 2) == std::pair(Vector3(-5.f, 0.f, 350.f), 8.75f));
    CHECK(FallView::RadarRing(basePoint, spot, 10.f, 3) == std::pair(spot, 10.f));
}

TEST_CASE("FallTests.ModelsAtTheCameraAreNotDrawn")
{
    // A missile past Kurt (5 units above him, 5 below the camera) would fill the view.
    Vector3 camera(0.f, 0.f, 1000.f);
    CHECK_FALSE(FallView::NearCamera(Vector3(0.f, 0.f, 990.f), camera));
    CHECK(FallView::NearCamera(Vector3(0.f, 0.f, 996.f), camera));
    CHECK(FallView::NearCamera(Vector3(0.f, 0.f, 1003.f), camera));
}

TEST_CASE("FallTests.TrackWidens")
{
    const std::pair<int, int> rows[] = {{0, 16}, {3, 18}, {23, 31}, {24, 32}};
    for (auto [row, width] : rows)
    {
        CAPTURE(row);
        CHECK(FallGround::TrackWidth(row) == width);
    }
}

TEST_CASE("FallTests.GroundMapping")
{
    // At the start a texel is a pixel: the 480-wide view shows columns 272-752 around row 824.
    auto [screen, texels] = FallGround::Visible(Vector3(0.f, 0.f, FallGround::UnitHeight), FallGround::CentreRow(0.f),
        RectangleF(0.f, 0.f, 480.f, 360.f));
    CHECK(screen == RectangleF(0.f, 0.f, 480.f, 360.f));
    CHECK(texels == RectangleF(272.f, 644.f, 480.f, 360.f));
    CHECK_PRECISION(200.f, FallGround::CentreRow(FallSim::EndTime), 3);
}

TEST_CASE("FallTests.GroundFillsATallView")
{
    // A near-square foldable sees 480 x 522 view pixels around the same centre: the ground covers
    // them, but for the texture's end (at the start only 200 rows are below the centre).
    RectangleF view(0.f, -81.f, 480.f, 522.f);
    Vector3 camera(0.f, 0.f, FallGround::UnitHeight);
    auto [screen, texels] = FallGround::Visible(camera, 600.f, view);
    CHECK(screen == view);
    CHECK(texels == RectangleF(272.f, 339.f, 480.f, 522.f));
    CHECK(FallGround::Visible(camera, FallGround::CentreRow(0.f), view).second.Bottom() == 1024.f);
}

TEST_CASE("FallTests.TrailsSwellBehindTheMissileThenTaper")
{
    // The original's trail (0x439454): its widths by a point's age in ticks (0x491d64, then linear).
    const std::pair<int, float> rows[] = {{0, 1.f}, {1, 1.25f}, {2, 1.2f}, {5, 1.f}, {19, 0.5f}, {31, 1.f / 26.f}};
    for (auto [age, width] : rows)
    {
        CAPTURE(age);
        CHECK_PRECISION(width, FallView::TrailWidth(age), 4);
    }
}

TEST_CASE("FallTests.TrailsTurnYellowAndOpaqueOverTheirLastEightSegments")
{
    // 0x410874's translucent table: white at 215, then blue 255 down to 31, alpha up to 250.
    constexpr int Count = 32;
    CHECK(FallView::TrailColour(1, Count) == Vertex::Rgba(255, 255, 255, 215));
    CHECK(FallView::TrailColour(23, Count) == Vertex::Rgba(255, 255, 255, 215));
    CHECK(FallView::TrailColour(24, Count) == Vertex::Rgba(255, 255, 255, 215));
    CHECK(FallView::TrailColour(25, Count) == Vertex::Rgba(255, 255, 223, 220));
    CHECK(FallView::TrailColour(31, Count) == Vertex::Rgba(255, 255, 31, 250));
}
