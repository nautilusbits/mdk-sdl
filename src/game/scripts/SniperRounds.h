#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "core/Numerics.h"
#include "game/objects/MdkObject.h"

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;

    // Kurt's sniper rounds (3 slots at 0x573c98, updated by 0x462708 after the objects; a port
    // of godot-mdk's sniper_rounds.gd, docs/gameplay.md "Sniper mode"). A round starts at the eye along
    // the view and is tested every tick along the segment it moved: objects (their boxes, then their
    // parts' faces), then the arena.
    //
    //   type 0  bullet          1100 u/s, 75 ticks, 8 damage
    //   type 1  homing bullet    400 u/s, 240 ticks, steers at the locked object
    //   type 2  grenade         as the bullet, explodes
    //   type 3  homing grenade  as the homing bullet, explodes
    //   type 4  mortar          thrown at 150 u/s, drag, gravity, bounces; bomb_follow_path guides it
    //
    //   FREE ──fire──► FLYING ──object──► HIT │ KILLED (camera watches 30 ticks)
    //                    │    ──wall, end─► DEAD (miss) │ EXPLODED ──linger──► FREE
    class SniperRounds
    {
    public:
        static constexpr int Slots = 3;

        // What a round's camera shows (0x461bcc).
        enum class Shot { Empty, Watching, Hit, Kill, Miss };

        // A round camera: from behind the round while it watches, then a colour or the miss
        // animation (Time: ticks since the miss).
        struct RoundCamera
        {
            SniperRounds::Shot Shot = SniperRounds::Shot::Empty;
            Vector3 Position;
            Vector3 Forward;
            float Time = 0.0f;

            bool operator==(const RoundCamera&) const = default;
        };

        explicit SniperRounds(ScriptRuntime& runtime);

        // The rounds' models in flight (drawn with the objects).
        const std::vector<std::shared_ptr<Objects::MdkObject>>& Visuals();

        // Fires a round of type from eye along the view
        // (0x461e88); homing rounds chase target. Returns false when all the slots are busy.
        bool Fire(int type, Vector3 eye, float yaw, float pitch, std::shared_ptr<Objects::MdkObject> target);

        // The flight at the start of the last tick; null: fired since.
        RoundCamera Camera(int index, float alpha) const;

        // bomb_follow_path (opcode 28): the mortar round that just hit a triangle group follows a path.
        void GuideLastMortar(int path);

        // A flying round's model as drawn alpha of the way from the last
        // tick's start to now (a new round: where it is).
        Matrix4x4 TransformOf(const Objects::MdkObject& visual, float alpha) const;

        // Updates the rounds by ticks.
        void Update(float ticks);

    private:
        enum class State { Free, Flying, Killed, Hit, Dead, Exploded };

        enum class Type { Bullet, Homing, Grenade, HomingGrenade, Mortar };

        static constexpr std::array<int, 5> Life = {75, 240, 75, 240, 450};
        static constexpr std::array<float, 5> Speed = {1100.0f, 400.0f, 1100.0f, 400.0f, 150.0f};
        static constexpr std::array<std::string_view, 5> Models = {"SW_SHOT", "SW_HOME", "SW_SGREN", "SW_HGREN", "SW_LGREN"};
        static constexpr int Damage = 8;
        // Objects this healthy aren't hurt by bullets.
        static constexpr int Unhurt = 65000;
        // Bullets and homing rounds spin 720° per second (not the mortar).
        static constexpr float Spin = 720.0f;
        // Homing (0x463174): steering starts after 7 ticks; the yaw rate accelerates by 540°/s² up to
        // 270°/s, the pitch turns by at most 120°/s; the speed aims for 250 within 35° of the target,
        // else 100, rising by 200/s and falling by 500/s.
        static constexpr float HomingStart = 233.0f;
        static constexpr float HomingYawAcceleration = 540.0f;
        static constexpr float HomingYawRate = 270.0f;
        static constexpr float HomingPitchRate = 120.0f;
        static constexpr float HomingAim = 35.0f;
        static constexpr float HomingFast = 250.0f;
        static constexpr float HomingSlow = 100.0f;
        static constexpr float HomingSpeedUp = 200.0f;
        static constexpr float HomingSlowDown = 500.0f;
        // The mortar (0x46360c): drag of 60 u/s² shared between the horizontal and vertical speeds,
        // gravity 32 u/s², bounces with v -= 1.75 (v·n) n; it settles after 15 ticks once it stops.
        static constexpr float MortarDrag = 60.0f;
        static constexpr float MortarGravity = 32.0f;
        static constexpr float MortarMaxFall = 220.0f;
        static constexpr float MortarBounce = 1.75f;
        static constexpr float MortarSettleLift = 4.0f;
        static constexpr float MortarSettleTicks = 15.0f;
        static constexpr float MortarStillSpeed = 0.5f;
        static constexpr float MortarStillRise = 1.0f;
        // The half extents of the box the mortar sweeps through the arena (0x491f24).
        static constexpr Vector3 MortarBox = Vector3(0.5f, 0.5f, 0.5f);
        // A guided mortar round stays alive until its path's last key.
        static constexpr float GuidedLife = 99.0f;
        // Explosions (0x4638cc): 150 damage to objects and triangle groups, 75 to Kurt, hit type -7,
        // within 25 (50 for the mortar), the effect at scale 2.
        static constexpr int ExplosionDamage = 150;
        static constexpr int ExplosionKurtDamage = 75;
        static constexpr int HitExplosion = -7;
        static constexpr float BlastRadius = 25.0f;
        static constexpr float MortarBlastRadius = 50.0f;
        static constexpr float ExplosionScale = 2.0f;
        static constexpr int TargetsAll = 6;
        static constexpr int TargetKurt = 1;
        // Ticks a slot stays busy after its round ended, and the camera's watch after a hit.
        static constexpr float MissLinger = 30.0f;
        static constexpr float HitLinger = 45.0f;
        static constexpr float WatchTicks = 30.0f;
        // The round camera backs off by 10 u/s, up to 10 behind the round (0x461d80).
        static constexpr float CameraBackSpeed = 10.0f;
        static constexpr float CameraBack = 10.0f;
        // Below the arena's lowest point by this much a round is lost (ObjectMotion's).
        static constexpr float FallOutDepth = 200.0f;
        // Objects flagged so are neither hit nor targets.
        static constexpr int Untouchable = Objects::MdkObject::FlagNotSolid | Objects::MdkObject::FlagNotTarget;
        static constexpr std::string_view HeadPart = "HEAD";

        // C#'s (Position, Camera, Yaw, Pitch, Roll) tuple.
        struct TickedState
        {
            Vector3 Position;
            Vector3 Camera;
            float Yaw = 0.0f;
            float Pitch = 0.0f;
            float Roll = 0.0f;
        };

        class Round
        {
        public:
            SniperRounds::State State = SniperRounds::State::Free;
            SniperRounds::Type Type = SniperRounds::Type::Bullet;
            Vector3 Position;
            float Yaw = 0.0f;
            // Positive looks down.
            float Pitch = 0.0f;
            float Roll = 0.0f;
            float Speed = 0.0f;
            // The mortar's vertical speed.
            float Vertical = 0.0f;
            float Life = 0.0f;
            float Linger = 0.0f;
            std::shared_ptr<Objects::MdkObject> Target;
            int TargetPart = -1;
            float YawRate = 0.0f;
            // bomb_follow_path (opcode 28): the path and its time.
            int Path = 0;
            float PathTime = 0.0f;
            std::shared_ptr<Objects::MdkObject> Visual;
            float CameraDistance = 0.0f;
            Vector3 CameraPosition;
            float Watch = 0.0f;
            // The flight at the start of the last tick; null: fired since.
            std::optional<TickedState> Ticked;
        };

        struct ObjectHit
        {
            std::shared_ptr<Objects::MdkObject> Object;
            Vector3 Point;
            int Part = 0;
        };

        // C#'s (Vector3? Point, int Part) tuple.
        struct PartHit
        {
            std::optional<Vector3> Point;
            int Part = 0;
        };

        ScriptRuntime* _runtime;
        std::array<std::shared_ptr<Round>, Slots> _rounds;
        std::shared_ptr<Round> _lastMortar;
        // The flying rounds' models (kept: no list per frame).
        std::vector<std::shared_ptr<Objects::MdkObject>> _visuals;

        // A part named HEAD is aimed at, else the whole object.
        static int HeadOf(const Objects::MdkObject& obj);

        std::shared_ptr<Objects::MdkObject> MakeVisual(Type type);

        void Fly(Round& round, float ticks, float dt);

        // The round models stand upright (their length along +Z): the nose is turned from +Z
        // to the flight direction, and the spin is around the length.
        static Matrix4x4 VisualBasis(float roll, float pitch, float yaw);

        static Vector3 Direction(const Round& round);

        // A ray hit's normal, turned towards where the ray came from.
        static Vector3 Facing(Vector3 normal, Vector3 motion) { return Vector3::Dot(normal, motion) > 0.0f ? -normal : normal; }

        // Straight flight (0x462f24), tested against objects and the arena.
        void MoveStraight(Round& round, float dt);

        // Homing (0x463028, 0x463174): the yaw rate accelerates towards the error (from 0
        // when it has the wrong sign), never overshooting; the pitch turns at a limited rate.
        void Steer(Round& round, float dt);

        // The mortar (0x46360c): drag shared between the horizontal and the vertical speed
        // (only while rising), gravity, bounces off the arena.
        void MoveMortar(Round& round, float dt);

        // A mortar round guided by bomb_follow_path (0x4634ac): along the path (absolute
        // positions) without collisions until its last key, then it goes off.
        void FollowPath(Round& round, float ticks);

        // The first object the segment crosses (part -1 for the whole box): objects of Kurt's
        // arena, alive, not flagged 0x30.
        std::optional<ObjectHit> TestObjects(Vector3 start, Vector3 end);

        // The nearest face of a visible part the segment crosses (0x414668): the segment in
        // model space against the pose's triangles, from either side. Faces, not part boxes: LEVEL8's
        // XBSHIP hull box holds its turrets.
        static PartHit HitPart(Objects::MdkObject& obj, Vector3 start, Vector3 end);

        // Where the segment crosses the triangle, as a fraction of it (0x4144c0, either side);
        // infinity when it doesn't.
        static float SegmentFace(Vector3 from, Vector3 to, Vector3 a, Vector3 b, Vector3 c);

        // A round hits an object (0x462708): grenades explode; bullets take 8 hit points (not
        // from objects with 65000 or more) and kill it at 0, else make sparks. The hit event is the part
        // + 1 (if_hit_part).
        void HitObject(Round& round, Objects::MdkObject& obj, int part, Vector3 point);

        // An explosion (0x4638cc) within radius.
        void Explode(Round& round, float radius, Objects::MdkObject* source);

        static void End(Round& round, State state, float linger);

        static float WrapAngle(float degrees);

        static float Wrap360(float degrees);
    };
}
