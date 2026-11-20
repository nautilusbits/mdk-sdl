#include <doctest.h>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/FrameBlend.h"
#include "Mdk.Game/Kurt/FollowCamera.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Kurt/Scope.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/Debris.h"
#include "Mdk.Game/Scripts/Effects.h"
#include "Mdk.Game/Viewer.h"
#include "Asserts.h"

#include <memory>
#include <optional>
#include <string>

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using namespace Mdk::Game::Objects;
using namespace Mdk::Game::Scripts;
using KurtPlayer = Mdk::Game::Kurt::Kurt;
using Mdk::Game::Kurt::FollowCamera;
using Mdk::Game::Kurt::Scope;

// Frames between the fixed steps (issue #8: screens above 60 Hz looked 60 Hz): Kurt, the
// objects and the camera drawn between their last two states, the mouse turning at once.
namespace
{
    constexpr float Step = Viewer::Step;
    constexpr float Half = 0.5f;
    // The C#'s float Precision picks xUnit's tolerance overload: within 4.
    constexpr float Precision = 4;
    // LEVEL6's lift moves this much a tick at full speed.
    constexpr float LiftMove = 25.f;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    // Kurt on the floor, with the space and mixer he refers to.
    struct Rig
    {
        std::shared_ptr<ArenaSpace> Space = std::make_shared<ArenaSpace>();
        std::shared_ptr<SoundMixer> Mixer =
            std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        std::shared_ptr<KurtPlayer> Kurt;

        Rig()
        {
            Kurt = std::make_shared<KurtPlayer>(*Space, *Mixer, [](auto) { return 1; });
            Kurt->OnFloor = true;
        }
    };

    std::shared_ptr<MdkObject> InArena(const std::string& arena)
    {
        auto obj = std::make_shared<MdkObject>();
        obj->Arena = arena;
        return obj;
    }
}

TEST_CASE("FrameBlendTests.LerpEndsExactlyAtTheLatest")
{
    Vector3 from(0.1f, 0.2f, 0.3f);
    Vector3 to(1e6f, -3.3f, 7.7f);
    CHECK(FrameBlend::Lerp(from, to, 1.f) == to);
    CHECK(FrameBlend::Lerp(Vector3::Zero(), Vector3(10.f, 0.f, 0.f), Half) == Vector3(5.f, 0.f, 0.f));
}

TEST_CASE("FrameBlendTests.AnglesTurnTheShortWay")
{
    CHECK_NEAR(360.f, FrameBlend::LerpAngle(350.f, 10.f, Half), Precision);
    CHECK_NEAR(180.f, FrameBlend::LerpAngle(170.f, 190.f, Half), Precision);
    CHECK_NEAR(-45.f, FrameBlend::LerpAngle(0.f, 270.f, Half), Precision);
}

TEST_CASE("FrameBlendTests.TeleportsAreJumps")
{
    CHECK_FALSE(FrameBlend::Jumped(Vector3::Zero(), Vector3(4.f, 0.f, 0.f), Step));
    CHECK(FrameBlend::Jumped(Vector3::Zero(), Vector3(500.f, 0.f, 0.f), Step));
}

TEST_CASE("FrameBlendTests.KurtIsDrawnBetweenSteps")
{
    Rig rig;
    auto& kurt = *rig.Kurt;
    FrameBlend blend(Blending::On);
    blend.Record(kurt, nullptr, nullptr, 0);
    kurt.Feet = Vector3(1.f, 0.f, 0.f);
    kurt.Yaw = 10.f;
    blend.Record(kurt, nullptr, nullptr, 0);

    blend.Update(kurt, Half, 1.f);
    CHECK(blend.Feet == Vector3(Half, 0.f, 0.f));
    CHECK_NEAR(5.f, blend.Yaw, Precision);

    blend.Update(kurt, 1.f, 1.f);
    CHECK(blend.Feet == kurt.Feet);
}

TEST_CASE("FrameBlendTests.TeleportsAreDrawnWhereTheyEnd")
{
    Rig rig;
    auto& kurt = *rig.Kurt;
    FrameBlend blend(Blending::On);
    blend.Record(kurt, nullptr, nullptr, 0);
    kurt.Teleport(Vector3(2.f, 0.f, 0.f), 0.f);
    blend.Record(kurt, nullptr, nullptr, 0);

    blend.Update(kurt, Half, 1.f);
    CHECK(blend.Feet == kurt.Feet);
}

TEST_CASE("FrameBlendTests.TestsDrawTheStepsAsTheyAre")
{
    Rig rig;
    auto& kurt = *rig.Kurt;
    FrameBlend blend(Blending::Off);
    blend.Record(kurt, nullptr, nullptr, 0);
    kurt.Feet = Vector3(1.f, 0.f, 0.f);
    blend.Record(kurt, nullptr, nullptr, 0);

    blend.Update(kurt, Half, Half);
    auto obj = std::make_shared<MdkObject>();
    obj->Position = Vector3(3.f, 0.f, 0.f);
    obj->Remember();
    obj->Position = Vector3(5.f, 0.f, 0.f);
    CHECK(blend.Feet == kurt.Feet);
    CHECK(blend.TransformOf(*obj) == obj->Transform());
}

TEST_CASE("FrameBlendTests.ObjectsAreDrawnBetweenTicks")
{
    auto obj = InArena("A");
    obj->Position = Vector3::Zero();
    obj->Yaw = 0.f;
    CHECK(obj->TransformAt(Half) == obj->Transform());

    obj->Remember();
    obj->Position = Vector3(10.f, 0.f, 0.f);
    obj->Yaw = 90.f;
    CHECK(obj->PositionAt(Half) == Vector3(5.f, 0.f, 0.f));
    CHECK_NEAR(45.f, obj->YawAt(Half), Precision);
    CHECK(obj->TransformAt(1.f) == obj->Transform());

    // Into another arena: where it is.
    obj->Arena = "B";
    CHECK(obj->PositionAt(Half) == obj->Position);
}

TEST_CASE("FrameBlendTests.KurtOnAPlatformMovesWithIt")
{
    // The platform moves 10 at the tick; Kurt, carried at the tick, is drawn where the platform is.
    Rig rig;
    auto& kurt = *rig.Kurt;
    auto lift = InArena("A");
    FrameBlend blend(Blending::On);
    blend.Record(kurt, lift, nullptr, 0);
    lift->Remember();
    lift->Position = Vector3(0.f, 0.f, 10.f);
    kurt.Feet = lift->Position;
    blend.Record(kurt, lift, nullptr, 1);

    blend.Update(kurt, Half, Half / 2.f);
    CHECK(blend.Feet == lift->PositionAt(Half / 2.f));
}

TEST_CASE("FrameBlendTests.KurtLandingOnALiftIsDrawnOnIt")
{
    // LEVEL6's lift (25 a tick): Kurt falls onto it as it moves; the step he lands on it
    // starts where the last frame drew him, not 25 further (--fps=240 --trace: a jitter).
    Rig rig;
    auto& kurt = *rig.Kurt;
    kurt.Feet = Vector3(0.f, 0.f, 2.f);
    auto lift = InArena("A");
    FrameBlend blend(Blending::On);
    blend.Record(kurt, nullptr, nullptr, 0);
    blend.Record(kurt, nullptr, nullptr, 0);

    lift->Remember();
    lift->Position = Vector3(0.f, -LiftMove, 0.f);
    kurt.Feet = lift->Position;
    blend.Record(kurt, lift, nullptr, 1);
    blend.Update(kurt, 0.f, 0.f);
    CHECK(blend.Feet == Vector3(0.f, 0.f, 2.f));

    // A step without a tick where he isn't on it (his fall finds it a step later): still
    // drawn with it.
    blend.Record(kurt, nullptr, nullptr, 1);
    blend.Update(kurt, Half, Half);
    CHECK(blend.Feet == lift->PositionAt(Half));
}

TEST_CASE("FrameBlendTests.WhatFollowsKurtDoesNotCarryHim")
{
    // LEVEL4's board: Kurt stands on it, it follows him at the tick. He stood still: drawn so,
    // not moved with it (--fps=240 --trace: 0.3 back and forth as he boards).
    Rig rig;
    auto& kurt = *rig.Kurt;
    kurt.Feet = Vector3::UnitX();
    auto board = InArena("A");
    FrameBlend blend(Blending::On);
    blend.Record(kurt, board, board, 0);
    board->Remember();
    board->Position = kurt.Feet;
    blend.Record(kurt, board, board, 1);

    blend.Update(kurt, Half, Half / 2.f);
    CHECK(blend.Feet == kurt.Feet);
}

TEST_CASE("FrameBlendTests.TheMouseTurnsKurtEveryFrame")
{
    Rig rig;
    auto& kurt = *rig.Kurt;
    Input input;
    kurt.Update(input, Step);
    float yaw = kurt.Yaw;

    input.AddMouseMotion(10.f, 0.f);
    float turn = kurt.MouseLook(input);
    CHECK(turn != 0.f);
    CHECK(kurt.Yaw == yaw + turn);
    CHECK(input.MouseX() == 0.f);

    // The step after doesn't turn him again.
    kurt.Update(input, Step);
    CHECK_NEAR(yaw + turn, kurt.Yaw, Precision);
}

TEST_CASE("FrameBlendTests.TheCameraIsPlacedForKurtAsDrawn")
{
    // At the last step's place and pitch the frame's camera is the step's.
    Rig rig;
    auto& kurt = *rig.Kurt;
    FollowCamera camera;
    Input input;
    camera.Update(kurt, 4.f, input, Step);
    camera.Update(kurt, 4.f, input, Step);
    Vector3 position = camera.Position;
    Vector3 forward = camera.Forward;

    camera.Blend(kurt, kurt.Feet, kurt.Yaw, 1.f);
    CHECK_NEAR(position.X, camera.Position.X, Precision);
    CHECK_NEAR(position.Y, camera.Position.Y, Precision);
    CHECK_NEAR(position.Z, camera.Position.Z, Precision);
    CHECK_NEAR(forward.Z, camera.Forward.Z, Precision);

    // Kurt drawn 1 further: so is the camera.
    camera.Blend(kurt, kurt.Feet + Vector3::UnitX(), kurt.Yaw, 1.f);
    CHECK_NEAR(position.X + 1.f, camera.Position.X, Precision);
}

TEST_CASE("FrameBlendTests.TheScopeIsDrawnBetweenSteps")
{
    // Issue #16: the zoom and the clip moved at 60 Hz. The mouse's tilt since the step shows.
    Rig rig;
    auto& kurt = *rig.Kurt;
    kurt.EnterSniper();
    auto scope = kurt.Scope;
    scope->ClipTime = 2.f;
    FrameBlend blend(Blending::On);
    blend.Record(kurt, nullptr, nullptr, 0);
    scope->Zoom = Half;
    scope->Pitch = 10.f;
    scope->ClipTime = 1.f;
    blend.Record(kurt, nullptr, nullptr, 0);
    scope->Pitch += 2.f;

    blend.Update(kurt, Half, 1.f);
    CHECK_NEAR(0.75f, blend.Zoom, Precision);
    CHECK_NEAR(7.f, blend.ScopePitch, Precision);
    CHECK_NEAR(1.5f, blend.ClipTime, Precision);

    // A shot or a reload refills the clip at once.
    scope->ClipTime = 3.f;
    blend.Record(kurt, nullptr, nullptr, 0);
    blend.Update(kurt, Half, 1.f);
    CHECK(blend.ClipTime == 3.f);
}

TEST_CASE("FrameBlendTests.TheScopeOpensAtOnce")
{
    // The last time's zoom isn't drawn on the way to the new one.
    Rig rig;
    auto& kurt = *rig.Kurt;
    kurt.Scope->Zoom = Scope::ZoomMin;
    FrameBlend blend(Blending::On);
    blend.Record(kurt, nullptr, nullptr, 0);
    kurt.EnterSniper();
    blend.Record(kurt, nullptr, nullptr, 0);

    blend.Update(kurt, Half, 1.f);
    CHECK(blend.Zoom == 1.f);
}

TEST_CASE("FrameBlendTests.PiecesAreDrawnBetweenTicks")
{
    // Issue #16: sparks and pieces flew at 30 Hz. A new one is drawn where it is.
    Debris debris(std::make_shared<Random>(1));
    debris.Spark("A", Vector3::Zero(), 1, 1.f, 3, 3);
    auto piece = debris.Pieces()[0];
    CHECK(piece->CenterAt(Half) == piece->Center);
    CHECK(piece->OrientationAt(Half) == piece->Orientation);

    Vector3 center = piece->Center;
    Quaternion orientation = piece->Orientation;
    debris.Update(1.f);
    CHECK(piece->CenterAt(Half) == Vector3::Lerp(center, piece->Center, Half));
    CHECK(piece->OrientationAt(Half) == Quaternion::Slerp(orientation, piece->Orientation, Half));
    CHECK(piece->CenterAt(1.f) == piece->Center);
}

TEST_CASE("FrameBlendTests.EffectsAreDrawnBetweenTicks")
{
    // Issue #16: slime drops flew at 30 Hz. A new one is drawn where it is.
    Effects effects(std::make_shared<Random>(1));
    effects.FrameCount = [](const std::string&, const std::string&) { return 4; };
    effects.SpawnDrop("A", Vector3::Zero(), Vector3::UnitX(), 1.f);
    auto drop = effects.All()[0];
    CHECK(drop->PositionAt(Half) == drop->Position);

    effects.Update(1.f);
    CHECK(drop->PositionAt(Half) == Vector3(Half, 0.f, 0.f));
    CHECK(drop->PositionAt(1.f) == drop->Position);
}
