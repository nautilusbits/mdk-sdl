#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Numerics.h"
#include "formats/Model.h"
#include "formats/ModelAnimation.h"
#include "game/audio/ISoundSource.h"
#include "game/collision/Solids.h"

namespace Mdk::Game::Objects
{
    // A scripted object (alien, door, pickup...) or an arena's script controller. State
    // follows the original object structure (godot-mdk docs/scripts/notes_part1.md): MDK coordinates
    // (Z up), degrees, yaw 0 = +X, 90 = +Y. A port of godot-mdk's mdk_object.gd without the
    // drawing (see ObjectView) and the physics bodies (collisions go through the BSP).
    // Made with std::make_shared: the scripts keep objects they get by reference (shared_from_this).
    class MdkObject : public Audio::ISoundSource, public Collision::Solids::ISurface, public std::enable_shared_from_this<MdkObject>
    {
    public:
        // Each part's vertices (a pose).
        using Parts = std::vector<std::vector<Vector3>>;

        static constexpr float AnimationFps = 30.f;
        // Flags (obj+0x148...obj+0x14b as one integer) used by the engine.
        static constexpr int FlagGravity = 0x2;
        static constexpr int FlagCollides = 0x4;
        static constexpr int FlagLoop = 0x8;
        // Kurt can stand on it (set_targetable), unless 0x10.
        static constexpr int FlagPlatform = 0x100;
        // Kurt goes through the object, and can't stand on it.
        static constexpr int FlagNotSolid = 0x10;
        // The chain gun doesn't aim at the object.
        static constexpr int FlagNotTarget = 0x20;
        static constexpr int FlagRolling = 0x40;
        static constexpr int FlagNoBanking = 0x80;
        static constexpr int FlagPathOnce = 0x400;
        // Kurt walks through the object, but stands on it if a platform.
        static constexpr int FlagNotSolid2 = 0x800;
        // Some parts take damage separately (set_weak_parts).
        static constexpr int FlagWeakParts = 0x2000;
        static constexpr int FlagNoTurning = 0x10000;
        // A pickup that has landed.
        static constexpr int FlagLanded = 0x20000;
        // A pickup Kurt has taken (it vanishes).
        static constexpr int FlagCollected = 0x40000;
        // Goes into the arena whose connection it crosses (the ridden board, thrown items; 0x45e810).
        static constexpr int FlagChangesArena = 0x80000;
        static constexpr int FlagDoor = 0x100000;
        static constexpr int FlagPickup = 0x200000;
        // Swings on a rope (jump_to, opcode 226).
        static constexpr int FlagSwinging = 0x400000;
        // Kurt landed on it stays on it (set_targetable 2; damp_gravity 0x469efc).
        static constexpr int FlagStandable = 0x800000;
        static constexpr int FlagPathPushes = 0x8000000;
        // The path speed follows Kurt's distance ahead (opcode 164).
        static constexpr int FlagPathSpeedByKurt = 0x10000000;
        static constexpr int FlagBounces = 0x20000000;
        // The holy cow of SW_EWJ (0x440074).
        static constexpr int FlagCow = 0x40000000;
        // The animation's root motion doesn't move it (obj+0x14b bit 7; anim_step_frames 0x43ab70),
        // e.g. Gunter on his pillar.
        static constexpr int FlagNoRootMotion = static_cast<int>(0x80000000u);

        // How Kurt meets the object: null, he passes through it.
        std::optional<Collision::Solids::Footing> Footing() const;

        // Contact flags (obj+0x14c), set by the movement code each frame.
        static constexpr int ContactCollided = 0x1;
        static constexpr int ContactFloor = 0x2;
        static constexpr int ContactTouchedKurt = 0x4;
        static constexpr int ContactStuck = 0x8;

        // anim_end_frame meaning the animation has ended (obj+0x118 = 0xFF00).
        static constexpr int AnimationEnded = -256;
        static constexpr int NoHoldFrame = -1;

        // Reference points: own (A) kept on the leader's (B) by attach_to.
        struct PointPair
        {
            int A = 0;
            int B = 0;

            bool operator==(const PointPair&) const = default;
        };

        std::string TypeName;
        // Instance number (obj+0x146).
        int InstanceId = -1;
        // The arena the object belongs to (e.g. HMO_1).
        std::string Arena;
        std::shared_ptr<Formats::Model> Model;
        // Position; yaw obj+0x4c, pitch obj+0x13c, roll obj+0x54 (degrees).
        Vector3 Position{};
        float Yaw = 0;
        float Pitch = 0;
        float Roll = 0;
        // Model scale (obj+0x58).
        float Scale = 1.f;
        Vector3 SpawnPosition{};
        // Position at the end of the previous frame (obj+0x180).
        Vector3 PreviousPosition{};
        int Health = 100;
        // The health set last by set_health (obj+0x2a2), the health bar's maximum.
        int MaxHealth = 0;
        bool Indestructible = false;
        int Flags = 0;
        // Script flag word obj+0x244.
        int ScriptFlags = 0;
        // Contact flags (obj+0x14c).
        int ContactFlags = 0;
        // Script variables (obj+0x234).
        std::vector<float> Variables = std::vector<float>(4);
        // Linked object (obj+0x2b8, variable kind "other").
        std::shared_ptr<MdkObject> Linked;
        // Leader / commanding object (obj+0x138).
        std::shared_ptr<MdkObject> Leader;
        bool Dead = false;
        // Mask of hidden model parts (obj+0x2c8).
        int HiddenParts = 0;
        // Parts blown off (obj+0x2cc, opcode 129): clear_parts_mask doesn't show them again.
        int LockedParts = 0;
        // Frame of the object's animated textures, -1 = by the level's clock.
        int TextureFrame = -1;
        // Drawn (only the objects of the drawn arenas, and not hidden by a cutscene).
        bool Visible = true;
        // What the holy cow falls on (null: Kurt).
        std::shared_ptr<MdkObject> CowTarget;

        // Script state.
        // Restart point (absolute CMI offset, 0 = no script).
        int Restart = 0;
        float WaitTime = 0;
        int WaitResume = 0;
        int DeathScript = 0;
        // Script target last set by a command (obj+0x10c).
        int CommandTarget = 0;
        std::vector<int> GosubReturns;
        std::vector<int> GosubRestarts;
        // Per gosub level tick counters, for if_timer.
        std::vector<float> LevelTimers = std::vector<float>(5);
        // Hit event of this frame (obj+0x21e): > 0 part index + 1 (or a weak part destroyed),
        // -1 chain gun, -2 other hits, -3 blasts.
        int HitEvent = 0;
        // Cause of the last hit (obj+0x21d: -1 chain gun, -2 super chain gun, 1-4 Kurt's
        // projectiles) and its direction (obj+0x224, degrees).
        int HitType = 0;
        float HitDirection = 0;
        // Weak parts (set_weak_parts): parts named prefix + digit take damage separately.
        std::string WeakPrefix;
        int WeakPrefixLength = 0;
        std::vector<int> PartHealth;
        std::vector<int> PartMaxHealth;
        // Command priority and obey level (obj+0x11a, obj+0x11b).
        int Priority = 0;
        int ObeyLevel = 0;
        // Target mode (obj+7, opcode 251): 1 the aliens' target, 2 always targets Kurt.
        int TargetMode = 0;
        // Values of opcodes 210 and 199 (obj+0x2c0, obj+0x104), not identified.
        float Value2c0 = 0;
        float Value104 = 0;

        // Movement state.
        // Movement command (obj+0x11e): 0 idle, 1 formation, 6 chase, 43/78 go to the
        // destination, 61 projectile, 88 forward... and its parameter (obj+0x11f).
        int MoveCommand = 0;
        int MoveParameter = 0;
        Vector3 MoveDestination{};
        // Stuck replans (obj+0x2a0), and the stuck window: ticks and distance (obj+0x2a1, obj+0x2a4).
        int StuckCount = 0;
        float StuckTicks = 0;
        Vector3 StuckMoved{};
        // Yaw at the previous update, for the banking (obj+0x50).
        float BankingYaw = 0;
        // Current waypoint (obj+0x12c, also the formation offset of command 1).
        Vector3 Waypoint{};
        // Reference points: own (A) kept on the leader's (B) by attach_to.
        PointPair AttachPoints;
        // Height offset added to targets (obj+0x5c).
        float HeightOffset = 0;
        float Speed = 0;
        float MaxSpeed = 50.f;
        float Acceleration = 10.f;
        float Deceleration = 15.f;
        // Friction (obj+0x44) and gravity (obj+0x48), units/s².
        float Friction = 64.f;
        float Gravity = 32.f;
        Vector3 Velocity{};
        // Velocity for this frame only (obj+0x294): walking, pushes.
        Vector3 Push{};
        // Per kind parameter (obj+0x302): projectile lifetime, jump time... and its timer (obj+0x306).
        float Parameter = 0;
        float ParameterTimer = 0;
        // Taken off Kurt's health when this projectile touches him, which ends it: the 1996
        // demo's bolts have no script to do it.
        int TouchDamage = 0;

        // Path state (obj+0xe6...obj+0x100).
        int Path = 0;
        float PathTime = 0;
        float PathSpeed = 1.f;
        // path_speed_by_kurt (opcode 164): distance, speeds with Kurt far ahead, around, near.
        std::vector<float> PathSpeeds = std::vector<float>(4);
        int PathStop = -1;
        Vector3 PathOrigin{};
        float PathYawOffset = 0;
        // Sound played when the animation reaches a frame (set_id_and_name: obj+0x140, obj+0x144).
        std::string FrameSound;
        int FrameSoundFrame = 0;
        // Effects (explosions) last this many ticks, showing texture frame EffectTime.
        int EffectFrames = 0;
        float EffectTime = 0;
        // Kurt's thrown item (obj+0x30a) and its ticks left (obj+0x30e).
        int ThrownKind = 0;
        int ItemTicks = 0;
        // Blasts farther than this don't hurt the object (obj+0x2c4, opcode 177).
        float BlastRange = 1000.f;
        // Part + 1 and point of the last sniper round hit (obj+0x21c, obj+0x210).
        int ShotPart = 0;
        Vector3 ShotPoint{};
        // Rolling (flag 0x40, opcode 85): the orientation, turned as it rolls, and the radius
        // (obj+0x326; 0 or less counts as 1).
        Matrix4x4 RollingBasis = Matrix4x4::Identity();
        float RollRadius = 0;
        // Swinging (opcode 226): pivot (obj+0x1c), angular speed (obj+0x302), gain (obj+0x30a),
        // rope length (obj+0x306) and the yaw the swing plane turns to (obj+0x30e).
        Vector3 SwingPivot{};
        float SwingSpeed = 0;
        float SwingGain = 0;
        float SwingLength = 0;
        float SwingYaw = 0;
        // Lines from the object (obj+0x2d0): colour, mask (0xFF: reference points 1-4 to the
        // points; else bits 0, 1: points 0-1 and 2-3).
        int RopeColor = 0;
        int RopeMask = 0;
        std::vector<Vector3> RopePoints = std::vector<Vector3>(4);
        // Object carried along (a pickup's chute).
        std::shared_ptr<MdkObject> Attached;

        // Doors (obj+0x306...obj+0x32a, see ObjectBehaviors).
        // CMI offsets of the opening and closing animations.
        std::vector<int> DoorAnimations = std::vector<int>(2);
        int DoorState = 0;
        // Kurt opens the door when closer than this.
        float DoorDistance = 20.f;
        // Sounds: starts opening, starts closing, open, closed.
        std::vector<std::string> DoorSounds = std::vector<std::string>(4);
        // Masks of the parts named LOCK and HC...
        int LockParts = 0;
        int HatchParts = 0;
        // Arena on the other side of a connector (spawn_connector).
        std::string Connects;
        // Sound tracked by if_own_sound (play_sound flag 4), and the looping sound (voices, 0 = none).
        std::string TrackedSound;
        int TrackedVoice = 0;
        int LoopSound = 0;
        // The looping sound's name, to start it again when the arena comes back (0x43f8e0).
        std::string LoopSoundName;
        // Strings of opcodes 25 and 26 (obj+0x154: explosion sound, obj+0x150: ricochet).
        std::vector<std::string> Labels = std::vector<std::string>(2);

        // Animation state.
        std::shared_ptr<Formats::ModelAnimation> Animation;
        float AnimationTime = 0;
        int AnimationFrame = 0;
        // Frames per second of the animation (obj+0xe0, anim_fps).
        float AnimationRate = AnimationFps;
        // Hold frame (obj+0x118): the animation stops there; AnimationEnded once a
        // non-looping animation has ended, NoHoldFrame = none.
        int AnimationEndFrame = AnimationEnded;

        // The orientation: yaw turns around Z, pitch raises the nose (+X towards +Z), roll
        // turns around the forward axis.
        Matrix4x4 Rotation() const { return RotationOf(Roll, Pitch, Yaw); }

        // Model space to the world (a rolling object turns by its rolling basis).
        Matrix4x4 Transform() const;

        // A tick starts: frames until the next are drawn from this pose to the tick's.
        void Remember();

        // The place, yaw and transform alpha of the way from the last tick's start to now (a
        // frame between ticks; a rolling object keeps its basis).
        Vector3 PositionAt(float alpha) const;
        float YawAt(float alpha) const;
        Matrix4x4 TransformAt(float alpha) const;

        // Two animation frames and the share of the second.
        struct PoseFrames
        {
            const Parts* From = nullptr;
            const Parts* To = nullptr;
            float Share = 0;
        };

        // The animation frames drawn alpha of the way from the last tick's start to now, and the
        // share of the second (a loop goes on to its first frame); null: the current pose (no tick
        // yet, another animation, a restart, a frame set by a script).
        std::optional<PoseFrames> FramesAt(float alpha) const;

        // A rolling object rests on its origin but its model is lifted by the height offset
        // (0x43b65c: z + obj+0x5c), e.g. LEVEL6's centred boulder XBO by 5 of its 5.15 radius.
        float Lift() const { return (Flags & FlagRolling) != 0 ? HeightOffset : 0.f; }

        enum class Rolling { Off, On };

        // Starts or stops rolling (set_rolling, opcode 85).
        void SetRolling(Rolling rolling);

        enum class Looping { Once, Loop };

        // Starts an animation (anim_loop / anim_once). Restarting the current one does nothing
        // unless it has ended, as in the original.
        void PlayAnimation(const std::shared_ptr<Formats::ModelAnimation>& animation, Looping looping);

        // Starts an animation from its first frame, even if it's already playing.
        void RestartAnimation(const std::shared_ptr<Formats::ModelAnimation>& animation, Looping looping);

        bool IsAnimationDone() const { return Animation == nullptr || AnimationEndFrame == AnimationEnded; }

        // Advances the animation (object_anim_update), moving the object by its root motion.
        void AdvanceAnimation(float delta);

        // The vertices of each part in the current pose (model space), hidden parts included;
        // shared, made once per model and animation.
        const Parts& PoseParts() const;

        // A sound on the object, offset turned with its yaw.
        Vector3 SoundPosition(Vector3 offset) override;

        // Forgets the poses and boxes kept for models (a new level: the last one's models go).
        static void ForgetPoses();

        // The vertices of each part in the current pose (model space), hidden parts empty.
        Parts Pose() const;

        // Bounds of each part in the current pose (model space), null for empty parts.
        const std::vector<std::optional<Formats::Box>>& PartBounds() const;

        // Makes an animation's poses and boxes for a model now (a level's load), not when it plays.
        static void Prepare(const std::shared_ptr<Formats::Model>& model, const std::shared_ptr<Formats::ModelAnimation>& animation);

        // Bounds of the visible parts in the current pose (model space).
        Formats::Box PoseBounds() const;

        // Index of the model part named name, or -1.
        int FindPart(const std::string& name) const;

        // World position of a model reference point (the attachment points obj+0x1b0), where the
        // current animation frame has it (animations keep each point's place per frame): LEVEL7's soldiers
        // hanging in a chain (XG) raise the hand they hang by, so rest-pose points bunched the chain up.
        Vector3 ReferencePoint(int index) const;

        // World position of the centre of a model part, in the current pose as drawn: an animated
        // model's parts stand where the animation puts them (the rest pose can leave them at the origin,
        // e.g. LEVEL7's boss XB1, whose GUN rested at his feet so its BIGBOLTs hit the floor at once).
        Vector3 PartCenter(int part) const;

        float DistanceTo(Vector3 point) const { return Vector3::Distance(Position, point); }

        // Direction (degrees) from this object to a point.
        float YawTo(Vector3 point) const;

    private:
        // The pose at the start of the last tick (Remember).
        struct Ticked
        {
            Vector3 Position{};
            float Yaw = 0;
            float Pitch = 0;
            float Roll = 0;
            std::string Arena;
            std::shared_ptr<Formats::ModelAnimation> Animation;
            float AnimationTime = 0;
        };

        using ModelKey = std::pair<std::shared_ptr<Formats::Model>, std::shared_ptr<Formats::ModelAnimation>>;

        // Shared by every object: an animation's poses per model, and part bounds per frame.
        // Keyed by owning pointers, like C#'s strong reference keys (no stale entry at a reused address); the locks stand for C#'s lock (Baked) / lock (BoundsCache).
        static std::map<ModelKey, std::vector<Parts>> Baked;
        static std::map<ModelKey, std::vector<std::vector<std::optional<Formats::Box>>>> BoundsCache;
        static std::map<std::shared_ptr<Formats::Model>, Parts> RestPoses;
        static std::mutex BakedLock;
        static std::mutex BoundsLock;

        // The pose at the start of the last tick; null: none yet.
        std::optional<Ticked> _ticked;

        static Matrix4x4 RotationOf(float roll, float pitch, float yaw);

        // Whether frames draw it between ticks: not when new, after a jump or in another arena.
        bool Blends(float alpha) const;

        static const Parts& Rest(const std::shared_ptr<Formats::Model>& model);

        static const std::vector<Parts>& Bake(const std::shared_ptr<Formats::Model>& model, const std::shared_ptr<Formats::ModelAnimation>& animation);

        // Each part's box in a pose (null for an empty part).
        static std::vector<std::optional<Formats::Box>> PoseBoxes(const Parts& pose);

        // Solids::ISurface, explicit in C#.
        bool HasMesh() const override { return Model != nullptr; }

        // The first hit of a segment on a part's posed triangles (world z; 0x414668 in model space).
        std::optional<float> Floor(int part, Vector3 from, Vector3 to) override;

        // Where (0..1) the segment a → a + ab crosses the triangle, either side (Möller–Trumbore).
        static std::optional<float> Crossing(Vector3 a, Vector3 ab, Vector3 v0, Vector3 v1, Vector3 v2);
    };
}
