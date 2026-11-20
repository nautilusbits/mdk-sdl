#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Formats/Arena.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Collision/Solids.h"
#include "Mdk.Game/Kurt/Inventory.h"

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Kurt
{
    class CameraRoll;
    class Scope;

    // Whether Kurt takes damage (the console's god).
    enum class Mortality { Mortal, God };

    // Whether Kurt collides (the console's noclip turns it off).
    enum class Clipping { On, Off };

    // Kurt Hectic ("Damp" in the original code): walking, turning, jumping, falling and the
    // chute, through the arenas' BSPs. The original runs at 30 ticks per second; its constants are
    // converted to seconds (see godot-mdk docs/gameplay.md and docs/bsp.md).
    //   input ──► speeds (vel_accel, vel_friction) ──► walk move (slide 0.75)
    //                                              └─► fall move (slide 0.5) ──► on the floor?
    //   items, chain gun, knock-down (Kurt.Combat.cpp) ◄───────────────────────┘
    //                                              └─► state ──► sprite
    //
    // C# types the objects Kurt stands on, is kept by and touches as object; they are MdkObjects.
    class Kurt : public std::enable_shared_from_this<Kurt>
    {
    public:
        enum class State
        {
            Still, Idle, Run, Side, Turn, Jump, RunJump, Fall, Chute, Land, Shot, RunFire, Throw, Knocked, GetUp, Dead,
            Slip, Slide, SlideFast, SlideBrake, Hang, HardLand,
            // The 1996 demo's moves (Kurt.Beta.cpp).
            RollLeft, RollRight, HelmetOn, HelmetOff
        };

        static constexpr float Ticks = 30.f;

        Kurt(Collision::ArenaSpace& space, Audio::SoundMixer& mixer, std::function<int(State)> frameCount);
        ~Kurt();

        // ======== Kurt.cs ========

        Vector3 Feet{};
        // Degrees; 90 faces +Y (MDK).
        float Yaw = 0;
        float ForwardSpeed = 0;
        float StrafeSpeed = 0;
        float TurnRate = 0;
        float VerticalSpeed = 0;
        bool OnFloor = false;
        bool ChuteOpen = false;
        State Current = State::Still;
        // The chute closed in the air this tick: K_CHUTE's closing frame shows once.
        bool ChuteClosing = false;
        // The solid object boxes of Kurt's arena within a region (set by the scripts runtime).
        // The span stays valid until the next call (the runtime keeps one list).
        std::function<std::span<const Collision::Solids::Solid>(const Formats::Box&)> SolidsWithin;
        // The object Kurt stands on (0x573b84), or null.
        Objects::MdkObject* Platform = nullptr;
        float StateTime = 0;
        // Animation position in frames.
        float AnimationFrame = 0;
        std::shared_ptr<Game::Kurt::CameraRoll> CameraRoll;
        // Counts the teleports: a frame draws him where he is, not on the way there.
        int Teleports = 0;

        Vector3 Facing() const;
        Vector3 Right() const { return Vector3(Facing().Y, -Facing().X, 0.f); }
        // Velocity (u/s): walking, the push of a knock-down, falling.
        Vector3 Velocity() const { return Facing() * ForwardSpeed + Right() * StrafeSpeed + Vector3(_push, VerticalSpeed); }

        // One step of delta seconds.
        void Update(Engine::Platform::Input& input, float delta);

        // Moves Kurt (a script's teleport): he stops.
        void Teleport(Vector3 feet, float yaw);

        // Moves Kurt by offset (horizontal) with collision, sliding along what he meets (the
        // camera's push, damp_collide_move). Returns how far he went.
        Vector3 Shove(Vector3 offset);

        // Kurt's vertical speed changes (wind zones, push_kurt).
        void AddVerticalSpeed(float speed) { VerticalSpeed += speed; }

        // The platform Kurt stands on moved from from to to and turned by turn degrees: he
        // moves and turns with it.
        void Carry(Vector3 from, Vector3 to, float turn);

        // A frame's mouse turn (and the scope's tilt), at once rather than with the next step
        // (screens above 60 Hz), where the last step turned him by the mouse. Returns the yaw change
        // in degrees.
        float MouseLook(Engine::Platform::Input& input);

        // ======== Kurt.Beta.cs ========

        // The demo's moves are on (its levels).
        bool BetaMoves = false;

        // Walking backwards in the demo's levels: its own frames (K_BCKUP, a file its
        // executable never loads; the port's guess), played forwards.
        bool BackingUp() const { return BetaMoves && Current == State::Run && ForwardSpeed < 0.f; }

        // ======== Kurt.Cheats.cs ========

        Game::Kurt::Mortality Mortality = Game::Kurt::Mortality::Mortal;
        Game::Kurt::Clipping Clipping = Game::Kurt::Clipping::On;

        // ======== Kurt.Combat.cs ========

        static constexpr int MaxHealth = Inventory::MaxHealth;
        // Knocking down (damp_control 0x4664xx): the damage taken adds up (0x573b20, draining by
        // 2 per second, at most 5); at 5 Kurt is knocked down and invulnerable for 3 seconds. In the air
        // only within 13 units of a floor, and he's slammed onto it at 64 u/s.
        static constexpr float KnockdownDamage = 5.f;

        // A muzzle flash: the K_MUZZF frame, drawn behind Kurt with its hotspot at his plus the
        // offset (pixels, y down).
        struct MuzzleFlash
        {
            int Frame = 0;
            int X = 0;
            int Y = 0;

            bool operator==(const MuzzleFlash&) const = default;
        };

        // Health (0x574324).
        int Health = MaxHealth;
        // Red flash (0-255), and the skull's fade once dead.
        float HurtFlash = 0;
        // White flash of the screen (0x573b68: the nuke, screen_flash, explosions).
        float WhiteFlash = 0;
        // Invulnerability time in seconds (0x573bd4).
        float Invulnerable = 0;
        // Damage taken recently (0x573b20), see KnockdownDamage.
        float KnockDamage = 0;
        // The chain gun fires (0x573a38).
        bool Firing = false;
        std::optional<MuzzleFlash> Muzzle;
        // Cutscenes: he stands still, maybe hidden.
        bool Frozen = false;
        bool Visible = true;
        std::shared_ptr<Game::Kurt::Inventory> Inventory;
        // The skull has faded in: the original loads the last saved game.
        std::function<void()> Died;
        // The selected item leaves Kurt's hand (0x46ce78).
        std::function<void()> ItemUsed;
        // "Use" pressed while the World's Most Interesting Bomb is out (0x43f258).
        std::function<void()> BombTriggered;
        // Whether an item may be used now (no while a thrown item is active).
        std::function<bool(Game::Kurt::Inventory::Item)> CanUseItem;

        bool Knocked() const { return Current == State::Knocked || Current == State::GetUp; }

        // Damage from aliens (hurt_kurt 0x46a604): none while invulnerable, knocked down or dead.
        void Hurt(int damage);

        // Takes a pickup (damp_collect_pickups); returns its sound, or "" when it isn't taken.
        std::string Collect(const std::string& pickup);

        // Starts with another health than 100 (the fall's).
        void SetHealth(int health) { Health = Math::Max(health, 0); }

        // Knocks Kurt down (state 901): he stops firing, falls and gets up (K_BANG, K_BFLIP), and is
        // invulnerable for 3 seconds.
        void KnockDown(Vector2 push);

        void StopFiring();

        // Back on his feet with full health (after Died).
        void Revive();

        // ======== Kurt.Keeper.cs ========

        // The position of a platform that keeps Kurt, or null when it doesn't (set by the
        // scripts runtime).
        std::function<std::optional<Vector3>(Objects::MdkObject*)> Keeper;

        // ======== Kurt.Ride.cs ========

        enum class Walk { Normal, Riding };

        // Keys a walker ride reads: steering (forward or turn held) and fire.
        struct RideKeys
        {
            bool Steer = false;
            bool Fire = false;

            bool operator==(const RideKeys&) const = default;
        };

        // A frame shown instead of the state's.
        struct PoseFrame
        {
            std::string Name;
            int Frame = 0;

            bool operator==(const PoseFrame&) const = default;
        };

        // An arena triangle a move ran into.
        struct ArenaContact
        {
            std::shared_ptr<Formats::Arena> Arena;
            int Triangle = 0;

            bool operator==(const ArenaContact&) const = default;
        };

        // The camera's pivot above the feet (0x573b7c).
        static constexpr float DefaultPivot = 4.5f;

        // A ride's step (the snowboard, the bomber) instead of walking.
        std::function<void(Engine::Platform::Input&, float)> Ride;
        Walk Walking = Walk::Normal;
        RideKeys Keys;
        // A frame shown instead of the state's (the board's K_SURF, the end's K_TAKEOF).
        std::optional<PoseFrame> Pose;
        float CameraPivot = DefaultPivot;
        // The bomber's view: this high above Kurt, looking down (0x4183f0).
        std::optional<float> TopDownHeight;
        // Added to the camera pitch (degrees, negative looks up; 0x573b1c), and the end of a
        // level is lifting him (no tilt in the air).
        float CameraTilt = 0;
        bool Rising = false;

        // The objects Kurt's box touches (damp_collide_move's 0x573c2c), and his platform.
        const std::vector<Objects::MdkObject*>& Touched() const { return _touched; }

        // A ride's move at velocity (u/s): the walk move against the arena and the objects,
        // sticking to downhill floors, gravity and the fall.
        void Glide(Vector3 velocity, float delta);

        // The chain gun and its muzzle flash on the board.
        void UpdateGun(Engine::Platform::Input& input, float delta);

        // Gets off the XD2: a running jump forward (40 u/s up, 20 u/s ahead).
        void JumpOff();

        // Gets off the board or the bomber, keeping the ride's speeds (u/s) as his walking ones.
        void GetOffBoard(float forward, float strafe);

        // The arena triangles Kurt's moves ran into since the last call (0x46634e: his arena's
        // groups get a hit).
        const std::vector<ArenaContact>& TakeContacts();

        // ======== Kurt.Slide.cs ========

        // Extra gravity of the wind zones and the slide (u/s²; damp_gravity's adds to it).
        static constexpr float SlideGravity = 128.f;

        // Sliding (0x573be8).
        bool Sliding = false;

        // The slide's velocity (u/s; 0x573bf0).
        Vector2 SlideVelocity() const { return _slideVelocity; }

        // Starts the slide (0x468b64): the wind zones call this while Kurt is in their box.
        void StartSlide();

        // Kurt crossed into another arena (0x41c550): a slide ends 15 ticks later. E.g.
        // LEVEL6's slide drops him into OLYM_2 against a wall.
        void EnterArena();

        // In a wind zone's box in the air (opcode 224 sets 0x573bec): the wind pulls him down
        // and keeps his chute shut for the next step (damp_vertical 0x4694bc), so he lands in the box
        // and slides instead of gliding past it.
        void Blow(float dt);

        void StopSlide();

        // Adds to the slide velocity (slide_accel 0x468be0): a push above 0.1 accelerates by its
        // square, below -0.1 it slows the same way, in between the velocity brakes towards 0.
        void SlideAccel(Vector2 push, float dt);

        // ======== Kurt.Snapshot.cs ========

        // Kurt for a full save (the original's DAMP and PLAY; kurt.gd snapshot):
        // where he is, his health, his clip and his pickups. He comes back standing still.
        nlohmann::json Snapshot() const;

        void Restore(const nlohmann::json& data);

        // ======== Kurt.Sniper.cs ========

        // Whether leaving sniper mode plays SNIPEROFF.
        enum class SniperExit { Silent, WithSound };

        // Sniper mode is on (0x573a60); Kurt isn't drawn.
        bool Sniping = false;
        std::shared_ptr<Game::Kurt::Scope> Scope;
        // Fires a round of an ammo type from the scope; false when no round slot is free.
        std::function<bool(int)> SniperFire;

        // The eye in sniper mode.
        Vector3 SniperEye() const;
        // The line of sight in sniper mode.
        Vector3 SniperForward() const;

        // Enters sniper mode (damp_move 0x46883c): only standing on a floor, not knocked down,
        // sliding or throwing. Kurt stops firing and shows SNIPERON (state 803).
        void EnterSniper();

        // Leaves sniper mode (0x4645c8).
        void LeaveSniper(SniperExit exit = SniperExit::Silent);

        // ======== Kurt.Updraft.cs ========

        // Out of an updraft Kurt rises at most this fast.
        static constexpr float UpdraftExitSpeed = 40.f;

        // The fans (set by the scripts runtime): (vertical speed, seconds) → the new vertical
        // speed, or NaN outside them.
        std::function<float(float, float)> Updraft;

        // Whether a fan holds Kurt.
        bool InUpdraft = false;

    private:
        Collision::ArenaSpace& _space;
        Audio::SoundMixer& _mixer;
        std::function<int(State)> _frameCount;

        // ======== Kurt.cs ========

        // Forward, backward and strafe speeds (u/s) and accelerations (u/s²), normal and turbo.
        static constexpr float MaxSpeed = 0.6667f * Ticks;
        static constexpr float MaxSpeedTurbo = 1.3333f * Ticks;
        static constexpr float Acceleration = 0.04444f * Ticks * Ticks;
        static constexpr float AccelerationTurbo = 0.08889f * Ticks * Ticks;
        // Deceleration without input (u/s²): above MaxSpeed, and below it.
        static constexpr float FrictionFast = 0.17778f * Ticks * Ticks;
        static constexpr float FrictionSlow = 0.08889f * Ticks * Ticks;
        // Friction and strafe acceleration in the air.
        static constexpr float AirControl = 0.75f;
        // Keyboard turning (degrees/s, degrees/s²), normal and turbo.
        static constexpr float TurnSpeed = 4.f * Ticks;
        static constexpr float TurnSpeedTurbo = 6.f * Ticks;
        static constexpr float TurnAcceleration = 0.9f * Ticks * Ticks;
        static constexpr float TurnAccelerationTurbo = 1.3f * Ticks * Ticks;
        static constexpr float TurnFrictionFast = 1.6f * Ticks * Ticks;
        static constexpr float TurnFrictionSlow = 0.55f * Ticks * Ticks;
        static constexpr float MouseDegrees = 0.15f;
        // Vertical movement (u/s, u/s²).
        static constexpr float Gravity = 64.f;
        static constexpr float MaxFallSpeed = 250.f;
        static constexpr float JumpSpeed = 40.f;
        // Releasing jump during the first ticks of a jump cuts it short.
        static constexpr int JumpHoldTicks = 6;
        static constexpr float JumpReleasePenalty = 3.333f;
        // Falling faster than this starts the fall (or with jump held, opens the chute).
        static constexpr float FallStartSpeed = -16.f;
        // The jumps' frames (0x4646a4): the first ones play one a tick, then frame n waits for the
        // vertical speed to drop to JumpSpeeds[n]; past the last comes the fall.
        static constexpr int JumpTimedFrames = 10;
        static constexpr int JumpFrames = 16;
        static constexpr int RunJumpTimedFrames = 7;
        static constexpr int RunJumpFrames = 12;
        // The jumps' frame speeds (0x492030; the running jump's 0x492074 is the same to frame 12).
        static constexpr std::array<float, 16> JumpSpeeds =
            { 25.f, 26.f, 27.f, 20.f, 15.f, 13.f, 9.f, 6.6f, 4.f, 0.2f, -3.f, -6.3f, -10.f, -14.f, -17.f, -20.f };
        static constexpr float ChuteGravity = 21.33f;
        static constexpr float ChuteFallSpeed = -8.f;
        static constexpr float ChuteBrake = 256.f;
        // CHUTEON flaps after the chute's opening frames.
        static constexpr int ChuteOpenFrames = 4;
        static constexpr const char* ChuteOnSound = "CHUTEON";
        // damp_collide_move's slide factors: walking stops within 30° of head-on; falling slides off 45°.
        static constexpr float WalkSlide = 0.75f;
        static constexpr float FallSlide = 0.5f;
        // Landing slower than this keeps Kurt from creeping (0.35 u/tick).
        static constexpr float CreepSpeed = 0.35f * Ticks;
        // Downhill glue: on floors flatter than this the feet follow the slope down.
        static constexpr float GlueNz = 0.25f;
        // The glue's fastest drop (u/s), under the hard landing's: the landing that follows it
        // must not hurt.
        static constexpr float GlueMaxSpeed = 90.f;
        // Running, or standing, Kurt starts the fall animation only after this long (seconds) off
        // the floor: down a steep slope his feet leave it for a step or two at a time, and the animation
        // flipped between the run and the fall.
        static constexpr float FallGrace = 0.15f;
        // Falling onto a platform first goes to 0.05 above its top (damp_gravity).
        static constexpr float PlatformLift = 0.05f;
        // Objects this far around a move (and Kurt's height above it) may stop it.
        static Vector3 NearbyMargin() { return Vector3(1.f, 1.f, Collision::Solids::PlatformReach); }
        static constexpr float KurtHeight = 5.f;
        // Sunk into a floor between these (#30), Kurt climbs onto it: from what the walking
        // box steps over (its 0.5 lift) to his waist.
        static constexpr float SunkMin = 0.5f;
        static constexpr float SunkMax = KurtHeight / 2.f;
        // Landing faster than this is a hard landing: 10 damage and K_CRASHL (state 806).
        static constexpr float HardLandingSpeed = -100.f;
        static constexpr int HardLandingDamage = 10;
        // K_CRASHL plays at 2 ticks a frame; in the air it holds at tick 8.
        static constexpr int CrashTicksPerFrame = 2;
        static constexpr int CrashHoldTick = 8;
        // Kurt dies this far below his arena's lowest point.
        static constexpr float FallOutDepth = -50.f;
        static constexpr float IdleDelay = 6.f;
        static constexpr float MovingSpeed = 1.f;
        // The turning animation covers 45° of rotation.
        static constexpr float TurnFrameDegrees = 45.f;
        // Footsteps on these run frames, alternating two pairs of sounds (FOOT3/4 first).
        static constexpr int LeftStep = 0;
        static constexpr int RightStep = 13;
        // The same while running and firing (K_RUNFIR).
        static constexpr int FireLeftStep = 4;
        static constexpr int FireRightStep = 17;

        // How the last step turned Kurt by the mouse: a frame turns him so at once.
        enum class MouseTurn { None, Walk, Scope };

        // A platform's top and its object.
        struct PlatformHit
        {
            float Top = 0;
            Objects::MdkObject* Owner = nullptr;
        };

        Vector3 _floorNormal = Vector3::UnitZ();
        bool _footstepPair = true;
        int _jumpTicksLeft = 0;
        bool _jumpReleased = true;
        MouseTurn _mouseTurn = MouseTurn::None;
        // How long Kurt has been off the floor (seconds).
        float _airFor = 0;
        // A jump's frames that follow the speed reached its top (0x573a48): the chute may open,
        // without waiting to fall faster than FallStartSpeed.
        bool _pastJumpTop = false;

        void UpdateStep(Engine::Platform::Input& input, float delta);

        // The solid object boxes around Kurt and the point to.
        std::span<const Collision::Solids::Solid> Nearby(Vector3 to);

        // A platform the fall of dz reaches (damp_platform_floor), or null.
        std::optional<PlatformHit> FindPlatform(float dz);

        static float Axis(Engine::Platform::Input& input, Engine::Platform::Key positive, Engine::Platform::Key negative);

        void UpdateTurning(Engine::Platform::Input& input, bool turbo, float delta);

        // Towards input × max speed, or friction without input (vel_accel, vel_friction). The
        // opposite direction reverses at once.
        static float Accelerate(float speed, float input, bool turbo, float accelFactor, float frictionFactor, float delta);

        static float MoveToward(float from, float to, float step)
        {
            return MathF::Abs(to - from) <= step ? to : from + MathF::Sign(to - from) * step;
        }

        // Jumping, the chute, gravity and the fall move (damp_vertical 0x4694bc, damp_gravity 0x469efc).
        void UpdateVertical(Engine::Platform::Input& input, Vector3 move, float delta);

        // In the air: the jump's hold time runs out; held jump opens the chute when falling
        // fast or past a jump's top, letting go closes it.
        void UpdateAir(bool jumpHeld, float delta);

        // CHUTEON flaps while the chute is open (after its opening frames); closing stops it,
        // with CHUTEIN (damp_animate).
        void UpdateChuteSound();

        // CHUTEON stops without CHUTEIN when Kurt leaves the chute otherwise: death, a
        // teleport, a ride, a cutscene or the level's end (it played on until the level ended).
        void StopChuteSound() { _mixer.Stop(ChuteOnSound); }

        // On a floor flatter than 75°, walking downhill keeps the feet on it. With exact, the feet
        // follow the slope's whole drop (the move along the slope over the normal's z), not only its
        // share along the normal: on a steep floor the feet stayed above it every other step, and
        // Kurt flickered between the run and the fall.
        void GlueDownhill(Vector3 move, float delta, bool exact = false);

        // The fall move, then out of the arena: 50 below its lowest point Kurt dies
        // (damp_gravity 0x469efc; he counts as on a floor, so the death plays).
        void Fall(float delta);

        // The fall move: a hit going down lands, going up bumps the head; sliding keeps the
        // fall rate. A platform under the feet stops the fall at its top (damp_gravity 0x469efc).
        void FallMove(float delta);

        // Deviation from the original (#30): a floor without a side wall at its edge lets the
        // walking box in from the side (the BSP has faces, no volume), and every fall move then stops
        // at once on its top (bsp_sweep_node 0x409680, damp_collide_move 0x465e34): Kurt walked on 2
        // inside level 8's walkway until a jump. Sunk deeper than a step, up to his waist, he climbs
        // onto the top when there is room above. Not on a ride (the board, the bomber).
        void ClimbOut(const Collision::ArenaSpace::Result& result);

        void Land(Vector3 before, Vector3 normal, float delta);

        // A hard landing: 10 damage (no knock-down) and K_CRASHL; the board skips it.
        void HardLanding();

        // States from 800 on: climbing a ledge, or the hard landing's K_CRASHL with gravity
        // (held at tick 8 while still falling).
        void UpdateSpecial(float delta);

        void UpdateState(float forward, float strafe, float delta);

        void Animate(float delta);

        // A jump's frames (0x4646a4): timed, then following the vertical speed down through its
        // top, then the fall. Played through by time, the jump ended while Kurt still rose, and he hung on
        // its last frame until falling fast enough.
        void AnimateJump(float delta);

        static int Wrap(int frame, int count) { return ((frame % count) + count) % count; }

        void Footstep(int previous, int current);

        // damp_run_anim_frame: the run animation follows the speed, backwards when backing up.
        float RunRate() const;

        void SetState(State state);

        // ======== Kurt.Beta.cs ========

        // A roll moves Kurt sideways at 8 x 0.05 units per tick and turns him 90° the other way
        // over the animation: a quarter circle around what he faces.
        static constexpr float RollSpeed = 8.f * 0.05f * Ticks;
        static constexpr float RollTurn = 90.f;

        bool InBetaMove() const
        {
            return Current == State::RollLeft || Current == State::RollRight || Current == State::HelmetOn || Current == State::HelmetOff;
        }

        // Z or C on the floor starts a roll (nothing in the demo asks for one: the keys are the port's).
        bool StartRoll(Engine::Platform::Input& input);

        // The helmet goes on before sniper mode starts (state 703).
        void PutHelmetOn();

        // A step of a move that plays its animation through, a frame per tick: a roll moves Kurt
        // sideways while he turns the other way; the helmet goes on (then sniper mode starts) or off.
        void UpdateBetaMove(float delta);

        // ======== Kurt.Cheats.cs ========

        static constexpr float NoclipSpeed = 60.f;
        static constexpr float NoclipTurbo = 3.f;

        // A noclip step; returns false when Kurt collides as usual.
        bool UpdateNoclip(Engine::Platform::Input& input, float delta);

        // ======== Kurt.Combat.cs ========

        // A muzzle flash's offset (pixels).
        struct Offset
        {
            int X = 0;
            int Y = 0;
        };

        // Red flash after hits (0x573b70): +25 per damage point, kept within 75-180; flashes
        // fade by 4 per tick. Once Kurt is dead it's the skull's fade, rising by 2 per tick to 255.
        static constexpr float HurtFlashPerDamage = 25.f;
        static constexpr float HurtFlashMin = 75.f;
        static constexpr float HurtFlashMax = 180.f;
        static constexpr float FlashFade = 4.f * Ticks;
        static constexpr float SkullFade = 2.f * Ticks;
        static constexpr float SkullEnd = 255.f;
        static constexpr float KnockdownDrain = 2.f;
        static constexpr float KnockdownInvulnerability = 3.f;
        static constexpr float KnockdownFloorDistance = 13.f;
        static constexpr float KnockdownSlamSpeed = -64.f;
        // A push (push_kurt, 0x573c08) slows down by 0.1 u/tick per tick.
        static constexpr float PushDrain = 0.1f * Ticks * Ticks;
        // Damage on easy: 2/3 (at least 1); on hard: double (hurt_kurt 0x46a604).
        static constexpr int EasyNumerator = 2;
        static constexpr int EasyDenominator = 3;
        static constexpr int HardFactor = 2;
        // Frame of K_SPWEP at which the item leaves Kurt's hand (damp_animate).
        static constexpr int ThrowFrame = 8;
        // The chain gun's loop (0x46c3e4): MULTIFIRE, GATTFIRE with the super chain gun.
        static constexpr const char* GunSound = "MULTIFIRE";
        static constexpr const char* SuperGunSound = "GATTFIRE";
        // K_MUZZF has 4 frames; a random offset of 0-4 pixels is added to the state's.
        static constexpr int MuzzleFrameMask = 3;
        static constexpr int MuzzleJitter = 5;
        // Muzzle flash offsets (pixels) in the states that don't show the chain gun firing by
        // themselves (damp_animate); SHOT and RUN_FIRE have the flash in their frames.
        static const std::unordered_map<State, Offset>& MuzzleOffsets();
        // On the board (state 201, K_SURF) the flash is drawn at (-42, 12).
        static constexpr const char* SurfPose = "K_SURF";
        static constexpr Offset SurfMuzzle = { -42, 12 };
        static constexpr std::array<Engine::Platform::Key, 5> SlotKeys = { Engine::Platform::Key::Item1, Engine::Platform::Key::Item2,
            Engine::Platform::Key::Item3, Engine::Platform::Key::Item4, Engine::Platform::Key::Item5 };

        // Horizontal push (u/s) while knocked down.
        Vector2 _push{};
        int _gunVoice = 0;
        bool _gunSuper = false;
        float _muzzleTicks = 0;
        int _muzzleTick = -1;
        int _muzzleFrame = 0;
        Mdk::Random _random;
        // Keys held at the previous step, for presses.
        std::unordered_set<Engine::Platform::Key> _held;

        // Damage by the difficulty, its red flash, and the knock damage (0x46a77c).
        void Damage(int damage);

        static int Scaled(int damage, Difficulty difficulty);

        void FadeFlashes(float delta);

        void DrainPush(float delta)
        {
            _push = Vector2(MoveToward(_push.X, 0.f, PushDrain * delta), MoveToward(_push.Y, 0.f, PushDrain * delta));
        }

        // At 5 damage taken recently Kurt is knocked down: on a floor, or in the air above one.
        void UpdateKnockDamage(float delta);

        // Dead Kurt falls to the floor and lies still while the skull fades in, then Died.
        void UpdateDeath(float delta);

        // Whether a key went down since the last step.
        bool Pressed(Engine::Platform::Input& input, Engine::Platform::Key key);

        // Item keys (damp_move 0x46ca38): select a slot (1-5, next, previous) or use the
        // selected item: on the floor Kurt throws it, in the air it's used at once.
        void UpdateItems(Engine::Platform::Input& input);

        // Holding fire fires the chain gun (damp_move): its sound loops; the hits are the
        // scripts runtime's (fire_chain_gun).
        void UpdateFiring(Engine::Platform::Input& input);

        // Every other tick while firing, a random muzzle flash frame is drawn behind Kurt in the
        // states whose frames don't show it: the next frame is (previous + 1-3) & 3.
        void UpdateMuzzle(float delta);

        // The flash's offset in the current state or pose, or null when the frames show it.
        std::optional<Offset> MuzzleOffset() const;

        // The states that override walking: death, knock-down, throwing. Returns whether one did.
        bool UpdateCombatState(float delta);

        // ======== Kurt.Keeper.cs ========

        Objects::MdkObject* _keeper = nullptr;
        // His feet above the keeping platform (0x573a20).
        float _keptHeight = 0;

        bool Kept() const { return _keeper != nullptr; }

        // Lets go of a platform that no longer keeps him.
        void CheckKeeper();

        // The keeping platform as the fall's floor (damp_platform_floor: no scan), or null.
        std::optional<PlatformHit> KeptPlatform(float dz);

        // Landed on owner: kept by it if it keeps.
        void KeepOn(Objects::MdkObject* owner);

        // After his move: on the keeping platform's XY, not below his height on it.
        void StayOnKeeper();

        // ======== Kurt.Ledge.cs ========

        // The hands' height above the feet (0x49798c), and how far ahead they reach.
        static constexpr float LedgeHeight = 4.6041665f;
        static constexpr int LedgeReach = 3;
        // Kurt grabs while falling at least this fast (u/s).
        static constexpr float LedgeSpeed = -0.25f;
        // Only flat tops (|nz| ≥ 0.85), their edge faced within 30°.
        static constexpr float LedgeFlat = 0.85f;
        static constexpr float LedgeAngle = 30.f;
        // The yaw is the edge's direction - 90° (0x49799c).
        static constexpr float EdgeToYaw = -90.f;
        // Room above the edge: a box of half size (0.5, 0.5, 2), 2.5 above it, swept from 1
        // before it to 0.5 past it.
        static Vector3 LedgeRoom() { return Vector3(0.5f, 0.5f, 2.f); }
        static constexpr float LedgeRoomLift = 2.5f;
        static constexpr float LedgeRoomPast = 0.5f;
        // Kurt hangs this far before the edge.
        static constexpr float HangBack = 1.f;
        // Climbing (K_HANG, 2 ticks a frame): the height and the backward offset of each frame
        // pair (0x491f34, 0x491f74), scaled by 0.708333 × 0.5 per tick.
        static constexpr std::array<float, 16> ClimbHeights =
            { 0.374f, 0.326f, 0.292f, 0.311f, 0.677f, 1.263f, 2.754f, 4.172f, 4.903f, 5.343f, 5.711f, 6.001f, 6.212f, 6.345f, 6.406f, 6.417f };
        static constexpr std::array<float, 16> ClimbBack =
            { 0.685f, 0.383f, 0.167f, 0.254f, 0.635f, 1.053f, 0.847f, 0.514f, 0.17f, -0.125f, -0.501f, -0.825f, -1.066f, -1.221f, -1.293f, -1.305f };
        static constexpr float ClimbScale = 0.708333f * 0.5f;
        static constexpr int ClimbSteps = 15;
        static constexpr int TicksPerClimbFrame = 2;

        int _climbTicks = 0;

        // Grabs a ledge after a step that started at before with the forward key forward: the
        // hands' path crosses a flat top, Kurt faces its edge and there's room above it. Returns
        // whether he hangs.
        bool GrabLedge(Vector3 before, float forward);

        // Hangs from the edge of top towards Kurt (ahead back).
        bool HangFrom(const Collision::ArenaSpace::Surface& top, Vector3 ahead);

        // Climbing (state 800): K_HANG at 2 ticks a frame, moved by the tables, no gravity.
        void UpdateClimb(float delta);

        static float Wrap180(float degrees) { return degrees - 360.f * MathF::Floor((degrees + 180.f) / 360.f); }

        // ======== Kurt.Ride.cs ========

        std::vector<Objects::MdkObject*> _touched;
        std::vector<Objects::MdkObject*> _touchedNow;
        std::vector<ArenaContact> _contacts;
        // The contacts last taken (kept: no list per step).
        std::vector<ArenaContact> _taken;

        // In the XD2: gravity and the fall only; he stands, hidden.
        void RideWalker(Engine::Platform::Input& input, float forward, float delta);

        Collision::ArenaSpace::Result Contact(const Collision::ArenaSpace::Result& result);

        void UpdateTouched();

        // ======== Kurt.Slide.cs ========

        // The slope pushes by 10 × the smoothed floor normal; pushes within ±0.1 brake by 2 u/s².
        static constexpr float SlideSlope = 10.f;
        static constexpr float SlidePushDead = 0.1f;
        static constexpr float SlideFriction = 2.f;
        // The floor normal is smoothed by 0.8·old + 0.2·new horizontally, half and half in z.
        static constexpr float SlideNormalKeep = 0.8f;
        static constexpr float SlideNormalKeepZ = 0.5f;
        // Speed cap: 50, raised by 10/s up to 80 going forward, lowered by 25/s down to 15
        // braking, back towards 50 by 20/s.
        static constexpr float SlideCap = 50.f;
        static constexpr float SlideCapMin = 15.f;
        static constexpr float SlideCapMax = 80.f;
        static constexpr float SlideCapRise = 10.f;
        static constexpr float SlideCapFall = 25.f;
        static constexpr float SlideCapRelax = 20.f;
        // Forward accelerates by 35 u/s² (above 15 u/s), back brakes by 15 u/s² (down to
        // 15 u/s), turning turns by 45°/s.
        static constexpr float SlideAcceleration = 35.f;
        static constexpr float SlideBrake = 15.f;
        static constexpr float SlideMinSpeed = 15.f;
        static constexpr float SlideTurn = 45.f;
        // The mouse turns at most 4 × SlideTurn.
        static constexpr float SlideMouseTurn = 4.f * SlideTurn;
        // Kurt faces the slide once |vx| + |vy| is above this.
        static constexpr float SlideHeading = 0.5f;
        // A wall stops him when he moved less than half of a move over 0.5 units on an axis:
        // the slide follows what he moved, its speed averaged with it.
        static constexpr float SlideWallMove = 0.5f;
        static constexpr float SlideWallShare = 0.5f;
        static constexpr float SlideAirTicks = 20.f;
        // Ticks of sliding left after entering another arena.
        static constexpr float SlideArenaTicks = 15.f;
        // At rest: no speed and no push left (within this).
        static constexpr float SlideRest = 1e-5f;
        // BUTSLIDE plays at 11025 Hz, at 15000 Hz going forward; BUTBRAKE while braking.
        static constexpr float SlideFastPitch = 15000.f / 11025.f;
        static constexpr const char* SlideSound = "BUTSLIDE";
        static constexpr const char* BrakeSound = "BUTBRAKE";

        Vector2 _slideVelocity{};
        float _slideCap = SlideCap;
        Vector3 _slideNormal = Vector3::UnitZ();
        Vector2 _slidePush{};
        float _slideAir = 0;
        // Ticks left after an arena change (0x573be8 below 0); 0: none.
        float _slideLeft = 0;
        // A wind zone blew on him since his last step (0x573bec).
        bool _blown = false;
        int _slideVoice = 0;
        std::string _slideSound;

        static float Accel(float speed, float push, float dt);

        // One step of the slide. Returns whether it took the step (false: it ended, walk on).
        bool UpdateSlide(Engine::Platform::Input& input, float delta);

        // The speed within the cap; forward speeds up and raises the cap, back brakes and
        // lowers it, without input it goes back to 50.
        float SlideSpeed(float forward, float delta);

        // Kurt faces where the slide goes; turning (keys or mouse) turns it.
        void Steer(Engine::Platform::Input& input, float speed, float delta);

        // The slide's move; a wall that stops it turns it along what Kurt moved.
        void SlideMove(float delta);

        static bool Stopped(float planned, float moved)
        {
            return (planned > SlideWallMove && moved < planned * SlideWallShare) || (planned < -SlideWallMove && moved > planned * SlideWallShare);
        }

        // K_SLIP once, then K_SLIDE, K_FSLIDE or K_BSLIDE by the forward key; their sounds.
        void UpdateSlideState(float forward, float delta);

        // ======== Kurt.Sniper.cs ========

        // Sidestepping only, at a quarter of the running speed.
        static constexpr float SniperStrafe = 0.25f;
        // Falling this fast (or rising) ends sniper mode.
        static constexpr float SniperFallSpeed = -30.f;
        static constexpr float SniperRiseSpeed = 1.f;

        int _breathVoice = 0;
        int _zoomVoice = 0;

        bool CanSnipe() const
        {
            return OnFloor && !Sliding && Health != 0 && !Knocked() && Current != State::Dead && Current != State::Throw;
        }

        // The sniper key, then a sniper step. Returns whether the step was a sniper one.
        bool UpdateSniper(Engine::Platform::Input& input, bool turbo, float delta);

        // The turn and forward/back keys (or the mouse) turn and tilt the view; the zoom keys
        // (or the wheel) zoom; the item keys pick the ammo (0x467384).
        void SniperControls(Engine::Platform::Input& input, bool turbo, float delta);

        // Kurt sidesteps at a quarter of the running speed and still falls.
        void SniperMove(Engine::Platform::Input& input, bool turbo, float delta);

        // ======== Kurt.Updraft.cs ========

        static constexpr const char* FanSound = "FAN";

        int _fanVoice = 0;

        void UpdateUpdraft(float delta);
    };
}
