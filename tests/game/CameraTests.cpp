#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include <doctest.h>

#include "Asserts.h"
#include "core/DotNet.h"
#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "engine/platform/Input.h"
#include "game/Angles.h"
#include "game/audio/SoundMixer.h"
#include "game/collision/ArenaSpace.h"
#include "game/kurt/FollowCamera.h"
#include "game/kurt/Kurt.h"

using namespace Mdk;
using namespace Mdk::Engine::Audio;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game::Audio;
using namespace Mdk::Game::Collision;
using Mdk::Game::Kurt::FollowCamera;
using KurtPlayer = Mdk::Game::Kurt::Kurt;

// The follow camera's pitch (camera_update 0x4174d0): the climb term −40 × the smoothed
// rise of Kurt's feet per tick (0x490db8).

namespace
{
    constexpr float Step = 1.f / 60.f;
    constexpr float ArenaPitch = 4.f;
    // The rise per tick is clamped to 0.5: the term tops out at −20°.
    constexpr float MaxClimbPitch = -20.f;
    // Mouse motion per step far past the camera's pitch limits.
    constexpr float FullLook = 1000.f;
    // The view's least share along Kurt's facing: about 1° short of straight down.
    constexpr float MinAhead = 0.01f;
    // The mouse's look stops here: further down Kurt's sprite lies flat under the camera,
    // further up the camera sinks towards his feet.
    constexpr float LookDownLimit = 60.f;
    constexpr float LookUpLimit = -30.f;

    AudioDevice& Device()
    {
        static AudioDevice device(Output::Muted);
        return device;
    }

    // The camera, Kurt (with the space and mixer he refers to) and the camera's pitch.
    struct After
    {
        std::shared_ptr<ArenaSpace> Space;
        std::shared_ptr<SoundMixer> Mixer;
        std::shared_ptr<FollowCamera> Camera;
        std::shared_ptr<KurtPlayer> Kurt;
        float Pitch = 0;
    };

    // The camera, and its pitch, after Kurt's floor rose and the mouse moved down
    // mouseDown each step.
    After CameraAfter(float risePerTick, float seconds, float mouseDown = 0.f)
    {
        After after;
        after.Space = std::make_shared<ArenaSpace>();
        after.Mixer = std::make_shared<SoundMixer>(Device(), [](const std::string&) { return std::optional<SoundMixer::Entry>(); });
        after.Kurt = std::make_shared<KurtPlayer>(*after.Space, *after.Mixer, [](auto) { return 1; });
        after.Kurt->OnFloor = true;
        after.Camera = std::make_shared<FollowCamera>();
        auto& kurt = *after.Kurt;
        auto& camera = *after.Camera;
        Input input;
        for (float t = 0.f; t < seconds; t += Step)
        {
            kurt.Feet += Vector3(0.f, 0.f, risePerTick * KurtPlayer::Ticks * Step);
            input.ClearMouse();
            input.AddMouseMotion(0.f, mouseDown);
            camera.Update(kurt, ArenaPitch, input, Step);
        }

        // Positive pitch looks down.
        after.Pitch = -Mdk::Game::RadiansToDegrees(MathF::Asin(camera.Forward.Z));
        return after;
    }

    // Kurt standing on a floor that rises risePerTick each tick.
    float PitchAfter(float risePerTick, float seconds) { return CameraAfter(risePerTick, seconds).Pitch; }
}

TEST_CASE("CameraTests.StandingStillKeepsTheArenaPitch")
{
    CHECK_PRECISION(ArenaPitch, PitchAfter(0.f, 3.f), 2);
}

TEST_CASE("CameraTests.ClimbingTiltsTheViewUp")
{
    // Rising 1 unit a tick (clamped to 0.5) for 5 s: the smoothed rise nears 0.5.
    CHECK_PRECISION(ArenaPitch + MaxClimbPitch, PitchAfter(1.f, 5.f), 0);
}

TEST_CASE("CameraTests.FallingTiltsTheViewDown")
{
    auto pitch = PitchAfter(-0.25f, 5.f);
    CHECK(pitch >= ArenaPitch + 9.f);
    CHECK(pitch <= ArenaPitch + 10.5f);
}

TEST_CASE("CameraTests.LookingFullyUpOrDownStaysUpright")
{
    const std::tuple<float, float> data[] = {{0.f, FullLook}, {0.f, -FullLook}, {-0.25f, FullLook}};
    for (auto [risePerTick, mouseDown] : data)
    {
        CAPTURE(risePerTick);
        CAPTURE(mouseDown);
        // The mouse pushed past the limit (and falling tilting further down) never turns the view
        // over: it still looks a little the way Kurt faces (issue #11).
        auto after = CameraAfter(risePerTick, 3.f, mouseDown);
        auto ahead = Vector3::Dot(after.Camera->Forward, after.Kurt->Facing());
        CHECK(ahead > MinAhead);
    }
}

TEST_CASE("CameraTests.FramesBetweenStepsStayUprightLookingFullyUpOrDown")
{
    for (float mouseDown : {FullLook, -FullLook})
    {
        CAPTURE(mouseDown);
        // The frames drawn between steps (above 60 Hz) place the camera again: looking all the way
        // down they turned the view over, and Kurt jumped from the middle to the top of the screen.
        auto after = CameraAfter(0.f, 3.f, mouseDown);
        auto& kurt = *after.Kurt;
        after.Camera->Blend(kurt, kurt.Feet, kurt.Yaw, 1.f);
        auto ahead = Vector3::Dot(after.Camera->Forward, kurt.Facing());
        CHECK(ahead > MinAhead);
    }
}

TEST_CASE("CameraTests.LookingFullyUpOrDownStopsAtTheLimit")
{
    const std::tuple<float, float, float> data[] = {
        {0.f, FullLook, LookDownLimit},
        {-0.25f, FullLook, LookDownLimit},
        {0.f, -FullLook, LookUpLimit},
        {1.f, -FullLook, LookUpLimit},
    };
    for (auto [risePerTick, mouseDown, limit] : data)
    {
        CAPTURE(risePerTick);
        CAPTURE(mouseDown);
        // Falling or climbing tilts the view too: the mouse's look stops at the same pitch.
        CHECK_PRECISION(limit, CameraAfter(risePerTick, 3.f, mouseDown).Pitch, 0);
    }
}
