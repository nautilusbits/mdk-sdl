#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Game/Collision/Bsp.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"

namespace Mdk::Game::Objects
{
    class MdkObject;
}

namespace Mdk::Game::Scripts
{
    // Moves scripted objects each tick like the original object update (0x43c7dc): spline
    // paths (0x43c258), movement commands (0x45b6c8), gravity (0x45e74c), then friction and velocity
    // with arena collisions (0x45e810, a BSP box sweep 0x45fec4). A port of godot-mdk's
    // object_motion.gd (see its docs/engine.md).
    //
    //   path ─► command (walk, fly, chase...) ─► swing ─► gravity ─► velocity + push ─► BSP sweep
    //        ─► item / pickup / cow ─► animation (+ frame sound) ─► roll, bank
    class ObjectMotion
    {
    public:
        explicit ObjectMotion(ScriptRuntime& runtime) : _runtime(&runtime) {}

        // Tick time in seconds and in ticks.
        static constexpr float Dt = ScriptRuntime::Tick;
        static constexpr float Ticks = 1.0f;
        // Turn speed (degrees per second) walking towards a waypoint or chasing.
        static constexpr float TurnSpeed = 180.0f;

    private:
        static constexpr float TerminalVelocity = -220.0f;
        // Walkers slow down to half speed when the waypoint is more than this angle away.
        static constexpr float SlowTurnAngle = 80.0f;
        // Objects falling this far below their arena's floor are killed.
        static constexpr float FallOutDepth = 200.0f;
        // Lines of sight for detours start this high above the object and the destination.
        static constexpr float PlanHeight = 8.0f;
        // A path key: s32 frame, position, in and out tangents (3 floats each).
        static constexpr int PathKeySize = 40;
        static constexpr int PathVectorSize = 12;
        static constexpr int PathCountSize = 4;
        static constexpr int PathFrameSize = 4;
        // The BSP sweep of objects (0x45fec4): passes after the first, and the slide factor ❓
        // (the caller's; Kurt walks with 0.75).
        static constexpr int SweepIterations = 2;
        static constexpr float SweepSlide = 0.75f;
        // Bouncing objects reflect their velocity along the normal (0x45e810).
        static constexpr float Bounce = -1.8f;
        // Contact flag cleared with the collision flags each step.
        static constexpr int ContactCleared = 0x10;
        static constexpr int LevelSix = 6;
        static constexpr int MortarItem = 4;
        // Kurt's thrown items (but not active, collected or on gravity flags 0x44000) lose most
        // of their horizontal speed in an updraft.
        static constexpr int ThrownFlagsMask = 0x45000;
        static constexpr float UpdraftBrake = 0.1f;

    public:
        // One tick of an object's motion.
        void Update(Objects::MdkObject& obj);

        // Spline paths: u32 key count, then keys of 40 bytes: s32 frame, position, in tangent and out
        // tangent. Segments are cubic Hermite curves.

        int PathKeyCount(int path);

        int PathKeyFrame(int path, int key);

    private:
        Vector3 PathVector(int path, int key, int field);

        // Fans lift objects with gravity (0x45e74c; not rolling objects in LEVEL6); Kurt's thrown
        // items (but the mortar) also lose most of their horizontal speed in them.
        void Updraft(Objects::MdkObject& obj);

        // path_speed_by_kurt (opcode 164, 0x43c258): the path speed goes towards the speed for
        // Kurt being ahead of, around, or behind the set distance (along the object's heading), by
        // (near - far) × 0.5 per second.
        void UpdatePathSpeed(Objects::MdkObject& obj);

    public:
        // Position on a path at a time (in frames), relative to its origin (spline_eval 0x43c0f8).
        Vector3 PathPosition(int path, float time);

    private:
        void UpdatePath(Objects::MdkObject& obj);

    public:
        // Starts following a path (follow_path): time in frames, origin = the path's offset.
        void StartPath(Objects::MdkObject& obj, int path, float time, Vector3 origin);

        // Movement commands.

    private:
        void UpdateCommand(Objects::MdkObject& obj);

        // Rolling (0x4602c8): the object turns by distance / (2π × radius) turns about the
        // horizontal axis across its motion.
        static void Roll(Objects::MdkObject& obj);

        // A rotation's axes made unit and perpendicular again (Gram-Schmidt): float products
        // drift, skewing and scaling a long-rolling model.
        static Matrix4x4 Orthonormal(const Matrix4x4& m);

    public:
        // turn_and_jump_to_dest (opcode 229, 0x460d24): turns towards the destination by at most
        // the rate (degrees per second); once facing it, jumps there (movement command 229). The top of
        // the jump is 10 above the higher end and at least 30 above the lower one; the flight time allows
        // for the friction slowing the object down.
        void TurnAndJump(Objects::MdkObject& obj, float rate);

    private:
        // A chain link (movement command 30, spawn_chain): the links hang off the head object,
        // each turned 90° more than the one before, with its first reference point on the previous one's
        // second (or its only one: LEVEL7's ship XE has just the hook under it, and its bomb or soldiers
        // hung from its centre, inside it). Only the first link (id 0) places the chain; a link whose
        // leader or any link before it is gone detaches and stops.
        void UpdateChain(Objects::MdkObject& link);

        // The live followers of leader by instance id, in a list kept from
        // call to call (an insertion sort: stable, like OrderBy).
        std::vector<std::shared_ptr<Objects::MdkObject>> Links(Objects::MdkObject& leader);

        std::vector<std::shared_ptr<Objects::MdkObject>> _links;

        // A pendulum (0x43cfe8, flag 0x400000 set by jump_to): the pitch swings by
        // ω -= sin θ·k·t, θ += ω·k·t (t in ticks) and the object hangs on its rope below the pivot, in the
        // vertical plane of its yaw. At each turning point the plane turns towards Kurt by at most 15°
        // (folded to ±90°: either side will do), at 10° per second.
        //        pivot
        //          │ \  rope
        //          │  \ θ = pitch
        //          │   ● object
        void Swing(Objects::MdkObject& obj);

    public:
        // plan_move (0x45a1dc), when a walk or flight starts (move_to, move_near_target,
        // move_to_bomb, command_objects 43): if the line from the object to the destination (both 8 units
        // up) is blocked, tries detour points on either side of its middle, 0.1 to 1.1 times its length
        // away, and takes the first one seen from both ends. The original offsets them along (sin a, cos a)
        // for a heading a, which is only sideways when the line runs along an axis.
        void PlanMove(Objects::MdkObject& obj);

    private:
        static constexpr std::array<float, 2> Sides = {-1.0f, 1.0f};

        // The stuck replan (0x45a434): an object heading to a detour gives it up and heads for the
        // destination again; one heading for the destination tries detours around the point 3/4 of the
        // way there, then around itself (0.1 to 1.9 times the distance, both sides).
        void Replan(Objects::MdkObject& obj);

        static Vector3 Detour(Vector3 a, Vector3 b, Vector3 origin, float amount);

        bool Clear(Vector3 a, Vector3 point, Vector3 b);

        enum class Moving { Walking, Flying };

        // Stuck detection after walking or flying (0x45a7d4, 0x45ae74). Walking with command 43,
        // any collision counts as stuck. Otherwise an object that collides while faster than 2 is stuck
        // when it has moved less than half its speed over a window of 16 ticks (9 walking with 197).
        static void DetectStuck(Objects::MdkObject& obj, Moving moving);

        static void ResetStuck(Objects::MdkObject& obj);

        // A stuck object that isn't turning (less than 3° this update, 0x45b6c8) replans once;
        // after that move_to (78) slides straight to the waypoint through everything and the others give
        // up (if_move_idle_flag sees StuckCount). Command 197 gives up at once.
        void HandleStuck(Objects::MdkObject& obj, float turned);

        // Automatic banking (0x43b65c): objects lean into their turns, roll = (roll - turn per
        // tick) × 0.95 within ±10° (the turn counted at most 2° per tick), unless flag 0x80 (set_banking
        // 0) or flag 1 is set.
        static void Bank(Objects::MdkObject& obj);

    public:
        // Turns the object towards a point by at most TurnSpeed × dt. Returns the angle that was
        // left to turn (before turning).
        static float TurnTowards(Objects::MdkObject& obj, Vector3 point);

    private:
        static void Accelerate(Objects::MdkObject& obj, float targetSpeed);

        static void Stop(Objects::MdkObject& obj);

        // The speed while turning: half when the waypoint is far to the side.
        static float TurnSpeedFactor(float diff);

        // Walks on the ground towards the waypoint, then the destination (0x45a7d4).
        static void Walk(Objects::MdkObject& obj);

        // Flies towards the waypoint, then the destination (0x45ae74).
        static void Fly(Objects::MdkObject& obj);

        // Chases a point, flying at a speed in units per tick (movement command 6, 0x45e448).
        static void Chase(Objects::MdkObject& obj, Vector3 point);

        // Keeps a formation position around the leader (movement command 1).
        static void FollowFormation(Objects::MdkObject& obj);

    public:
        // Position of a formation offset (obj+0x12c) around a leader.
        static Vector3 FormationPosition(Objects::MdkObject& leader, Vector3 offset);

    private:
        // Moves along the yaw and pitch (movement command 88, set_move_x).
        static void MoveForward(Objects::MdkObject& obj);

        // Stays attached to another object (movement command 74, attach_to): reference point A
        // of this object is kept on point B of the other one.
        static void FollowAttachment(Objects::MdkObject& obj);

        // Flies straight along the yaw and pitch until the lifetime (obj+0x302) runs out or
        // something is hit (movement command 61, 0x45b6c8).
        void FlyProjectile(Objects::MdkObject& obj);

        // Velocity.

        // Applies friction, then moves by the velocity and this frame's push, colliding with the
        // arena when the object has FlagCollides (0x45e810).
        void ApplyVelocity(Objects::MdkObject& obj);

        // The collision bounds of an object without a model.
        static constexpr Formats::Box DefaultBounds = {Vector3(-1.0f, -1.0f, 0.0f), Vector3(1.0f, 1.0f, 4.0f)};

        // Sweeps the object's collision box by the motion through its arena's BSP (0x45fec4),
        // sliding along what it hits. Returns the plane normal of the first hit, or null.
        //   box: half extents = world bounds × 0.25 across, half the origin-to-top height,
        //        bottom 0.05 above the origin when moving vertically (0.5 otherwise),
        //        lowered by the height offset when moving vertically
        std::optional<Vector3> Sweep(Objects::MdkObject& obj, Vector3 motion);

        // The sweep's first contact's normal (one delegate for every sweep: no closure per move).
        void FirstContact(Collision::Bsp::Contact contact);

        std::function<void(Collision::Bsp::Contact)> _onContact;
        std::optional<Vector3> _firstContact;
        std::shared_ptr<Collision::Bsp> _sweeping;

        ScriptRuntime* _runtime;
    };
}
