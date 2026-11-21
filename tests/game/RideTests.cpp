#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <doctest.h>

#include "core/Numerics.h"
#include "engine/platform/Input.h"
#include "formats/Arena.h"
#include "game/level/LevelData.h"
#include "game/scripts/Bomber.h"
#include "game/scripts/EndLevel.h"
#include "game/scripts/Snowboard.h"
#include "Asserts.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;

// The rides' physics (snowboard.gd, bomber.gd; godot-mdk docs/gameplay.md "The snowboard",
// "The XE bomber") and the end of a level's timing (end_level.gd).
namespace
{
    constexpr float Tick = 1.f;
    // A key turns the board by 4°/tick (0x5014ec = ±1 × 4).
    constexpr float KeyTurn = 4.f;
    // A 60 Hz step: half a tick.
    constexpr float HalfTick = 0.5f;

    // Whether a triangle has the NONE material.
    bool IsNone(const Formats::Arena& arena, int triangle)
    {
        int m = arena.TriangleMaterials[triangle];
        return m >= 0 && arena.Materials[m] == "NONE";
    }
}

TEST_CASE("RideTests.SteeringRampsUpOnTheGroundAndSnapsBack")
{
    // Right turns lower S: 4°/tick × (ticks × 2 / 30) during the first 15 ticks on the ground.
    float turnTicks = 0.f;
    float steer = Snowboard::Steer(0.f, KeyTurn, true, turnTicks, Tick);
    CHECK_PRECISION(-4.f * 2.f / 30.f, steer, 4);

    // Full rate at once in the air, clamped to ±30.
    turnTicks = 0.f;
    steer = 0.f;
    for (int i = 0; i < 20; i++)
    {
        steer = Snowboard::Steer(steer, KeyTurn, false, turnTicks, Tick);
    }

    CHECK(-30.f == steer);

    // The other way snaps it straight; no key brings it back by 3°/tick.
    CHECK(0.f == Snowboard::Steer(-30.f, -KeyTurn, true, turnTicks, Tick));
    CHECK(-27.f == Snowboard::Steer(-30.f, 0.f, true, turnTicks, Tick));
}

// Found by playtest: only the arrows steered the board. The original's turn input
// (0x5014ec, input_read_axes 0x408334) is the larger of the turn and strafe keys, × 4.
TEST_CASE("RideTests.TurnAndStrafeKeysSteer")
{
    const std::pair<Key, float> cases[] = {
        {Key::TurnRight, KeyTurn},
        {Key::TurnLeft, -KeyTurn},
        {Key::StrafeRight, KeyTurn},
        {Key::StrafeLeft, -KeyTurn},
    };
    for (const auto& [key, expected] : cases)
    {
        CAPTURE(static_cast<int>(key));
        Input input;
        input.Hold(key, Input::State::Down);

        CHECK(expected == Snowboard::TurnInput(input, HalfTick));
    }
}

// The mouse steers too: 4 × clamp(dx / dt, ±4), dx in the original's units (its walk
// turns 3° a unit, the port 0.15° a mouse unit); it wins over the keys.
TEST_CASE("RideTests.TheMouseSteersTheBoard")
{
    const std::pair<float, float> cases[] = {
        {10.f, KeyTurn},
        {-10.f, -KeyTurn},
        {1000.f, 4.f * KeyTurn},
    };
    for (const auto& [mouse, expected] : cases)
    {
        CAPTURE(mouse);
        Input input;
        input.Hold(Key::TurnLeft, Input::State::Down);
        input.AddMouseMotion(mouse, 0.f);

        CHECK_PRECISION(expected, Snowboard::TurnInput(input, HalfTick), 4);
    }
}

TEST_CASE("RideTests.SpeedFollowsTheKeys")
{
    struct Case
    {
        float Forward;
        float Speed;
        float Expected;
    };
    const Case cases[] = {
        {1.f, 1.f, 1.05f},
        {1.f, 2.49f, 2.5f},
        {-1.f, 2.f, 1.95f},
        {-1.f, 1.16667f, 1.16667f},
        {0.f, 1.f, 1.05f},
        {0.f, 2.f, 1.9972222f},
    };
    for (const auto& c : cases)
    {
        CAPTURE(c.Forward);
        CAPTURE(c.Speed);
        CHECK_PRECISION(c.Expected, Snowboard::Accelerate(c.Speed, c.Forward, Tick), 4);
    }
}

TEST_CASE("RideTests.SlopesSpeedUpAndSlowDown")
{
    // Nose down 30° (pitch 330): +0.0333 a tick, up to 2.6667.
    CHECK_PRECISION(1.5f + 0.5f * 0.066667f, Snowboard::SlopeSpeed(1.5f, 330.f, Tick), 4);
    CHECK_PRECISION(2.66667f, Snowboard::SlopeSpeed(2.66f, 330.f, Tick), 4);
    // Nose up: down to 1.1667.
    CHECK_PRECISION(1.16667f, Snowboard::SlopeSpeed(1.17f, 30.f, Tick), 4);
}

TEST_CASE("RideTests.JumpPivotDipsThenComesBack")
{
    CHECK_PRECISION(4.5f - 3 * 0.2f, Snowboard::JumpPivot(3.5f, 4.5f, 0.f), 4);
    CHECK_PRECISION(4.0f, Snowboard::JumpPivot(6.f, 3.5f, 0.5f), 4);
    CHECK_PRECISION(4.5f, Snowboard::JumpPivot(6.f, 4.4f, 1.f), 4);
}

TEST_CASE("RideTests.CursorAcceleratesBrakesAndReverses")
{
    // 1/3 px/tick² up to 10 px/tick.
    float speed = 0.f;
    for (int i = 0; i < 40; i++)
    {
        speed = Bomber::CursorStep(speed, 1.f, Tick);
    }

    CHECK_PRECISION(10.f, speed, 4);
    // The other way starts over; no key brakes by 2/3.
    CHECK_PRECISION(-1.f / 3.f, Bomber::CursorStep(10.f, -1.f, Tick), 4);
    CHECK_PRECISION(10.f - 2.f / 3.f, Bomber::CursorStep(10.f, 0.f, Tick), 4);
}

TEST_CASE("RideTests.AimLooksDownThroughTheCursor")
{
    // The centre looks straight down; above the centre is the heading (yaw 90: +Y).
    CHECK(Vector3(0.f, 0.f, -250.f) == Bomber::AimDirection(Bomber::CursorCentre, 90.f));
    auto ahead = Bomber::AimDirection(Bomber::CursorCentre - Vector2(0.f, 100.f), 90.f);
    CHECK_PRECISION(100.f, ahead.Y, 3);
    CHECK_PRECISION(0.f, ahead.X, 3);
    auto right = Bomber::AimDirection(Bomber::CursorCentre + Vector2(100.f, 0.f), 90.f);
    CHECK_PRECISION(100.f, right.X, 3);
}

TEST_CASE("RideTests.BombFallsOntoTheTarget")
{
    // 64 units under 32 u/s²: 2 s; nothing below: 2.5 s.
    CHECK_PRECISION(2.f, Bomber::FallTime(64.f), 4);
    CHECK(2.5f == Bomber::FallTime(-5.f));
}

TEST_CASE("RideTests.KurtRisesFasterAfterThreeUnitsATick")
{
    // 0.025 per tick²: past 3 on the 121st tick, then 0.075 per tick².
    float speed = 0.f;
    int ticks = 0;
    while (speed <= 3.f)
    {
        speed = EndLevel::RiseSpeed(speed, Tick);
        ticks++;
    }

    // 121 ticks in exact arithmetic; float sums may cross a tick early.
    CHECK(ticks >= 120);
    CHECK(ticks <= 121);
    CHECK_PRECISION(speed + 0.075f, EndLevel::RiseSpeed(speed, Tick), 4);
}

// The end tears off only drawn triangles (level.gd get_arena_triangles): level 3's
// NONE triangles stay.
DATA_TEST_CASE("RideTests.EndTearsOnlyDrawnTriangles")
{
    LevelData level(*GameData::Data(), 3);
    std::shared_ptr<Formats::Arena> arena;
    int none = -1;
    for (const auto& candidate : level.Arenas)
    {
        for (int t = 0; t < candidate->TriangleCount(); t++)
        {
            if (IsNone(*candidate, t))
            {
                arena = candidate;
                none = t;
                break;
            }
        }

        if (arena)
        {
            break;
        }
    }

    REQUIRE(arena != nullptr);
    auto order = EndLevel::HighestFirst(level, *arena);
    CHECK(std::find(order.begin(), order.end(), none) == order.end());
}
