#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string_view>

#include "core/Numerics.h"
#include "engine/platform/Input.h"

namespace Mdk::Game::Kurt
{
    class Kurt;
}

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;

    // Kurt on the snowboard of level 4 (XSNOWB, snowboard_update 0x46ac4c;
    // snowboard.gd, godot-mdk docs/gameplay.md "The snowboard"). The board's path only guides the
    // heading H, which turns towards the path at 40°/s (faster carving into the turn, slower
    // against it). Kurt steers with the carve angle S (±30°), which slides him sideways
    // (L), and speeds up or brakes (V); slopes speed him up or slow him down. The board
    // sits under his feet at the yaw H + S.
    //
    //   input ──► S ──► L (sideways)     path ──► H (heading)     slope, keys ──► V (forward)
    class Snowboard
    {
    private:
        // Kurt::Kurt::Ticks (defined in the .cpp: Kurt.h isn't included here).
        static const float Ticks;
        // Steering: ±30° at 4°/tick (reached over 15 ticks on the ground), back at 3°/tick.
        static constexpr float SteerMax = 30.0f;
        static constexpr float SteerRate = 4.0f;
        // The mouse: 4 × clamp(dx / dt, ±4) °/tick (input_read_axes 0x408334), dx in the
        // original's units: its walk turns 3° a unit, the port's 0.15° a mouse unit.
        static constexpr float MouseUnits = 0.05f;
        static constexpr float MouseLimit = 4.0f;
        static constexpr float SteerRampTicks = 15.0f;
        static constexpr float SteerReturn = 3.0f;
        // Sideways speed L = −S·V/96, braked in the air by 0.0055556 per tick.
        static constexpr float SideFactor = -0.8333f * 0.0125f;
        static constexpr float SideAirBrake = 0.0055556f;
        // Speeds (units/tick): cruise 1.5, up to 2.5 with the key (or locked), down to 1.1667
        // braking or uphill, up to 2.6667 downhill; 0.05 per tick², decay above cruise 0.0027778.
        static constexpr float Cruise = 1.5f;
        static constexpr float Fast = 2.5f;
        static constexpr float Slow = 1.16667f;
        static constexpr float Downhill = 2.66667f;
        static constexpr float Acceleration = 0.05f;
        static constexpr float Decay = 0.0027778f;
        static constexpr float SlopeAcceleration = 0.066667f;
        // The thrown board falls by 64 u/s².
        static constexpr float Gravity = 64.0f;
        // The board's ends are 4 units from its middle; ground up to 3 units above them counts.
        static constexpr float BoardHalf = 4.0f;
        static constexpr float RayHeight = 3.0f;
        static constexpr float RayBelow = 0.01f;
        // The board pitches towards the ground at 90°/s, between −45° and 45°.
        static constexpr float PitchRate = 90.0f;
        static constexpr float PitchLimit = 45.0f;
        static constexpr float HalfTurn = 180.0f;
        static constexpr float FullTurn = 360.0f;
        // Carving tilts the nose up by a quarter of |S|.
        static constexpr float CarveTilt = 0.25f;
        // Jump: 28.5 units/s up, the key counts up to 12 ticks before landing; not at the end of CMEAT_1.
        static constexpr float JumpSpeed = 28.5f;
        static constexpr float JumpLift = 1.0f;
        static constexpr int JumpBuffer = 12;
        static constexpr float JumpHeld = 50.0f;
        static constexpr std::string_view NoJumpArena = "CMEAT_1";
        static constexpr float NoJumpY = 2131.0f;
        // Heading: 4/3 °/tick ± S/30 on the ground, 0.4 in the air; Kurt's yaw follows it at 360°/s.
        static constexpr float HeadingRate = 1.33333f;
        static constexpr float HeadingCarve = 1.33333f / 30.0f * 0.75f;
        static constexpr float HeadingAir = 0.4f;
        static constexpr float KurtTurn = 360.0f;
        // The path search's steps (path frames), and the look ahead for its heading.
        static constexpr std::array<float, 2> PathSteps = {1.0f, 0.2f};
        static constexpr float PathAhead = 0.2f;
        // Kurt nudged back towards the path when stuck (moving under 0.5 units/tick at speed).
        static constexpr float UnstickSpeed = 0.5f;
        static constexpr float UnstickStep = 2.0f;
        // The board sits 0.25 below Kurt's feet; thrown at 1.1 × his speed.
        static constexpr float BoardDrop = 0.25f;
        static constexpr float ThrowFactor = 1.1f;
        // Sounds: SKITURN beyond 22.5°, SKILAND after 15 ticks in the air, SKI stops after 7.
        static constexpr float TurnSound = 22.5f;
        static constexpr float LandTicks = 15.0f;
        static constexpr float QuietTicks = 7.0f;
        static constexpr std::array<std::string_view, 3> Sounds = {"SKI", "SKITURN", "SKILAND"};
        // Ramming: objects without these flags and with 0 < health < 65000 die; Kurt takes 5.
        static constexpr int RamSpared = 0x40304000;
        static constexpr int RamDamage = 5;
        // K_SURF loops; K_SURFJ holds on frame 5 in the air, lands from frame 6 to 10.
        static constexpr std::string_view SurfFrames = "K_SURF";
        static constexpr std::string_view JumpFrames = "K_SURFJ";
        static constexpr int SurfLoop = 8;
        static constexpr int JumpHoldFrame = 5;
        static constexpr int JumpLandFrame = 6;
        static constexpr float LateLanding = 3.0f;
        static constexpr float LandEnd = 11.0f;
        // The camera pivot dips by 0.2 a frame while K_SURFJ takes off, then comes back up by 1 u/s.
        static constexpr float PivotDip = 0.2f;

        enum class Pose { Surf, Jump, Land };

    public:
        std::shared_ptr<Objects::MdkObject> Board;

        Snowboard(ScriptRuntime& runtime, std::shared_ptr<Objects::MdkObject> board);

        // Forward speed (u/tick), for tests.
        float Speed() const { return _speed; }

        // One step of riding (instead of Kurt's walking).
        void Update(Engine::Platform::Input& input, float delta);

        // Getting off (the script clears the rideable flag, or Kurt died): he keeps the board's
        // speeds as his walking ones, and the board is thrown on at 1.1 × his velocity.
        void GetOff();

        // The carve angle S (positive left): the turn input right
        // (°/tick, > 0 right) turns it, ramping up over 15 ticks on the ground; the other way snaps
        // it straight; without input it goes back at 3°/tick.
        static float Steer(float steer, float right, bool grounded, float& turnTicks, float dt);

        // The forward speed after a step: forward > 0 speeds up, < 0 brakes, 0 cruises.
        static float Accelerate(float speed, float forward, float dt);

        // Slopes: nose down (pitch above 180) speeds up to 2.6667, nose up slows down to 1.1667.
        static float SlopeSpeed(float speed, float pitch, float dt);

        // The camera pivot at K_SURFJ frame frame (0x46ac4c), coming from
        // pivot after seconds.
        static float JumpPivot(float frame, float pivot, float seconds);

        // The turn input I (°/tick, > 0 right; 0x5014ec): the larger of the turn and
        // strafe keys × 4 (the strafe on a tie), or the mouse's 4 × clamp(dx / dt, ±4) when it moved.
        static float TurnInput(Engine::Platform::Input& input, float dt);

    private:
        ScriptRuntime* _runtime;
        std::shared_ptr<Kurt::Kurt> _kurt;
        float _speed = 0.0f;
        float _side = 0.0f;
        float _heading = 0.0f;
        float _steer = 0.0f;
        float _pitch = 0.0f;
        float _turnTicks = 0.0f;
        float _jumpTicks = 0.0f;
        float _airTicks = 0.0f;
        bool _grounded = true;
        float _pathHeading = 0.0f;
        Pose _pose = Pose::Surf;
        float _frame = 0.0f;

        // Speed on the ground: the keys speed up (to 2.5) or brake (to 1.1667), else back to
        // cruise 1.5; locked controls speed up to 2.5 on their own. Carving slides sideways.
        void UpdateSpeed(float forward, bool controls, float dt);

        // The board's pitch from the ground under its ends, turning at 90°/s; any ground under
        // the board means it's grounded.
        void UpdatePitch(float delta);

        // The ground under a board end: up to 3 units above it, or null.
        std::optional<Vector3> Ground(Vector3 end);

        // Jumping: on the ground, within 12 ticks of pressing the key.
        void Jump(Engine::Platform::Input& input, bool controls, float dt);

        void UpdateSounds(float dt);

        // The heading follows the board's path: the path time moves on to the point nearest to
        // Kurt, the direction there is the path's heading; a stuck Kurt is nudged towards the path.
        void FollowPath(Vector3 previous, float dt);

        // Moves a stuck Kurt a step towards to.
        void Nudge(Vector3 to, Vector2 kurt);

        Vector3 PathPoint(float t);

        float Distance(float t, Vector2 kurt);

        // The heading turns towards the path's (faster carving into the turn); Kurt faces the heading.
        void Turn(float delta, float dt);

        static float TurnTowards(float from, float to, float step);

        // The board under Kurt's feet at the yaw H + S, its nose up while carving.
        void Place(float delta);

        // Whatever Kurt runs into dies, and he takes 5 damage.
        void Ram();

        // K_SURF loops; K_SURFJ holds on frame 5 in the air and lands on frames 6-10.
        void Animate(float dt);

        static float Axis(Engine::Platform::Input& input, Engine::Platform::Key positive, Engine::Platform::Key negative);

        static float MoveToward(float from, float to, float step);

        static float Wrap360(float degrees);

        static float WrapHalf(float degrees);
    };
}
