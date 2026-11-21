#include <algorithm>
#include <memory>
#include <tuple>
#include <vector>

#include <doctest.h>

#include "core/Numerics.h"
#include "game/kurt/Inventory.h"
#include "game/stream/Flight.h"
#include "game/stream/StreamTube.h"
#include "game/stream/TubeBasis.h"
#include "game/stream/WatcomRandom.h"
#include "Asserts.h"

using namespace Mdk;
using namespace Mdk::Game::Stream;
using Mdk::Game::Kurt::Difficulty;

// The stream (stream.gd, stream_tube.gd): the tube's generator against the Godot port's
// numbers (its StreamTube with Watcom's rand(), seed 1), steering and the walls.
namespace
{
    constexpr float Tolerance = 2e-3f;
    constexpr float Step = 1.f / 60.f;
    const Clip Loop{10, 1.f};

    std::shared_ptr<StreamTube> Tube(int index, Difficulty difficulty, StreamTube::Kind kind, uint seed = WatcomRandom::DefaultSeed)
    {
        return std::make_shared<StreamTube>(index, difficulty, kind, std::make_shared<WatcomRandom>(seed));
    }

    std::shared_ptr<Flight> FlightOf(StreamTube::Kind kind, int health)
    {
        return std::make_shared<Flight>(Tube(0, Difficulty::Normal, kind), kind, Difficulty::Normal, std::make_shared<WatcomRandom>(), health, Loop, Loop, Loop);
    }

    void Near(Vector3 expected, Vector3 actual, float tolerance = Tolerance)
    {
        INFO("expected ", expected.X, ", ", expected.Y, ", ", expected.Z, ", got ", actual.X, ", ", actual.Y, ", ", actual.Z);
        CHECK(Vector3::Distance(expected, actual) < tolerance);
    }

    bool Contains(const std::vector<FlightEvent>& events, FlightEvent e)
    {
        return std::find(events.begin(), events.end(), e) != events.end();
    }
}

TEST_CASE("StreamTests.RandIsWatcoms")
{
    WatcomRandom random;
    int a = random.Next();
    int b = random.Next();
    int c = random.Next();
    CHECK(std::vector<int>{a, b, c} == std::vector<int>{16838, 5758, 10113});
}

TEST_CASE("StreamTests.LimitsFollowLevelAndDifficulty")
{
    struct Row
    {
        int Index;
        Difficulty Difficulty;
        float Turn;
        float Min;
        float Max;
    };

    const Row rows[] = {
        {0, Difficulty::Normal, 8.f, 10.f, 17.f},
        {2, Difficulty::Hard, 12.f, 9.f, 12.f},
        {4, Difficulty::Normal, 12.f, 8.f, 13.f},
        {3, Difficulty::Easy, 9.f, 9.f, 16.f},
    };
    for (const auto& row : rows)
    {
        CAPTURE(row.Index);
        auto tube = Tube(row.Index, row.Difficulty, StreamTube::Kind::Normal);
        CHECK(std::tuple(row.Turn, row.Min, row.Max) == std::tuple(tube->MaxTurn, tube->MinRadius, tube->MaxRadius));
    }
}

// A vertex takes its ramp colour and its level's alpha (of 256), blended per vertex.
TEST_CASE("StreamTests.VertexTakesRampAndLevel")
{
    struct Row
    {
        int Colour;
        int Level;
        int R;
        int G;
        int B;
        int A;
    };

    const Row rows[] = {
        {0, 0, 222, 206, 90, 0x5A},
        {10, 2, 0x87, 0x68, 0x1a, 0x50},
        {63, 5, 209, 186, 79, 0x0F},
    };
    for (const auto& row : rows)
    {
        CAPTURE(row.Colour);
        auto tube = Tube(0, Difficulty::Normal, StreamTube::Kind::Normal);
        auto rgba = tube->Rgba(TubeVertex{Vector3::Zero(), row.Colour, row.Level});
        CHECK(std::tuple(row.R, row.G, row.B, row.A) == std::tuple<int, int, int, int>(rgba.R, rgba.G, rgba.B, rgba.A));
    }
}

// godot-mdk, StreamTube.new(0, 1, NORMAL) with Watcom's rand().
TEST_CASE("StreamTests.TubeMatchesGodot")
{
    auto tube = Tube(0, Difficulty::Normal, StreamTube::Kind::Normal);
    const auto& ramp = tube->Ramp();
    CHECK(std::tuple(222, 206, 90) == std::tuple<int, int, int>(ramp[0].R, ramp[0].G, ramp[0].B));
    CHECK(std::tuple(0x87, 0x68, 0x1a) == std::tuple<int, int, int>(ramp[10].R, ramp[10].G, ramp[10].B));
    CHECK(std::tuple(0, 31) == std::tuple(tube->Tail, tube->Head));

    auto spawns = tube->TakeSpawns();
    REQUIRE(spawns.size() == 27);
    CHECK(spawns[0].T == 0.f);
    CHECK_NEAR(-0.6729f, spawns[0].X, 1e-4f);
    CHECK_NEAR(7.3076f, spawns[0].Size, 1e-4f);
    CHECK_NEAR(-6.9166f, spawns[0].Speed, 1e-4f);

    Near(Vector3(0.f, 10.f, 0.f), tube->Origin(1));
    Near(Vector3(-0.036f, 9.973f, 9.425f), tube->Point(1, 0));
    Near(Vector3(-3.523f, 98.642f, 14.672f), tube->Point(10, 0));
    Near(Vector3(-79.963f, 249.386f, 101.282f), tube->Origin(30));
    Near(Vector3(-83.717f, 257.434f, 93.861f), tube->Point(30, 5));
    std::vector<int> colours;
    for (int j = 0; j < 16; j++)
    {
        colours.push_back(tube->Colour(5, j));
    }

    CHECK(colours == std::vector<int>{40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 49, 48, 47, 46, 45, 44});
    auto [normal, distance] = tube->Plane(5, 0);
    Near(Vector3(-0.521f, -0.089f, -0.849f), normal);
    CHECK_NEAR(15.546f, distance, Tolerance);

    Near(Vector3(-6.703f, 104.371f, 5.385f), tube->Centre(10.5f));
    Near(Vector3(-5.318f, 104.421f, 7.140f), tube->Place(10.5f, 1.f, 2.f));
    auto frame = tube->Frame(10.5f, Vector3::UnitZ());
    Near(Vector3(0.976f, 0.216f, 0.f), frame.X);
    Near(Vector3(0.032f, -0.145f, 0.989f), frame.Z);
    CHECK(tube->WallHit(tube->Centre(10.5f), 10.5f, 1.5f) == -1.f);
    CHECK_NEAR(0.0080f, tube->WallHit(tube->Centre(10.5f) + frame.X * 9.5f, 10.5f, 1.5f), 1e-3f);

    int lights = 0;
    for (int i = 0; i < 200; i++)
    {
        tube->Advance();
        lights += static_cast<int>(tube->TakeSpawns().size());
    }

    CHECK(std::tuple(200, 231, 142) == std::tuple(tube->Tail, tube->Head, lights));
    Near(Vector3(-466.086f, 254.029f, -304.175f), tube->Origin(230), 0.05f);
    Near(Vector3(0.962f, -7.344f, 5.093f), tube->Angles(), 0.01f);
    CHECK_NEAR(16.0577f, tube->Radius(), 1e-3f);
}

// godot-mdk, StreamTube.new(4, 1, GUNTER): it straightens and ends at the planet.
TEST_CASE("StreamTests.GunterTubeEndsAtPlanet")
{
    auto tube = Tube(4, Difficulty::Normal, StreamTube::Kind::Gunter);
    tube->TakeSpawns();
    int lights = 0;
    float planet = -1.f;
    for (int i = 0; i < 200; i++)
    {
        tube->Advance();
        for (const auto& spawn : tube->TakeSpawns())
        {
            if (spawn.Kind == StreamTube::SpawnKind::Planet)
            {
                planet = spawn.T;
                continue;
            }

            lights++;
        }
    }

    CHECK(std::tuple(187, 115, 184.f) == std::tuple(tube->Head, lights, planet));
    CHECK(tube->Angles() == Vector3::Zero());
    Near(Vector3(-316.770f, 205.873f, -2.547f), tube->Origin(186), 0.05f);
}

TEST_CASE("StreamTests.SameSeedSameTube")
{
    auto a = Tube(1, Difficulty::Normal, StreamTube::Kind::Normal, 7);
    auto b = Tube(1, Difficulty::Normal, StreamTube::Kind::Normal, 7);
    auto c = Tube(1, Difficulty::Normal, StreamTube::Kind::Normal, 8);
    CHECK(a->Point(30, 3) == b->Point(30, 3));
    CHECK(a->Point(30, 3) != c->Point(30, 3));
}

TEST_CASE("StreamTests.WallPlanesFaceTheAxis")
{
    auto tube = Tube(0, Difficulty::Normal, StreamTube::Kind::Normal);
    for (int n = 0; n < 29; n++)
    {
        auto centre = tube->Centre(n + 0.5f);
        for (int i = 0; i < StreamTube::Points * 2; i++)
        {
            auto [normal, distance] = tube->Plane(n, i);
            INFO("segment ", n, " plane ", i);
            CHECK(Vector3::Dot(normal, centre) + distance > 0.f);
        }
    }
}

TEST_CASE("StreamTests.SteeringTurnsUpTo45AndBack")
{
    // 180°/s: a quarter of a second to full turn, before Kurt reaches a wall.
    auto flight = FlightOf(StreamTube::Kind::Normal, 100);
    for (int i = 0; i < 15; i++)
    {
        flight->Update(Step, Steering{1, 1});
    }

    CHECK_NEAR(Flight::NeutralYaw - 45.f, flight->Kurt->Yaw, 1e-3f);
    CHECK_NEAR(45.f, flight->Pitch, 1e-3f);

    for (int i = 0; i < 15; i++)
    {
        flight->Update(Step, Steering{});
    }

    CHECK(flight->Kurt->Yaw == Flight::NeutralYaw);
    CHECK(flight->Pitch == 0.f);
}

TEST_CASE("StreamTests.WallHitPushesBackHurtsAndSlows")
{
    auto flight = FlightOf(StreamTube::Kind::Normal, 100);
    flight->Update(Step, Steering{});
    flight->Kurt->X = 30.f;
    flight->Update(Step, Steering{});

    CHECK(Contains(flight->Events, FlightEvent::WallHit));
    CHECK(flight->Health >= 97);
    CHECK(flight->Health <= 98);
    CHECK_NEAR(5.4f, flight->Kurt->Speed, 1e-4f);
    CHECK(flight->Kurt->X < 30.f);
    CHECK_NEAR(Flight::NeutralYaw + 45.f, flight->Kurt->Yaw, 1.f);
}

TEST_CASE("StreamTests.NormalTubeKeepsOneHealthAndBonesComes")
{
    // 3 health less a hit of 2 or 3 leaves 1.
    auto flight = FlightOf(StreamTube::Kind::Normal, 3);
    flight->Update(Step, Steering{});
    flight->Kurt->X = 30.f;
    flight->Update(Step, Steering{});
    CHECK(flight->Health == Flight::LowHealth);

    flight->Update(Step, Steering{});
    CHECK(Contains(flight->Events, FlightEvent::BonesCame));
    CHECK(flight->Bones != nullptr);
}

TEST_CASE("StreamTests.GunterTubeKills")
{
    auto flight = FlightOf(StreamTube::Kind::Gunter, Flight::LowHealth);
    flight->Update(Step, Steering{});
    flight->Kurt->X = 30.f;
    flight->Update(Step, Steering{});
    CHECK(Contains(flight->Events, FlightEvent::KurtDied));

    while (!flight->Ended)
    {
        flight->Update(Step, Steering{});
    }

    CHECK(flight->Outcome() == Outcome::Died);
}

// Gunter (0x435a34) takes the tube's frame unturned (0x4366f4), head first down the
// stream, and plays his loop.
TEST_CASE("StreamTests.GunterFollowsTheTube")
{
    auto flight = FlightOf(StreamTube::Kind::Gunter, 100);
    for (int i = 0; i < 30; i++)
    {
        flight->Update(Step, Steering{});
    }

    REQUIRE(flight->Other != nullptr);
    const auto& gunter = *flight->Other;
    auto transform = flight->Transform(gunter);
    auto frame = flight->Tube()->Frame(gunter.T, flight->Up());
    Near(frame.Y, Vector3(transform.M21, transform.M22, transform.M23));
    Near(frame.Z, Vector3(transform.M31, transform.M32, transform.M33));
    CHECK(flight->OtherFrame(gunter) != 0);
}

TEST_CASE("StreamTests.BonesComesAfterSegment177")
{
    auto flight = FlightOf(StreamTube::Kind::Normal, 1000);
    while (flight->Bones == nullptr)
    {
        flight->Update(Step, Steering{});
    }

    CHECK(flight->Tube()->Tail == Flight::RescueSegment + 1);
    while (!flight->Ended)
    {
        flight->Update(Step, Steering{});
    }

    CHECK(flight->Outcome() == Outcome::Survived);
}
