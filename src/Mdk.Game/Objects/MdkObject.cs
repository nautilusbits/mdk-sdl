using System.Numerics;
using Mdk.Formats;
using Mdk.Game.Audio;
using Mdk.Game.Scripts;

namespace Mdk.Game.Objects;

/// <summary>A scripted object (alien, door, pickup...) or an arena's script controller. State
/// follows the original object structure (godot-mdk docs/scripts/notes_part1.md): MDK coordinates
/// (Z up), degrees, yaw 0 = +X, 90 = +Y. A port of godot-mdk's <c>mdk_object.gd</c> without the
/// drawing (see <see cref="ObjectView"/>) and the physics bodies (collisions go through the BSP).</summary>
public sealed class MdkObject : ISoundSource, Collision.Solids.ISurface
{
    public const float AnimationFps = 30f;

    // Flags (obj+0x148...obj+0x14b as one integer) used by the engine.
    public const int FlagGravity = 0x2;
    public const int FlagCollides = 0x4;
    public const int FlagLoop = 0x8;
    /// <summary>Kurt can stand on it (set_targetable), unless 0x10.</summary>
    public const int FlagPlatform = 0x100;
    /// <summary>Kurt goes through the object, and can't stand on it.</summary>
    public const int FlagNotSolid = 0x10;
    /// <summary>The chain gun doesn't aim at the object.</summary>
    public const int FlagNotTarget = 0x20;
    public const int FlagRolling = 0x40;
    public const int FlagNoBanking = 0x80;
    public const int FlagPathOnce = 0x400;
    /// <summary>Kurt walks through the object, but stands on it if a platform.</summary>
    public const int FlagNotSolid2 = 0x800;
    /// <summary>Some parts take damage separately (set_weak_parts).</summary>
    public const int FlagWeakParts = 0x2000;
    public const int FlagNoTurning = 0x10000;
    /// <summary>A pickup that has landed.</summary>
    public const int FlagLanded = 0x20000;
    /// <summary>A pickup Kurt has taken (it vanishes).</summary>
    public const int FlagCollected = 0x40000;
    /// <summary>Goes into the arena whose connection it crosses (the ridden board, thrown items; 0x45e810).</summary>
    public const int FlagChangesArena = 0x80000;
    public const int FlagDoor = 0x100000;
    public const int FlagPickup = 0x200000;
    /// <summary>Swings on a rope (jump_to, opcode 226).</summary>
    public const int FlagSwinging = 0x400000;
    /// <summary>Kurt landed on it stays on it (set_targetable 2; damp_gravity 0x469efc).</summary>
    public const int FlagStandable = 0x800000;
    public const int FlagPathPushes = 0x8000000;
    /// <summary>The path speed follows Kurt's distance ahead (opcode 164).</summary>
    public const int FlagPathSpeedByKurt = 0x10000000;
    public const int FlagBounces = 0x20000000;
    /// <summary>The holy cow of SW_EWJ (0x440074).</summary>
    public const int FlagCow = 0x40000000;
    /// <summary>The animation's root motion doesn't move it (obj+0x14b bit 7; anim_step_frames 0x43ab70),
    /// e.g. Gunter on his pillar.</summary>
    public const int FlagNoRootMotion = unchecked((int)0x80000000);

    /// <summary>How Kurt meets the object: null, he passes through it.</summary>
    public Collision.Solids.Footing? Footing
    {
        get
        {
            // A wall: no 0x10, 0x800 (damp_collide_move 0x465e34); a floor: 0x100, no 0x10
            // (damp_platform_floor 0x41d2c4). E.g. the ridden snowboard (0x800900): a floor only.
            var wall = (Flags & (FlagNotSolid | FlagNotSolid2)) == 0;
            var floor = (Flags & (FlagNotSolid | FlagPlatform)) == FlagPlatform;
            if (wall)
            {
                return floor ? Collision.Solids.Footing.Platform : Collision.Solids.Footing.Wall;
            }

            return floor ? Collision.Solids.Footing.Floor : null;
        }
    }

    // Contact flags (obj+0x14c), set by the movement code each frame.
    public const int ContactCollided = 0x1;
    public const int ContactFloor = 0x2;
    public const int ContactTouchedKurt = 0x4;
    public const int ContactStuck = 0x8;

    /// <summary>anim_end_frame meaning the animation has ended (obj+0x118 = 0xFF00).</summary>
    public const int AnimationEnded = -256;
    public const int NoHoldFrame = -1;

    public string TypeName = "";
    /// <summary>Instance number (obj+0x146).</summary>
    public int InstanceId = -1;
    /// <summary>The arena the object belongs to (e.g. HMO_1).</summary>
    public string Arena = "";
    public Model? Model;

    /// <summary>Position; yaw obj+0x4c, pitch obj+0x13c, roll obj+0x54 (degrees).</summary>
    public Vector3 Position;
    public float Yaw;
    public float Pitch;
    public float Roll;
    /// <summary>Model scale (obj+0x58).</summary>
    public float Scale = 1f;
    public Vector3 SpawnPosition;
    /// <summary>Position at the end of the previous frame (obj+0x180).</summary>
    public Vector3 PreviousPosition;

    public int Health = 100;
    /// <summary>The health set last by set_health (obj+0x2a2), the health bar's maximum.</summary>
    public int MaxHealth;
    public bool Indestructible;
    public int Flags;
    /// <summary>Script flag word obj+0x244.</summary>
    public int ScriptFlags;
    /// <summary>Contact flags (obj+0x14c).</summary>
    public int ContactFlags;
    /// <summary>Script variables (obj+0x234).</summary>
    public float[] Variables = new float[4];
    /// <summary>Linked object (obj+0x2b8, variable kind "other").</summary>
    public MdkObject? Linked;
    /// <summary>Leader / commanding object (obj+0x138).</summary>
    public MdkObject? Leader;
    public bool Dead;
    /// <summary>Mask of hidden model parts (obj+0x2c8).</summary>
    public int HiddenParts;
    /// <summary>Parts blown off (obj+0x2cc, opcode 129): clear_parts_mask doesn't show them again.</summary>
    public int LockedParts;
    /// <summary>Frame of the object's animated textures, -1 = by the level's clock.</summary>
    public int TextureFrame = -1;
    /// <summary>Drawn (only the objects of the drawn arenas, and not hidden by a cutscene).</summary>
    public bool Visible = true;
    /// <summary>What the holy cow falls on (null: Kurt).</summary>
    public MdkObject? CowTarget;

    // Script state.
    /// <summary>Restart point (absolute CMI offset, 0 = no script).</summary>
    public int Restart;
    public float WaitTime;
    public int WaitResume;
    public int DeathScript;
    /// <summary>Script target last set by a command (obj+0x10c).</summary>
    public int CommandTarget;
    public List<int> GosubReturns = [];
    public List<int> GosubRestarts = [];
    /// <summary>Per gosub level tick counters, for if_timer.</summary>
    public float[] LevelTimers = new float[5];
    /// <summary>Hit event of this frame (obj+0x21e): > 0 part index + 1 (or a weak part destroyed),
    /// -1 chain gun, -2 other hits, -3 blasts.</summary>
    public int HitEvent;
    /// <summary>Cause of the last hit (obj+0x21d: -1 chain gun, -2 super chain gun, 1-4 Kurt's
    /// projectiles) and its direction (obj+0x224, degrees).</summary>
    public int HitType;
    public float HitDirection;
    /// <summary>Weak parts (set_weak_parts): parts named prefix + digit take damage separately.</summary>
    public string WeakPrefix = "";
    public int WeakPrefixLength;
    public int[] PartHealth = [];
    public int[] PartMaxHealth = [];
    /// <summary>Command priority and obey level (obj+0x11a, obj+0x11b).</summary>
    public int Priority;
    public int ObeyLevel;
    /// <summary>Target mode (obj+7, opcode 251): 1 the aliens' target, 2 always targets Kurt.</summary>
    public int TargetMode;
    /// <summary>Values of opcodes 210 and 199 (obj+0x2c0, obj+0x104), not identified.</summary>
    public float Value2c0;
    public float Value104;

    // Movement state.
    /// <summary>Movement command (obj+0x11e): 0 idle, 1 formation, 6 chase, 43/78 go to the
    /// destination, 61 projectile, 88 forward... and its parameter (obj+0x11f).</summary>
    public int MoveCommand;
    public int MoveParameter;
    public Vector3 MoveDestination;
    /// <summary>Stuck replans (obj+0x2a0), and the stuck window: ticks and distance (obj+0x2a1, obj+0x2a4).</summary>
    public int StuckCount;
    public float StuckTicks;
    public Vector3 StuckMoved;
    /// <summary>Yaw at the previous update, for the banking (obj+0x50).</summary>
    public float BankingYaw;
    /// <summary>Current waypoint (obj+0x12c, also the formation offset of command 1).</summary>
    public Vector3 Waypoint;
    /// <summary>Reference points: own (A) kept on the leader's (B) by attach_to.</summary>
    public (int A, int B) AttachPoints;
    /// <summary>Height offset added to targets (obj+0x5c).</summary>
    public float HeightOffset;
    public float Speed;
    public float MaxSpeed = 50f;
    public float Acceleration = 10f;
    public float Deceleration = 15f;
    /// <summary>Friction (obj+0x44) and gravity (obj+0x48), units/s².</summary>
    public float Friction = 64f;
    public float Gravity = 32f;
    public Vector3 Velocity;
    /// <summary>Velocity for this frame only (obj+0x294): walking, pushes.</summary>
    public Vector3 Push;
    /// <summary>Per kind parameter (obj+0x302): projectile lifetime, jump time... and its timer (obj+0x306).</summary>
    public float Parameter;
    public float ParameterTimer;
    /// <summary>Taken off Kurt's health when this projectile touches him, which ends it: the 1996
    /// demo's bolts have no script to do it.</summary>
    public int TouchDamage;

    // Path state (obj+0xe6...obj+0x100).
    public int Path;
    public float PathTime;
    public float PathSpeed = 1f;
    /// <summary>path_speed_by_kurt (opcode 164): distance, speeds with Kurt far ahead, around, near.</summary>
    public float[] PathSpeeds = new float[4];
    public int PathStop = -1;
    public Vector3 PathOrigin;
    public float PathYawOffset;

    /// <summary>Sound played when the animation reaches a frame (set_id_and_name: obj+0x140, obj+0x144).</summary>
    public string FrameSound = "";
    public int FrameSoundFrame;
    /// <summary>Effects (explosions) last this many ticks, showing texture frame EffectTime.</summary>
    public int EffectFrames;
    public float EffectTime;
    /// <summary>Kurt's thrown item (obj+0x30a) and its ticks left (obj+0x30e).</summary>
    public int ThrownKind;
    public int ItemTicks;
    /// <summary>Blasts farther than this don't hurt the object (obj+0x2c4, opcode 177).</summary>
    public float BlastRange = 1000f;
    /// <summary>Part + 1 and point of the last sniper round hit (obj+0x21c, obj+0x210).</summary>
    public int ShotPart;
    public Vector3 ShotPoint;
    /// <summary>Rolling (flag 0x40, opcode 85): the orientation, turned as it rolls, and the radius
    /// (obj+0x326; 0 or less counts as 1).</summary>
    public Matrix4x4 RollingBasis = Matrix4x4.Identity;
    public float RollRadius;
    /// <summary>Swinging (opcode 226): pivot (obj+0x1c), angular speed (obj+0x302), gain (obj+0x30a),
    /// rope length (obj+0x306) and the yaw the swing plane turns to (obj+0x30e).</summary>
    public Vector3 SwingPivot;
    public float SwingSpeed;
    public float SwingGain;
    public float SwingLength;
    public float SwingYaw;
    /// <summary>Lines from the object (obj+0x2d0): colour, mask (0xFF: reference points 1-4 to the
    /// points; else bits 0, 1: points 0-1 and 2-3).</summary>
    public int RopeColor;
    public int RopeMask;
    public Vector3[] RopePoints = new Vector3[4];
    /// <summary>Object carried along (a pickup's chute).</summary>
    public MdkObject? Attached;

    // Doors (obj+0x306...obj+0x32a, see ObjectBehaviors).
    /// <summary>CMI offsets of the opening and closing animations.</summary>
    public int[] DoorAnimations = new int[2];
    public int DoorState;
    /// <summary>Kurt opens the door when closer than this.</summary>
    public float DoorDistance = 20f;
    /// <summary>Sounds: starts opening, starts closing, open, closed.</summary>
    public string[] DoorSounds = ["", "", "", ""];
    /// <summary>Masks of the parts named LOCK and HC...</summary>
    public int LockParts;
    public int HatchParts;
    /// <summary>Arena on the other side of a connector (spawn_connector).</summary>
    public string Connects = "";

    /// <summary>Sound tracked by if_own_sound (play_sound flag 4), and the looping sound (voices, 0 = none).</summary>
    public string TrackedSound = "";
    public int TrackedVoice;
    public int LoopSound;
    /// <summary>The looping sound's name, to start it again when the arena comes back (0x43f8e0).</summary>
    public string LoopSoundName = "";
    /// <summary>Strings of opcodes 25 and 26 (obj+0x154: explosion sound, obj+0x150: ricochet).</summary>
    public string[] Labels = ["", ""];

    // Animation state.
    public ModelAnimation? Animation;
    public float AnimationTime;
    public int AnimationFrame;
    /// <summary>Frames per second of the animation (obj+0xe0, anim_fps).</summary>
    public float AnimationRate = AnimationFps;
    /// <summary>Hold frame (obj+0x118): the animation stops there; <see cref="AnimationEnded"/> once a
    /// non-looping animation has ended, <see cref="NoHoldFrame"/> = none.</summary>
    public int AnimationEndFrame = AnimationEnded;

    /// <summary>Shared by every object: an animation's poses per model, and part bounds per frame.</summary>
    private static readonly Dictionary<(Model, ModelAnimation), Vector3[][][]> Baked = [];
    private static readonly Dictionary<(Model, ModelAnimation?), Box?[][]> BoundsCache = [];

    /// <summary>The orientation: yaw turns around Z, pitch raises the nose (+X towards +Z), roll
    /// turns around the forward axis.</summary>
    public Matrix4x4 Rotation => RotationOf(Roll, Pitch, Yaw);

    private static Matrix4x4 RotationOf(float roll, float pitch, float yaw) =>
        Matrix4x4.CreateRotationX(float.DegreesToRadians(roll))
        * Matrix4x4.CreateRotationY(-float.DegreesToRadians(pitch))
        * Matrix4x4.CreateRotationZ(float.DegreesToRadians(yaw));

    /// <summary>Model space to the world (a rolling object turns by its rolling basis).</summary>
    public Matrix4x4 Transform =>
        Matrix4x4.CreateScale(Scale) * ((Flags & FlagRolling) != 0 ? RollingBasis : Rotation) * Matrix4x4.CreateTranslation(Position + Vector3.UnitZ * Lift);

    /// <summary>The pose at the start of the last tick (<see cref="Remember"/>); null: none yet.</summary>
    private (Vector3 Position, float Yaw, float Pitch, float Roll, string Arena, ModelAnimation? Animation, float AnimationTime)? _ticked;

    /// <summary>A tick starts: frames until the next are drawn from this pose to the tick's.</summary>
    public void Remember() => _ticked = (Position, Yaw, Pitch, Roll, Arena, Animation, AnimationTime);

    /// <summary>Whether frames draw it between ticks: not when new, after a jump or in another arena.</summary>
    private bool Blends(float alpha) =>
        alpha < 1f && _ticked is { } from && from.Arena == Arena && !FrameBlend.Jumped(from.Position, Position, ScriptRuntime.Tick);

    /// <summary>The place, yaw and transform <paramref name="alpha"/> of the way from the last
    /// tick's start to now (a frame between ticks; a rolling object keeps its basis).</summary>
    public Vector3 PositionAt(float alpha) => Blends(alpha) ? FrameBlend.Lerp(_ticked!.Value.Position, Position, alpha) : Position;

    public float YawAt(float alpha) => Blends(alpha) ? FrameBlend.LerpAngle(_ticked!.Value.Yaw, Yaw, alpha) : Yaw;

    public Matrix4x4 TransformAt(float alpha)
    {
        if (!Blends(alpha))
        {
            return Transform;
        }

        var from = _ticked!.Value;
        var rotation = (Flags & FlagRolling) != 0
            ? RollingBasis
            : RotationOf(FrameBlend.LerpAngle(from.Roll, Roll, alpha), FrameBlend.LerpAngle(from.Pitch, Pitch, alpha), FrameBlend.LerpAngle(from.Yaw, Yaw, alpha));
        return Matrix4x4.CreateScale(Scale) * rotation * Matrix4x4.CreateTranslation(PositionAt(alpha) + Vector3.UnitZ * Lift);
    }

    /// <summary>The animation frames drawn <paramref name="alpha"/> of the way from the last tick's
    /// start to now, and the share of the second (a loop goes on to its first frame); null: the
    /// current pose (no tick yet, another animation, a restart, a frame set by a script).</summary>
    public (Vector3[][] From, Vector3[][] To, float Share)? FramesAt(float alpha)
    {
        if (!Blends(alpha) || Model == null || Animation is not { FrameCount: > 0 } animation || _ticked!.Value.Animation != animation)
        {
            return null;
        }

        var count = animation.FrameCount;
        var from = _ticked.Value.AnimationTime;
        var to = AnimationTime;
        if (to < from && (Flags & FlagLoop) != 0)
        {
            to += count;
        }

        if (to < from || (int)MathF.Round(AnimationTime) % count != AnimationFrame)
        {
            return null;
        }

        var time = FrameBlend.Lerp(from, to, alpha);
        var frame = (int)time;
        var frames = Bake(Model, animation);
        return (frames[frame % count], frames[(frame + 1) % count], time - frame);
    }

    /// <summary>A rolling object rests on its origin but its model is lifted by the height offset
    /// (0x43b65c: z + obj+0x5c), e.g. LEVEL6's centred boulder XBO by 5 of its 5.15 radius.</summary>
    public float Lift => (Flags & FlagRolling) != 0 ? HeightOffset : 0f;

    public enum Rolling { Off, On }

    /// <summary>Starts or stops rolling (set_rolling, opcode 85).</summary>
    public void SetRolling(Rolling rolling)
    {
        if (rolling == Rolling.Off)
        {
            Flags &= ~FlagRolling;
            return;
        }

        if ((Flags & FlagRolling) == 0)
        {
            Flags |= FlagRolling;
            RollingBasis = Rotation;
        }
    }

    /// <summary>Starts an animation (anim_loop / anim_once). Restarting the current one does nothing
    /// unless it has ended, as in the original.</summary>
    public void PlayAnimation(ModelAnimation? animation, Looping looping)
    {
        if (looping == Looping.Loop)
        {
            Flags |= FlagLoop;
        }
        else
        {
            Flags &= ~FlagLoop;
        }

        if (animation == Animation && AnimationEndFrame != AnimationEnded)
        {
            if (AnimationEndFrame >= 0)
            {
                AnimationEndFrame = NoHoldFrame;
            }

            return;
        }

        Animation = animation;
        AnimationTime = 0f;
        AnimationFrame = 0;
        AnimationEndFrame = animation != null ? NoHoldFrame : AnimationEnded;
    }

    public enum Looping { Once, Loop }

    /// <summary>Starts an animation from its first frame, even if it's already playing.</summary>
    public void RestartAnimation(ModelAnimation? animation, Looping looping)
    {
        Animation = null;
        PlayAnimation(animation, looping);
    }

    public bool IsAnimationDone => Animation == null || AnimationEndFrame == AnimationEnded;

    /// <summary>Advances the animation (object_anim_update), moving the object by its root motion.</summary>
    public void AdvanceAnimation(float delta)
    {
        if (Animation == null || AnimationEndFrame == AnimationEnded)
        {
            return;
        }

        if (AnimationEndFrame >= 0 && AnimationFrame == AnimationEndFrame)
        {
            return;
        }

        var count = Animation.FrameCount;
        AnimationTime += delta * AnimationRate * Animation.Speed;
        if (AnimationEndFrame >= 0 && AnimationFrame < AnimationEndFrame && (int)MathF.Round(AnimationTime) >= AnimationEndFrame)
        {
            AnimationTime = AnimationEndFrame;
        }

        if (AnimationTime >= count - 1)
        {
            if ((Flags & FlagLoop) != 0)
            {
                if (AnimationTime >= count)
                {
                    AnimationTime -= count;
                }
            }
            else
            {
                AnimationTime = count - 1;
            }
        }

        var frame = (int)MathF.Round(AnimationTime) % count;
        if (frame != AnimationFrame)
        {
            // Root motion: each passed frame's motion (model space) joins the push, so the next
            // tick's move collides with the arena (anim_step_frames 0x43ab70): an alien's pose
            // doesn't sink it through the floor.
            var step = frame > AnimationFrame ? frame - AnimationFrame : frame + count - AnimationFrame;
            if ((Flags & FlagNoRootMotion) != 0)
            {
                step = 0;
            }

            var turn = Matrix4x4.CreateRotationZ(float.DegreesToRadians(Yaw));
            for (var i = 0; i < step; i++)
            {
                var f = (AnimationFrame + 1 + i) % count;
                Push += Vector3.Transform(Animation.RootMotion[f], turn) / delta;
            }

            AnimationFrame = frame;
        }

        if (AnimationFrame == count - 1 && (Flags & FlagLoop) == 0 && AnimationFrame != AnimationEndFrame)
        {
            AnimationEndFrame = AnimationEnded;
        }
    }

    /// <summary>The vertices of each part in the current pose (model space), hidden parts included;
    /// shared, made once per model and animation.</summary>
    public Vector3[][] PoseParts()
    {
        if (Model == null)
        {
            return [];
        }

        return Animation == null ? Rest(Model) : Bake(Model, Animation)[AnimationFrame];
    }

    private static Vector3[][] Rest(Model model)
    {
        lock (Baked)
        {
            if (!RestPoses.TryGetValue(model, out var pose))
            {
                pose = RestPoses[model] = [.. model.PartList.Select(p => p.Vertices)];
            }

            return pose;
        }
    }

    private static readonly Dictionary<Model, Vector3[][]> RestPoses = [];

    /// <summary>A sound on the object, <paramref name="offset"/> turned with its yaw.</summary>
    public Vector3 SoundPosition(Vector3 offset) => Position + ScriptMath.RotatedZ(offset, Yaw);

    /// <summary>Forgets the poses and boxes kept for models (a new level: the last one's models go).</summary>
    public static void ForgetPoses()
    {
        lock (Baked)
        {
            Baked.Clear();
            RestPoses.Clear();
        }

        lock (BoundsCache)
        {
            BoundsCache.Clear();
        }
    }

    /// <summary>The vertices of each part in the current pose (model space), hidden parts empty.</summary>
    public Vector3[][] Pose()
    {
        if (Model == null)
        {
            return [];
        }

        var pose = PoseParts();
        if (HiddenParts == 0)
        {
            return pose;
        }

        return pose.Select((vertices, i) => (HiddenParts & (1 << i)) != 0 ? [] : vertices).ToArray();
    }

    private static Vector3[][][] Bake(Model model, ModelAnimation animation)
    {
        lock (Baked)
        {
            if (!Baked.TryGetValue((model, animation), out var frames))
            {
                frames = Baked[(model, animation)] = animation.Bake(model);
            }

            return frames;
        }
    }

    /// <summary>Bounds of each part in the current pose (model space), null for empty parts.</summary>
    public Box?[] PartBounds()
    {
        if (Model == null)
        {
            return [];
        }

        // Shared like Baked: runtimes on other threads (parallel tests) use it too. Every frame of
        // an animation at once: no allocation while it plays.
        var key = (Model, Animation);
        lock (BoundsCache)
        {
            if (!BoundsCache.TryGetValue(key, out var frames))
            {
                var poses = Animation == null ? [Rest(Model)] : Bake(Model, Animation);
                frames = BoundsCache[key] = [.. poses.Select(PoseBoxes)];
            }

            return frames[Animation == null ? 0 : AnimationFrame];
        }
    }

    /// <summary>Each part's box in a pose (null for an empty part).</summary>
    private static Box?[] PoseBoxes(Vector3[][] pose) =>
        [.. pose.Select(v => v.Length == 0 ? (Box?)null : new Box(v.Aggregate(Vector3.Min), v.Aggregate(Vector3.Max)))];

    /// <summary>Makes an animation's poses and boxes for a model now (a level's load), not when it plays.</summary>
    public static void Prepare(Model model, ModelAnimation? animation) =>
        new MdkObject { Model = model, Animation = animation }.PartBounds();

    /// <summary>Bounds of the visible parts in the current pose (model space).</summary>
    public Box PoseBounds()
    {
        Box? result = null;
        var bounds = PartBounds();
        for (var i = 0; i < bounds.Length; i++)
        {
            if ((HiddenParts & (1 << i)) != 0 || bounds[i] is not { } b)
            {
                continue;
            }

            result = result is { } r ? new Box(Vector3.Min(r.Min, b.Min), Vector3.Max(r.Max, b.Max)) : b;
        }

        return result ?? Model?.Bounds ?? default;
    }

    bool Collision.Solids.ISurface.HasMesh => Model != null;

    /// <summary>The first hit of a segment on a part's posed triangles (world z; 0x414668 in model space).</summary>
    float? Collision.Solids.ISurface.Floor(int part, Vector3 from, Vector3 to)
    {
        var pose = PoseParts();
        if (part >= pose.Length || !Matrix4x4.Invert(Transform, out var inverse))
        {
            return null;
        }

        var a = Vector3.Transform(from, inverse);
        var ab = Vector3.Transform(to, inverse) - a;
        var vertices = pose[part];
        var indices = Model!.PartList[part].TriangleIndices;
        float? nearest = null;
        for (var i = 0; i + 2 < indices.Length; i += 3)
        {
            if (Crossing(a, ab, vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]]) is { } t
                && t < (nearest ?? float.MaxValue))
            {
                nearest = t;
            }
        }

        return nearest is { } h ? Vector3.Transform(a + ab * h, Transform).Z : null;
    }

    /// <summary>Where (0..1) the segment a → a + ab crosses the triangle, either side (Möller–Trumbore).</summary>
    private static float? Crossing(Vector3 a, Vector3 ab, Vector3 v0, Vector3 v1, Vector3 v2)
    {
        var e1 = v1 - v0;
        var e2 = v2 - v0;
        var p = Vector3.Cross(ab, e2);
        var det = Vector3.Dot(e1, p);
        if (MathF.Abs(det) < 1e-9f)
        {
            return null;
        }

        var s = a - v0;
        var u = Vector3.Dot(s, p) / det;
        var q = Vector3.Cross(s, e1);
        var v = Vector3.Dot(ab, q) / det;
        var t = Vector3.Dot(e2, q) / det;
        return u < 0f || v < 0f || u + v > 1f || t < 0f || t > 1f ? null : t;
    }

    /// <summary>Index of the model part named <paramref name="name"/>, or -1.</summary>
    public int FindPart(string name) =>
        Model?.PartList.FindIndex(p => p.Name.Equals(name, StringComparison.OrdinalIgnoreCase)) ?? -1;

    /// <summary>World position of a model reference point (the attachment points obj+0x1b0), where the
    /// current animation frame has it (animations keep each point's place per frame): LEVEL7's soldiers
    /// hanging in a chain (XG) raise the hand they hang by, so rest-pose points bunched the chain up.</summary>
    public Vector3 ReferencePoint(int index)
    {
        if (Model == null || index >= Model.ReferencePoints.Count)
        {
            return Position;
        }

        var point = Model.ReferencePoints[index];
        if (Animation is { FrameCount: > 0 } animation && index < animation.ReferencePoints.Count)
        {
            var frames = animation.ReferencePoints[index];
            point = frames[Math.Clamp(AnimationFrame, 0, frames.Length - 1)];
        }

        return Position + Vector3.Transform(point * Scale, Matrix4x4.CreateRotationZ(float.DegreesToRadians(Yaw)));
    }

    /// <summary>World position of the centre of a model part, in the current pose as drawn: an animated
    /// model's parts stand where the animation puts them (the rest pose can leave them at the origin,
    /// e.g. LEVEL7's boss XB1, whose GUN rested at his feet so its BIGBOLTs hit the floor at once).</summary>
    public Vector3 PartCenter(int part)
    {
        if (part < 0 || Model == null)
        {
            return Position;
        }

        var pose = PoseParts();
        if (part < pose.Length && pose[part].Length > 0)
        {
            var min = new Vector3(float.MaxValue);
            var max = new Vector3(float.MinValue);
            foreach (var vertex in pose[part])
            {
                min = Vector3.Min(min, vertex);
                max = Vector3.Max(max, vertex);
            }

            return Vector3.Transform((min + max) / 2f, Transform);
        }

        var bounds = Model.PartList[part].Bounds;
        var centre = (bounds.Min + bounds.Max) / 2f * Scale;
        return Position + Vector3.Transform(centre, Matrix4x4.CreateRotationZ(float.DegreesToRadians(Yaw)));
    }

    public float DistanceTo(Vector3 point) => Vector3.Distance(Position, point);

    /// <summary>Direction (degrees) from this object to a point.</summary>
    public float YawTo(Vector3 point)
    {
        var degrees = float.RadiansToDegrees(MathF.Atan2(point.Y - Position.Y, point.X - Position.X));
        return ((degrees % 360f) + 360f) % 360f;
    }
}
