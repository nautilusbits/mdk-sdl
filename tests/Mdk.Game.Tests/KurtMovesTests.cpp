#include <doctest.h>

#include "Asserts.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Game/Angles.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/CameraRoll.h"
#include "Mdk.Game/Kurt/FollowCamera.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Shared/GameData.h"

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Level;
using Mdk::Game::DegreesToRadians;
using Mdk::Game::Kurt::CameraRoll;
using Mdk::Game::Kurt::Difficulty;
using Mdk::Game::Kurt::FollowCamera;
using Mdk::Tests::GameData;
using KurtPlayer = Mdk::Game::Kurt::Kurt;
using State = KurtPlayer::State;

// Kurt's other moves (godot-mdk docs/gameplay.md): ledges (damp_ledge_grab 0x469868),
// sliding (damp_buttslide 0x468db8), hard landings and falling out of the arena (damp_gravity
// 0x469efc), and the camera's roll, clearance (0x417ee8) and shake.
namespace
{
    constexpr float Step = 1.f / 60.f;
    constexpr float Tick = 1.f / 30.f;
    constexpr int Frames = 5;
    // K_HANG's frames: climbing takes 2 x 16 - 1 ticks; K_CRASHL's: 52 ticks.
    constexpr int HangFrames = 16;
    constexpr int CrashFrames = 26;
    // Level 3's start pad (Kurt stands at z 192).
    const Vector3 Pad(-4.f, 0.f, 195.f);
    // Level 3: running and jumping east here, Kurt grabs the ledge at x 83 and climbs it
    // (the Godot port's Kurt ends at (93, 446, 145) too).
    const Vector3 BelowLedge(67.6f, 441.8f, 133.5f);

    // Level 6's first wind zone (COLYM_1, wind_zone [1, 90, 10]): its box holds Kurt.
    const Vector3 WindMin(-1206.f, -811.f, -115.f);
    const Vector3 WindMax(-1096.f, -764.f, -35.f);
    constexpr float WindYaw = 90.f;
    constexpr float WindSpeed = 10.f;

    // Level 6's COLYM_3 wind zone (wind_zone [1, 180, 10]), at the top of the spiral under
    // OLYM_3's hatch.
    const Vector3 HatchWindMin(-1738.f, 179.f, -745.f);
    const Vector3 HatchWindMax(-1656.f, 246.f, -680.f);
    constexpr float HatchWindYaw = 180.f;

    // Level 3: a flat floor (the pad slopes a little: Kurt slides off it).
    const Vector3 FlatFloor(-86.f, 334.f, 144.f);

    // Level 7: Kurt with his back 2 units from the corridor's wall (x 14).
    const Vector3 BackToWall(12.f, 100.f, 12.f);
    constexpr float FacingWest = 180.f;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    // A level's arena (its start arena by default), Kurt's only solid one.
    std::shared_ptr<ArenaSpace> SpaceOf(int level, std::optional<std::string> name = std::nullopt)
    {
        LevelData data(*GameData::Data(), level);
        if (!name.has_value())
        {
            name = data.Dti->Arenas[data.Dti->StartArena].Name;
        }

        auto space = std::make_shared<ArenaSpace>();
        for (const auto& arena : data.Arenas)
        {
            if (arena->Name == *name)
            {
                space->Add(arena);
                break;
            }
        }

        std::vector<std::string> solid{*name};
        space->SetSolid(solid);
        return space;
    }

    // Kurt with the space and mixer he holds by reference (Kurt destroyed first).
    struct Rig
    {
        std::shared_ptr<ArenaSpace> Space;
        std::shared_ptr<SoundMixer> Mixer;
        std::shared_ptr<KurtPlayer> Kurt;

        KurtPlayer* operator->() const { return Kurt.get(); }
        KurtPlayer& operator*() const { return *Kurt; }
    };

    int FrameCount(State state)
    {
        switch (state)
        {
            case State::Hang:
                return HangFrames;
            case State::HardLand:
                return CrashFrames;
            default:
                return Frames;
        }
    }

    Rig KurtIn(std::shared_ptr<ArenaSpace> space, Vector3 feet, float yaw = 0.f)
    {
        auto mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        auto kurt = std::make_shared<KurtPlayer>(*space, *mixer, FrameCount);
        kurt->Feet = feet;
        kurt->Yaw = yaw;
        return {space, mixer, kurt};
    }

    void Run(KurtPlayer& kurt, Input& input, float seconds, FollowCamera* camera = nullptr)
    {
        for (float t = 0.f; t < seconds; t += Step)
        {
            kurt.Update(input, Step);
            if (camera != nullptr)
            {
                camera->Update(kurt, 4.f, input, Step);
            }
        }
    }

    void Run(KurtPlayer& kurt, Input&& input, float seconds, FollowCamera* camera = nullptr)
    {
        Run(kurt, input, seconds, camera);
    }

    Input Holding(std::initializer_list<Key> keys)
    {
        Input input;
        for (auto key : keys)
        {
            input.Hold(key, Input::State::Down);
        }

        return input;
    }

    // Runs until Kurt is in `state` (at most `seconds`).
    bool RunUntil(KurtPlayer& kurt, Input& input, State state, float seconds)
    {
        for (float t = 0.f; t < seconds && kurt.Current != state; t += Step)
        {
            kurt.Update(input, Step);
        }

        return kurt.Current == state;
    }

    bool RunUntil(KurtPlayer& kurt, Input&& input, State state, float seconds)
    {
        return RunUntil(kurt, input, state, seconds);
    }

    Vector3 WithZ(Vector3 v, float z)
    {
        v.Z = z;
        return v;
    }
}

DATA_TEST_CASE("KurtMovesTests.KurtGrabsTheLedgeAndClimbsIt")
{
    auto kurt = KurtIn(SpaceOf(3), BelowLedge);
    Run(*kurt, Input(), 0.5f);
    auto input = Holding({Key::Forward, Key::Jump});

    CHECK(RunUntil(*kurt, input, State::Hang, 3.f));

    // He hangs 4.6 below the edge, facing it (its direction - 90 degrees), 1 unit before it.
    CHECK_NEAR(15.f, kurt->Yaw, 0.5f);
    CHECK_NEAR(82.13f, kurt->Feet.X, 0.05f);
    CHECK(kurt->VerticalSpeed == 0.f);
    CHECK_FALSE(kurt->ChuteOpen);

    // Climbing: 2 x (6.406 - 0.374) x 0.354 up and 2 x (0.685 + 1.293) x 0.354 on, in 31 ticks.
    auto hang = kurt->Feet;
    auto facing = kurt->Facing();
    Run(*kurt, input, 0.5f);
    CHECK(kurt->Current == State::Hang);
    CHECK(kurt->AnimationFrame == 7.f);
    CHECK(RunUntil(*kurt, input, State::Still, 1.f));
    CHECK_NEAR(hang.Z + 4.2706f, kurt->Feet.Z, 0.01f);
    CHECK_NEAR(1.4011f, Vector3::Dot(kurt->Feet - hang, facing), 0.01f);

    // On top he runs on.
    Run(*kurt, input, 0.5f);
    CHECK(kurt->OnFloor);
    CHECK(kurt->Feet.Z >= 145.f);
    CHECK(kurt->Feet.Z <= 146.5f);
}

// Issue #30: level 8's walkway (GUNT_7, top at z -228) is a wedge with no side wall at
// its inner edge. Falling short of it, Kurt's box goes in from the side; the original keeps him
// 2 below its top. He climbs onto it instead.
DATA_TEST_CASE("KurtMovesTests.KurtDoesNotStayInsideTheWalkway")
{
    auto kurt = KurtIn(SpaceOf(8, "GUNT_7"), Vector3(60.f, 2774.f, -220.f), 90.f);
    Run(*kurt, Holding({Key::Forward}), 1.f);
    Run(*kurt, Input(), 0.5f);

    CHECK(kurt->OnFloor);
    CHECK_NEAR(-228.f, kurt->Feet.Z, 0.05f);
}

DATA_TEST_CASE("KurtMovesTests.NoLedgeWithoutForward")
{
    auto kurt = KurtIn(SpaceOf(3), BelowLedge);
    Run(*kurt, Input(), 0.5f);

    CHECK_FALSE(RunUntil(*kurt, Holding({Key::Jump}), State::Hang, 3.f));
}

TEST_CASE("KurtMovesTests.SlideAccelSquaresThePush")
{
    auto kurt = KurtIn(std::make_shared<ArenaSpace>(), Vector3::Zero());
    kurt->SlideAccel(Vector2(10.f, -10.f), Tick);
    CHECK_PRECISION(100.f / 30.f, kurt->SlideVelocity().X, 4);
    CHECK_PRECISION(-100.f / 30.f, kurt->SlideVelocity().Y, 4);

    // Within +-0.1 it brakes by 2 u/s^2.
    kurt->SlideAccel(Vector2(0.05f, 0.f), Tick);
    CHECK_PRECISION(100.f / 30.f - 2.f / 30.f, kurt->SlideVelocity().X, 4);
    CHECK_PRECISION(-100.f / 30.f + 2.f / 30.f, kurt->SlideVelocity().Y, 4);
}

DATA_TEST_CASE("KurtMovesTests.TheWindSlidesKurtDownTheTunnel")
{
    auto kurt = KurtIn(SpaceOf(6, "COLYM_1"), Vector3(-1150.f, -790.f, -85.f), WindYaw);
    Input input;
    for (float t = 0.f; t < 4.f; t += Step)
    {
        // The script's tick (30 a second), as ScriptRuntime.WindZone.
        auto p = kurt->Feet;
        if (ToInt(t / Step) % 2 == 0 && p.X >= WindMin.X && p.Y >= WindMin.Y && p.Z >= WindMin.Z && p.X <= WindMax.X && p.Y <= WindMax.Y
            && p.Z <= WindMax.Z && (kurt->Sliding || kurt->OnFloor))
        {
            kurt->StartSlide();
            kurt->Yaw = WindYaw;
            kurt->SlideAccel(Vector2(0.f, WindSpeed), Tick);
        }

        kurt->Update(input, Step);
        CHECK(kurt->SlideVelocity().Length() <= 50.f + 1e-3f);
    }

    CHECK(kurt->Sliding);
    CHECK(kurt->Current == State::Slide);

    // On the Godot port's path: (-1138, -599, -185) to (-1138, -498, -241).
    auto f = kurt->Feet;
    CHECK(f.X >= -1145.f);
    CHECK(f.X <= -1135.f);
    CHECK(f.Y >= -640.f);
    CHECK(f.Y <= -560.f);
    CHECK_NEAR(-185.f - 56.f / 101.f * (f.Y + 599.f), f.Z, 3.f);
}

// Issue #20: dropping through the hatch with the chute open, Kurt glided past the wind
// box and walked down the spiral. In the box the wind keeps the chute shut (0x573bec, damp_vertical
// 0x4694bc): he lands in it and slides.
DATA_TEST_CASE("KurtMovesTests.TheWindShutsTheChute")
{
    auto kurt = KurtIn(SpaceOf(6, "COLYM_3"), Vector3(-1662.f, 212.f, -684.f), HatchWindYaw);
    auto input = Holding({Key::Jump, Key::Forward});
    for (float t = 0.f; t < 3.f; t += Step)
    {
        // The script's tick (30 a second), as ScriptRuntime.WindZone.
        auto p = kurt->Feet;
        bool inBox = p.X >= HatchWindMin.X && p.Y >= HatchWindMin.Y && p.Z >= HatchWindMin.Z && p.X <= HatchWindMax.X
            && p.Y <= HatchWindMax.Y && p.Z <= HatchWindMax.Z;
        if (ToInt(t / Step) % 2 == 0 && inBox && (kurt->Sliding || kurt->OnFloor))
        {
            kurt->StartSlide();
            kurt->Yaw = HatchWindYaw;
            kurt->SlideAccel(Vector2(-WindSpeed, 0.f), Tick);
        }
        else if (ToInt(t / Step) % 2 == 0 && inBox)
        {
            kurt->Blow(Tick);
        }

        kurt->Update(input, Step);
    }

    CHECK_MESSAGE(kurt->Sliding, "not sliding at (" << kurt->Feet.X << ", " << kurt->Feet.Y << ", " << kurt->Feet.Z << "), "
        << static_cast<int>(kurt->Current));
}

DATA_TEST_CASE("KurtMovesTests.ForwardSpeedsTheSlideUpAndBackBrakesIt")
{
    auto kurt = KurtIn(SpaceOf(6, "COLYM_1"), Vector3(-1150.f, -790.f, -85.f), WindYaw);
    Run(*kurt, Input(), 0.5f);
    kurt->StartSlide();
    kurt->SlideAccel(Vector2(0.f, 20.f), 1.f);
    CHECK(kurt->Current == State::Slip);

    Run(*kurt, Holding({Key::Forward}), 1.f);
    CHECK(kurt->Current == State::SlideFast);
    CHECK(kurt->SlideVelocity().Length() > 50.f);

    Run(*kurt, Holding({Key::Back}), 0.2f);
    CHECK(kurt->Current == State::SlideBrake);
}

DATA_TEST_CASE("KurtMovesTests.AtRestKurtGetsUp")
{
    auto kurt = KurtIn(SpaceOf(3), FlatFloor);
    Run(*kurt, Input(), 1.f);
    kurt->StartSlide();
    CHECK(kurt->Sliding);

    // A flat floor gives no push and he has no speed: he gets up from K_BFLIP.
    Input idle;
    kurt->Update(idle, Step);
    CHECK_FALSE(kurt->Sliding);
    CHECK(kurt->Knocked());
    CHECK(RunUntil(*kurt, Input(), State::Still, 2.f));
}

// LEVEL6: the slide drops Kurt into OLYM_2 against a wall; still moving, he lay there
// until friction stopped him. A new arena ends the slide 15 ticks later (0x573be8 = -15).
DATA_TEST_CASE("KurtMovesTests.ANewArenaEndsTheSlide")
{
    auto kurt = KurtIn(SpaceOf(3), FlatFloor);
    Run(*kurt, Input(), 1.f);
    kurt->StartSlide();
    kurt->SlideAccel(Vector2(0.f, 30.f), 1.f);
    kurt->EnterArena();

    Run(*kurt, Input(), 0.45f);
    CHECK(kurt->Sliding);

    Run(*kurt, Input(), 0.1f);
    CHECK_FALSE(kurt->Sliding);
    CHECK(kurt->SlideVelocity() == Vector2::Zero());
    CHECK(RunUntil(*kurt, Input(), State::Still, 2.f));
}

// The mouse steers the slide as it turns Kurt walking, at most 4 x 45 degrees/s (damp_control
// clamps it to +-4 before 0x5014d4).
DATA_TEST_CASE("KurtMovesTests.TheMouseSteersTheSlide")
{
    struct Row { float Mouse; float Yaw; };
    const Row rows[] = {{10.f, -1.5f}, {1000.f, -3.f}};

    for (const auto& row : rows)
    {
        CAPTURE(row.Mouse);
        auto kurt = KurtIn(SpaceOf(3), FlatFloor);
        Run(*kurt, Input(), 1.f);
        kurt->StartSlide();
        kurt->SlideAccel(Vector2(30.f, 0.f), 1.f);
        Input input;
        input.AddMouseMotion(row.Mouse, 0.f);

        kurt->Update(input, Step);
        CHECK_PRECISION(row.Yaw, kurt->Yaw, 3);
    }
}

TEST_CASE("KurtMovesTests.TwentyTicksInTheAirEndTheSlide")
{
    auto kurt = KurtIn(std::make_shared<ArenaSpace>(), Vector3(0.f, 0.f, 100.f));
    kurt->StartSlide();
    Run(*kurt, Input(), 0.6f);
    CHECK(kurt->Sliding);

    Run(*kurt, Input(), 0.1f);
    CHECK_FALSE(kurt->Sliding);
    CHECK(kurt->Current == State::Fall);
}

DATA_TEST_CASE("KurtMovesTests.LandingFasterThan100Hurts")
{
    struct Row { Difficulty Difficulty; float Height; int Health; State State; };
    const Row rows[] = {
        {Difficulty::Normal, 300.f, 90, State::HardLand},
        {Difficulty::Easy, 300.f, 94, State::HardLand},
        {Difficulty::Normal, 250.f, 100, State::Land},
    };

    for (const auto& row : rows)
    {
        CAPTURE(row.Height);
        auto kurt = KurtIn(SpaceOf(3), WithZ(Pad, row.Height));
        kurt->Inventory->Difficulty = row.Difficulty;
        CHECK(RunUntil(*kurt, Input(), row.State, 5.f));
        CHECK(kurt->Health == row.Health);
        CHECK(kurt->KnockDamage == 0.f);
    }
}

DATA_TEST_CASE("KurtMovesTests.AfterAHardLandingKurtCantMoveForAWhile")
{
    auto kurt = KurtIn(SpaceOf(3), WithZ(Pad, 300.f));
    CHECK(RunUntil(*kurt, Input(), State::HardLand, 5.f));
    auto landed = kurt->Feet;

    // K_CRASHL: 26 frames at 2 ticks each.
    Run(*kurt, Holding({Key::Forward, Key::Fire}), 1.5f);
    CHECK(kurt->Current == State::HardLand);
    CHECK(kurt->Feet == landed);
    CHECK_FALSE(kurt->Firing);
    CHECK(RunUntil(*kurt, Input(), State::Still, 0.5f));
}

DATA_TEST_CASE("KurtMovesTests.Falling50BelowTheArenaKills")
{
    auto space = SpaceOf(3);
    float bottom = space->Bottom(Pad).value();
    auto kurt = KurtIn(space, WithZ(Pad, bottom - 49.f));
    bool died = false;
    kurt->Died = [&died] { died = true; };

    CHECK(RunUntil(*kurt, Input(), State::Dead, 2.f));
    CHECK(kurt->Health == 0);
    CHECK(kurt->Feet.Z <= bottom - 50.f);
    Run(*kurt, Input(), 5.f);
    CHECK(died);
}

// Playtest: off LEVEL6's lift (OLYM_8, start of XTR_1000) Kurt falls past the floating
// lamps; the arena's bottom is z -2900 (as in the original), so he dies after about 7 s.
DATA_TEST_CASE("KurtMovesTests.FallingOffTheLevel6LiftKills")
{
    auto space = SpaceOf(6, "OLYM_8");
    auto kurt = KurtIn(space, Vector3(-1658.f, 3406.f, -1866.f));

    CHECK(RunUntil(*kurt, Input(), State::Dead, 8.f));
    CHECK(kurt->Feet.Z <= space->Bottom(kurt->Feet).value() - 50.f);
}

TEST_CASE("KurtMovesTests.RunningAndTurningRollsTheView")
{
    // 0.25 degrees a tick up to 10, the other way first jumps 2 back.
    CameraRoll roll;
    for (int i = 0; i < 4; i++)
    {
        roll.Walk(1.f, 1.f, Tick);
        roll.Settle(Tick);
    }

    CHECK_PRECISION(1.f, roll.Roll, 4);
    for (int i = 0; i < 100; i++)
    {
        roll.Walk(1.f, 1.f, Tick);
        roll.Settle(Tick);
    }

    CHECK_PRECISION(10.f, roll.Roll, 4);
    roll.Walk(-1.f, 1.f, Tick);
    CHECK_PRECISION(7.75f, roll.Roll, 4);

    // Without input it levels out by 0.35 |roll| a tick (at most 2.5, at least 0.05).
    roll.Settle(Tick);
    roll.Settle(Tick);
    CHECK_PRECISION(5.25f, roll.Roll, 4);
    for (int i = 0; i < 100; i++)
    {
        roll.Settle(Tick);
    }

    CHECK(roll.Roll == 0.f);

    // Turning on the spot doesn't roll.
    roll.Walk(1.f, 0.f, Tick);
    roll.Settle(Tick);
    CHECK(roll.Roll == 0.f);
}

TEST_CASE("KurtMovesTests.SlidingAndTheBoardRollTheView")
{
    // Slope 30 degrees across the way (normal (0.5, 0, 0.866), facing +y): towards 30, a tenth a tick.
    CameraRoll roll;
    roll.Slide(Vector3(0.5f, 0.f, MathF::Sqrt(0.75f)), Vector2::UnitY(), Tick);
    CHECK_PRECISION(3.f, roll.Roll, 3);

    // The board's bank at 45 degrees/s.
    roll = CameraRoll();
    roll.Follow(10.f, Tick);
    roll.Settle(Tick);
    CHECK_PRECISION(1.5f, roll.Roll, 4);
}

TEST_CASE("KurtMovesTests.KurtRollsTheCameraTurningRight")
{
    auto kurt = KurtIn(std::make_shared<ArenaSpace>(), Vector3(0.f, 0.f, 100.f), 90.f);
    FollowCamera camera;
    Run(*kurt, Holding({Key::Forward, Key::TurnRight}), 1.f, &camera);
    CHECK_NEAR(7.5f, kurt->CameraRoll->Roll, 0.2f);

    // The camera's up turns towards its right.
    auto level = FollowCamera::UpOf(camera.Forward);
    auto right = Vector3::Normalize(Vector3::Cross(camera.Forward, level));
    CHECK_PRECISION(MathF::Sin(DegreesToRadians(kurt->CameraRoll->Roll)), Vector3::Dot(camera.Up, right), 3);
}

DATA_TEST_CASE("KurtMovesTests.AWallBehindKurtPushesHim")
{
    auto space = SpaceOf(7);
    auto kurt = KurtIn(space, BackToWall, FacingWest);
    FollowCamera camera(space.get());
    Run(*kurt, Input(), 1.5f, &camera);

    // The Godot port pushes him to x 7.0 (its ray misses a lower part of the wall the box meets).
    CHECK(kurt->Feet.X >= 5.f);
    CHECK(kurt->Feet.X <= 7.5f);
}

DATA_TEST_CASE("KurtMovesTests.WithoutClearanceTheWallDoesntPush")
{
    auto kurt = KurtIn(SpaceOf(7), BackToWall, FacingWest);
    FollowCamera camera;
    Run(*kurt, Input(), 1.5f, &camera);
    CHECK_PRECISION(BackToWall.X, kurt->Feet.X, 2);
}

DATA_TEST_CASE("KurtMovesTests.AnObjectInTheViewPushesKurtForwardGradually")
{
    auto space = SpaceOf(3);
    auto kurt = KurtIn(space, Pad, FacingWest);
    Run(*kurt, Input(), 1.f);

    // An object's face 4 units behind him (the camera is 8 behind).
    float start = kurt->Feet.X;
    float face = start + 4.f;
    FollowCamera camera(space.get());
    camera.ClipView = [face](Vector3 from, Vector3 to)
    {
        return to.X <= face ? to : Vector3::Lerp(from, to, (face - from.X) / (to.X - from.X));
    };
    Run(*kurt, Input(), Step, &camera);

    // One step pushes him only part of the way (at most 40 u/s): a door behind him doesn't
    // "fast-forward" him.
    CHECK(start - kurt->Feet.X >= 0.01f);
    CHECK(start - kurt->Feet.X <= 40.f * Step + 0.01f);

    // A few steps on (4 cut off at 40 u/s), he has gone forward by all of it, the camera with him.
    for (int i = 0; i < 5; i++)
    {
        Run(*kurt, Input(), Step, &camera);
    }

    CHECK_NEAR(face, camera.Position.X, 0.01f);
    CHECK_NEAR(face - 7.97f, kurt->Feet.X, 0.05f);
}

DATA_TEST_CASE("KurtMovesTests.TheShakeTurnsTheViewThenDrains")
{
    auto kurt = KurtIn(SpaceOf(3), Pad);
    FollowCamera camera;
    Run(*kurt, Input(), 1.f, &camera);
    auto steady = camera.Forward;

    // Shake 5: up to +-1.64 x 5 pixels; it drains by 0.25 a tick.
    camera.RaiseShake(5.f);
    int shaken = 0;
    for (float t = 0.f; t < 0.5f; t += Step)
    {
        Input idle;
        kurt->Update(idle, Step);
        Input still;
        camera.Update(*kurt, 4.f, still, Step);
        CHECK(MathF::Abs(camera.ShakeOffset.X) >= 0.f);
        CHECK(MathF::Abs(camera.ShakeOffset.X) <= 8.f);
        CHECK(MathF::Abs(camera.ShakeOffset.Y) >= 0.f);
        CHECK(MathF::Abs(camera.ShakeOffset.Y) <= 8.f);
        shaken += camera.ShakeOffset != Vector2::Zero() && camera.Forward != steady ? 1 : 0;
    }

    CHECK(shaken > 0);
    Run(*kurt, Input(), 0.3f, &camera);
    CHECK(camera.ShakeOffset == Vector2::Zero());
    CHECK(camera.Forward == steady);
}

TEST_CASE("KurtMovesTests.FiringOnTheBoardShowsTheMuzzleFlash")
{
    auto kurt = KurtIn(std::make_shared<ArenaSpace>(), Vector3::Zero());
    kurt->Ride = [](Input&, float) {};
    kurt->Pose = KurtPlayer::PoseFrame{"K_SURF", 0};
    auto input = Holding({Key::Fire});
    std::optional<KurtPlayer::MuzzleFlash> seen;
    for (int i = 0; i < 8; i++)
    {
        kurt->UpdateGun(input, Step);
        if (!seen.has_value())
        {
            seen = kurt->Muzzle;
        }
    }

    // At (-42, 12) plus 0-4 pixels (damp_animate, state 201).
    REQUIRE(seen.has_value());
    CHECK(seen->X >= -42);
    CHECK(seen->X <= -38);
    CHECK(seen->Y >= 12);
    CHECK(seen->Y <= 16);
}
