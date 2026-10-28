using System.Numerics;
using Mdk.Engine.Platform;
using Mdk.Formats;
using Mdk.Game.Audio;
using Mdk.Game.Collision;

namespace Mdk.Game.Kurt;

/// <summary>Kurt Hectic ("Damp" in the original code): walking, turning, jumping, falling and the
/// chute, through the arenas' BSPs. The original runs at 30 ticks per second; its constants are
/// converted to seconds (see godot-mdk docs/gameplay.md and docs/bsp.md).
/// <code>
///   input ──► speeds (vel_accel, vel_friction) ──► walk move (slide 0.75)
///                                              └─► fall move (slide 0.5) ──► on the floor?
///   items, chain gun, knock-down (Kurt.Combat.cs) ◄────────────────────────┘
///                                              └─► state ──► sprite
/// </code></summary>
public sealed partial class Kurt(ArenaSpace space, SoundMixer mixer, Func<Kurt.State, int> frameCount)
{
    public enum State { Still, Idle, Run, Side, Turn, Jump, RunJump, Fall, Chute, Land, Shot, RunFire, Throw, Knocked, GetUp, Dead,
        Slip, Slide, SlideFast, SlideBrake, Hang, HardLand,
        /// <summary>The 1996 demo's moves (Kurt.Beta.cs).</summary>
        RollLeft, RollRight, HelmetOn, HelmetOff }

    public const float Ticks = 30f;

    // Forward, backward and strafe speeds (u/s) and accelerations (u/s²), normal and turbo.
    private const float MaxSpeed = 0.6667f * Ticks;
    private const float MaxSpeedTurbo = 1.3333f * Ticks;
    private const float Acceleration = 0.04444f * Ticks * Ticks;
    private const float AccelerationTurbo = 0.08889f * Ticks * Ticks;
    /// <summary>Deceleration without input (u/s²): above <see cref="MaxSpeed"/>, and below it.</summary>
    private const float FrictionFast = 0.17778f * Ticks * Ticks;
    private const float FrictionSlow = 0.08889f * Ticks * Ticks;
    /// <summary>Friction and strafe acceleration in the air.</summary>
    private const float AirControl = 0.75f;

    // Keyboard turning (degrees/s, degrees/s²), normal and turbo.
    private const float TurnSpeed = 4f * Ticks;
    private const float TurnSpeedTurbo = 6f * Ticks;
    private const float TurnAcceleration = 0.9f * Ticks * Ticks;
    private const float TurnAccelerationTurbo = 1.3f * Ticks * Ticks;
    private const float TurnFrictionFast = 1.6f * Ticks * Ticks;
    private const float TurnFrictionSlow = 0.55f * Ticks * Ticks;
    private const float MouseDegrees = 0.15f;

    // Vertical movement (u/s, u/s²).
    private const float Gravity = 64f;
    private const float MaxFallSpeed = 250f;
    private const float JumpSpeed = 40f;
    /// <summary>Releasing jump during the first ticks of a jump cuts it short.</summary>
    private const int JumpHoldTicks = 6;
    private const float JumpReleasePenalty = 3.333f;
    /// <summary>Falling faster than this starts the fall (or with jump held, opens the chute).</summary>
    private const float FallStartSpeed = -16f;
    /// <summary>The jumps' frames (0x4646a4): the first ones play one a tick, then frame n waits for the
    /// vertical speed to drop to <see cref="JumpSpeeds"/>[n]; past the last comes the fall.</summary>
    private const int JumpTimedFrames = 10;
    private const int JumpFrames = 16;
    private const int RunJumpTimedFrames = 7;
    private const int RunJumpFrames = 12;
    /// <summary>The jumps' frame speeds (0x492030; the running jump's 0x492074 is the same to frame 12).</summary>
    private static readonly float[] JumpSpeeds = [25f, 26f, 27f, 20f, 15f, 13f, 9f, 6.6f, 4f, 0.2f, -3f, -6.3f, -10f, -14f, -17f, -20f];
    private const float ChuteGravity = 21.33f;
    private const float ChuteFallSpeed = -8f;
    private const float ChuteBrake = 256f;
    /// <summary>CHUTEON flaps after the chute's opening frames.</summary>
    private const int ChuteOpenFrames = 4;
    private const string ChuteOnSound = "CHUTEON";

    // damp_collide_move's slide factors: walking stops within 30° of head-on; falling slides off 45°.
    private const float WalkSlide = 0.75f;
    private const float FallSlide = 0.5f;
    /// <summary>Landing slower than this keeps Kurt from creeping (0.35 u/tick).</summary>
    private const float CreepSpeed = 0.35f * Ticks;
    /// <summary>Downhill glue: on floors flatter than this the feet follow the slope down.</summary>
    private const float GlueNz = 0.25f;
    /// <summary>The glue's fastest drop (u/s), under the hard landing's: the landing that follows it
    /// must not hurt.</summary>
    private const float GlueMaxSpeed = 90f;
    /// <summary>Running, or standing, Kurt starts the fall animation only after this long (seconds) off
    /// the floor: down a steep slope his feet leave it for a step or two at a time, and the animation
    /// flipped between the run and the fall.</summary>
    private const float FallGrace = 0.15f;
    /// <summary>Falling onto a platform first goes to 0.05 above its top (damp_gravity).</summary>
    private const float PlatformLift = 0.05f;
    /// <summary>Objects this far around a move (and Kurt's height above it) may stop it.</summary>
    private static readonly Vector3 NearbyMargin = new(1f, 1f, Solids.PlatformReach);
    private const float KurtHeight = 5f;
    /// <summary>Sunk into a floor between these (#30), Kurt climbs onto it: from what the walking
    /// box steps over (its 0.5 lift) to his waist.</summary>
    private const float SunkMin = 0.5f;
    private const float SunkMax = KurtHeight / 2f;
    /// <summary>Landing faster than this is a hard landing: 10 damage and K_CRASHL (state 806).</summary>
    private const float HardLandingSpeed = -100f;
    private const int HardLandingDamage = 10;
    /// <summary>K_CRASHL plays at 2 ticks a frame; in the air it holds at tick 8.</summary>
    private const int CrashTicksPerFrame = 2;
    private const int CrashHoldTick = 8;
    /// <summary>Kurt dies this far below his arena's lowest point.</summary>
    private const float FallOutDepth = -50f;

    private const float IdleDelay = 6f;
    private const float MovingSpeed = 1f;
    /// <summary>The turning animation covers 45° of rotation.</summary>
    private const float TurnFrameDegrees = 45f;
    /// <summary>Footsteps on these run frames, alternating two pairs of sounds (FOOT3/4 first).</summary>
    private const int LeftStep = 0;
    private const int RightStep = 13;
    /// <summary>The same while running and firing (K_RUNFIR).</summary>
    private const int FireLeftStep = 4;
    private const int FireRightStep = 17;

    public Vector3 Feet;
    /// <summary>Degrees; 90 faces +Y (MDK).</summary>
    public float Yaw;
    public float ForwardSpeed;
    public float StrafeSpeed;
    public float TurnRate;
    public float VerticalSpeed;
    public bool OnFloor;
    public bool ChuteOpen;
    public State Current = State.Still;
    /// <summary>The chute closed in the air this tick: K_CHUTE's closing frame shows once.</summary>
    public bool ChuteClosing { get; private set; }
    /// <summary>The solid object boxes of Kurt's arena within a region (set by the scripts runtime).</summary>
    public Func<Box, IReadOnlyList<Solids.Solid>>? SolidsWithin;
    /// <summary>The object Kurt stands on (0x573b84), or null.</summary>
    public object? Platform { get; private set; }
    public float StateTime;
    /// <summary>Animation position in frames.</summary>
    public float AnimationFrame;
    public CameraRoll CameraRoll { get; } = new();

    private Vector3 _floorNormal = Vector3.UnitZ;
    private bool _footstepPair = true;
    private int _jumpTicksLeft;
    private bool _jumpReleased = true;

    /// <summary>How the last step turned Kurt by the mouse: a frame turns him so at once.</summary>
    private enum MouseTurn { None, Walk, Scope }
    private MouseTurn _mouseTurn;

    /// <summary>Counts the teleports: a frame draws him where he is, not on the way there.</summary>
    public int Teleports { get; private set; }

    public Vector3 Facing => new(MathF.Cos(float.DegreesToRadians(Yaw)), MathF.Sin(float.DegreesToRadians(Yaw)), 0f);

    public Vector3 Right => new(Facing.Y, -Facing.X, 0f);

    /// <summary>Velocity (u/s): walking, the push of a knock-down, falling.</summary>
    public Vector3 Velocity => Facing * ForwardSpeed + Right * StrafeSpeed + new Vector3(_push, VerticalSpeed);

    /// <summary>One step of <paramref name="delta"/> seconds.</summary>
    public void Update(Input input, float delta)
    {
        // The scope's own bindings (zoom, the wheel) apply while he snipes, whatever else this step does.
        input.Scoped = Sniping;
        UpdateStep(input, delta);
        // Entering or leaving the scope this step switches the bindings now, not a step late: a wheel
        // notch right after the sniper key zooms (or, rebound, does its scope action).
        input.Scoped = Sniping;
    }

    private void UpdateStep(Input input, float delta)
    {
        _mouseTurn = MouseTurn.None;
        Invulnerable = MathF.Max(Invulnerable - delta, 0f);
        ChuteClosing = false;
        if (Frozen)
        {
            // Cutscenes and the level's end: he stands still; the flashes still fade (0x478704).
            FadeFlashes(delta);
            StopChuteSound();
            ForwardSpeed = 0f;
            StrafeSpeed = 0f;
            VerticalSpeed = 0f;
            return;
        }

        if (Current == State.Dead)
        {
            StopChuteSound();
            UpdateDeath(delta);
            return;
        }

        FadeFlashes(delta);
        CameraRoll.Settle(delta);
        if (UpdateNoclip(input, delta))
        {
            return;
        }

        if (Ride != null)
        {
            // No knock-down, chute or ledges while riding.
            KnockDamage = 0f;
            StopChuteSound();
            _keeper = null;
            Ride(input, delta);
            return;
        }

        UpdateKnockDamage(delta);
        UpdateChuteSound();
        var turbo = input.IsDown(Key.Turbo);
        if (InBetaMove)
        {
            UpdateBetaMove(delta);
            return;
        }

        if (UpdateSniper(input, turbo, delta) || UpdateSlide(input, delta))
        {
            return;
        }

        if (Current is State.Hang or State.HardLand)
        {
            // States from 800 on: no walking (damp_control).
            UpdateSpecial(delta);
            return;
        }

        if (StartRoll(input))
        {
            return;
        }

        UpdateTurning(input, turbo, delta);
        CheckKeeper();

        // Throwing, knocked down or kept by a platform, he doesn't walk.
        var forward = Axis(input, Key.Forward, Key.Back);
        var strafe = Walking == Walk.Riding ? 0f : Axis(input, Key.StrafeRight, Key.StrafeLeft);
        if (Current is State.Throw or State.Knocked or State.GetUp || Kept)
        {
            forward = 0f;
            strafe = 0f;
        }

        var air = OnFloor ? 1f : AirControl;
        ForwardSpeed = Accelerate(ForwardSpeed, forward, turbo, 1f, air, delta);
        StrafeSpeed = Accelerate(StrafeSpeed, strafe, turbo, air, air, delta);
        // Running while turning with the keys (not the mouse) rolls the view.
        CameraRoll.Walk(-Axis(input, Key.TurnLeft, Key.TurnRight), forward, delta);

        var before = Feet;
        var move = (Facing * ForwardSpeed + Right * StrafeSpeed + new Vector3(_push, 0f)) * delta;
        DrainPush(delta);
        if (move != Vector3.Zero)
        {
            Feet = Contact(space.Move(Feet, move, ArenaSpace.Motion.Walk, WalkSlide, Nearby(Feet + move))).Feet;
        }

        if (Walking == Walk.Riding)
        {
            RideWalker(input, forward, delta);
            return;
        }

        UpdateVertical(input, move, delta);
        StayOnKeeper();
        UpdateTouched();
        if (Current == State.HardLand || GrabLedge(before, forward))
        {
            return;
        }

        UpdateItems(input);
        UpdateFiring(input);
        UpdateState(forward, strafe, delta);
        UpdateMuzzle(delta);
    }

    /// <summary>Moves Kurt (a script's teleport): he stops.</summary>
    public void Teleport(Vector3 feet, float yaw)
    {
        // The chute closes silently.
        ChuteOpen = false;
        StopChuteSound();
        _keeper = null;
        Feet = feet;
        Yaw = yaw;
        ForwardSpeed = 0f;
        StrafeSpeed = 0f;
        VerticalSpeed = 0f;
        Teleports++;
    }

    /// <summary>Moves Kurt by <paramref name="offset"/> (horizontal) with collision, sliding along
    /// what he meets (the camera's push, damp_collide_move). Returns how far he went.</summary>
    public Vector3 Shove(Vector3 offset)
    {
        var start = Feet;
        var move = offset with { Z = 0f };
        if (move != Vector3.Zero)
        {
            Feet = Contact(space.Move(Feet, move, ArenaSpace.Motion.Walk, WalkSlide, Nearby(Feet + move))).Feet;
        }

        return Feet - start;
    }

    /// <summary>Kurt's vertical speed changes (wind zones, push_kurt).</summary>
    public void AddVerticalSpeed(float speed) => VerticalSpeed += speed;

    /// <summary>The platform Kurt stands on moved from <paramref name="from"/> to <paramref name="to"/>
    /// and turned by <paramref name="turn"/> degrees: he moves and turns with it.</summary>
    public void Carry(Vector3 from, Vector3 to, float turn)
    {
        var offset = Vector3.Transform(Feet - from, Matrix4x4.CreateRotationZ(float.DegreesToRadians(turn)));
        Feet = to + offset;
        Yaw += turn;
    }

    /// <summary>The solid object boxes around Kurt and the point <paramref name="to"/>.</summary>
    private IReadOnlyList<Solids.Solid> Nearby(Vector3 to)
    {
        if (SolidsWithin == null)
        {
            return [];
        }

        var min = Vector3.Min(Feet, to) - NearbyMargin;
        var max = Vector3.Max(Feet, to) + NearbyMargin + new Vector3(0f, 0f, KurtHeight);
        return SolidsWithin(new Box(min, max));
    }

    /// <summary>A platform the fall of <paramref name="dz"/> reaches (damp_platform_floor), or null.</summary>
    private (float Top, object Owner)? FindPlatform(float dz)
    {
        if (VerticalSpeed > 0f)
        {
            return null;
        }

        var bottom = Feet.Z + dz;
        var platform = Solids.Platform(Feet, bottom, Nearby(Feet with { Z = bottom }));
        return platform is { } p && bottom <= p.Top ? p : null;
    }

    private static float Axis(Input input, Key positive, Key negative) =>
        (input.IsDown(positive) ? 1f : 0f) - (input.IsDown(negative) ? 1f : 0f);

    private void UpdateTurning(Input input, bool turbo, float delta)
    {
        var turn = Axis(input, Key.TurnLeft, Key.TurnRight);
        if (turn == 0f)
        {
            var friction = MathF.Abs(TurnRate) > TurnSpeed ? TurnFrictionFast : TurnFrictionSlow;
            TurnRate = MoveToward(TurnRate, 0f, friction * delta);
        }
        else
        {
            var max = turbo ? TurnSpeedTurbo : TurnSpeed;
            var acceleration = turbo ? TurnAccelerationTurbo : TurnAcceleration;
            TurnRate = Math.Clamp(TurnRate + turn * acceleration * delta, -max, max);
        }

        Yaw += TurnRate * delta - input.MouseX * MouseDegrees;
        _mouseTurn = MouseTurn.Walk;
    }

    /// <summary>A frame's mouse turn (and the scope's tilt), at once rather than with the next step
    /// (screens above 60 Hz), where the last step turned him by the mouse. Returns the yaw change
    /// in degrees.</summary>
    public float MouseLook(Input input)
    {
        if (_mouseTurn == MouseTurn.Walk)
        {
            var turn = -input.TakeMouseX() * MouseDegrees;
            Yaw += turn;
            return turn;
        }

        if (_mouseTurn != MouseTurn.Scope)
        {
            return 0f;
        }

        // The mouse's share of the scope's look: no keys, no time.
        var mouse = new Vector2(input.TakeMouseX(), input.TakeMouseY()) * MouseDegrees;
        var scope = Scope.Look(0f, 0f, mouse, Scope.Turbo.Off, 0f);
        Yaw += scope;
        return scope;
    }

    /// <summary>Towards <c>input × max speed</c>, or friction without input (vel_accel,
    /// vel_friction). The opposite direction reverses at once.</summary>
    private static float Accelerate(float speed, float input, bool turbo, float accelFactor, float frictionFactor, float delta)
    {
        if (input == 0f)
        {
            var friction = MathF.Abs(speed) > MaxSpeed ? FrictionFast : FrictionSlow;
            return MoveToward(speed, 0f, friction * frictionFactor * delta);
        }

        var acceleration = (turbo ? AccelerationTurbo : Acceleration) * accelFactor;
        var max = turbo ? MaxSpeedTurbo : MaxSpeed;
        if (speed * input < 0f)
        {
            speed = 0f;
        }

        return Math.Clamp(speed + input * acceleration * delta, -max, max);
    }

    private static float MoveToward(float from, float to, float step) =>
        MathF.Abs(to - from) <= step ? to : from + MathF.Sign(to - from) * step;

    /// <summary>Jumping, the chute, gravity and the fall move (damp_vertical 0x4694bc, damp_gravity 0x469efc).</summary>
    private void UpdateVertical(Input input, Vector3 move, float delta)
    {
        var jumpHeld = input.IsDown(Key.Jump);
        if (!jumpHeld)
        {
            _jumpReleased = true;
            if (_jumpTicksLeft > 0)
            {
                VerticalSpeed -= _jumpTicksLeft * JumpReleasePenalty;
                _jumpTicksLeft = 0;
            }
        }

        if (OnFloor)
        {
            ChuteOpen = false;
            if (jumpHeld && _jumpReleased)
            {
                VerticalSpeed = JumpSpeed;
                _jumpTicksLeft = JumpHoldTicks;
                _jumpReleased = false;
            }
            else
            {
                GlueDownhill(move, delta, exact: true);
            }
        }
        else
        {
            UpdateAir(jumpHeld, delta);
        }

        UpdateUpdraft(delta);
        Fall(delta);
        _blown = false;
    }

    /// <summary>In the air: the jump's hold time runs out; held jump opens the chute when falling
    /// fast or past a jump's top, letting go closes it.</summary>
    private void UpdateAir(bool jumpHeld, float delta)
    {
        if (_jumpTicksLeft > 0)
        {
            _jumpTicksLeft = Math.Max(0, _jumpTicksLeft - (int)MathF.Ceiling(delta * Ticks));
        }

        // The wind keeps the chute shut.
        if (!ChuteOpen && jumpHeld && !_blown && (VerticalSpeed < FallStartSpeed || _pastJumpTop))
        {
            ChuteOpen = true;
            mixer.Play("CHUTEOUT");
        }
        else if (ChuteOpen && (_blown || (!jumpHeld && !InUpdraft)))
        {
            ChuteOpen = false;
        }

        if (!ChuteOpen)
        {
            VerticalSpeed = MathF.Max(VerticalSpeed - Gravity * delta, -MaxFallSpeed);
            return;
        }

        VerticalSpeed -= ChuteGravity * delta;
        if (VerticalSpeed < ChuteFallSpeed)
        {
            VerticalSpeed = MathF.Min(VerticalSpeed + ChuteBrake * delta, ChuteFallSpeed);
        }
    }

    /// <summary>CHUTEON flaps while the chute is open (after its opening frames); closing stops it,
    /// with CHUTEIN (damp_animate).</summary>
    private void UpdateChuteSound()
    {
        if (ChuteOpen && Current == State.Chute && AnimationFrame >= ChuteOpenFrames)
        {
            mixer.Play(ChuteOnSound, SoundMixer.Start.Once);
            return;
        }

        if (ChuteOpen || !mixer.IsPlaying(ChuteOnSound))
        {
            return;
        }

        mixer.Stop(ChuteOnSound);
        mixer.Play("CHUTEIN");
    }

    /// <summary>CHUTEON stops without CHUTEIN when Kurt leaves the chute otherwise: death, a
    /// teleport, a ride, a cutscene or the level's end (it played on until the level ended).</summary>
    private void StopChuteSound() => mixer.Stop(ChuteOnSound);

    /// <summary>On a floor flatter than 75°, walking downhill keeps the feet on it. With
    /// <paramref name="exact"/>, the feet follow the slope's whole drop (the move along the slope over
    /// the normal's z), not only its share along the normal: on a steep floor the feet stayed above it
    /// every other step, and Kurt flickered between the run and the fall.</summary>
    private void GlueDownhill(Vector3 move, float delta, bool exact = false)
    {
        var n = _floorNormal.Z < 0f ? -_floorNormal : _floorNormal;
        var downhill = move.X * n.X + move.Y * n.Y;
        if (n.Z > GlueNz && downhill > 0f)
        {
            var speed = (exact ? downhill / n.Z : downhill) / delta;
            VerticalSpeed = MathF.Min(VerticalSpeed, -MathF.Min(speed, exact ? GlueMaxSpeed : speed));
        }

        VerticalSpeed = MathF.Max(VerticalSpeed - Gravity * delta, -MaxFallSpeed);
    }

    /// <summary>The fall move, then out of the arena: 50 below its lowest point Kurt dies
    /// (damp_gravity 0x469efc; he counts as on a floor, so the death plays).</summary>
    private void Fall(float delta)
    {
        FallMove(delta);
        if (space.Bottom(Feet) is not { } bottom || Feet.Z > bottom + FallOutDepth || Mortality == Mortality.God)
        {
            return;
        }

        Health = 0;
        VerticalSpeed = 0f;
        OnFloor = true;
    }

    /// <summary>The fall move: a hit going down lands, going up bumps the head; sliding keeps the
    /// fall rate. A platform under the feet stops the fall at its top (damp_gravity 0x469efc).</summary>
    private void FallMove(float delta)
    {
        var before = Feet;
        var dz = VerticalSpeed * delta;
        var platform = KeptPlatform(dz) ?? FindPlatform(dz);
        if (platform is { } below)
        {
            dz = below.Top + PlatformLift - Feet.Z;
        }

        var result = Contact(space.Move(Feet, new Vector3(0f, 0f, dz), ArenaSpace.Motion.Fall, FallSlide));
        Feet = result.Feet;
        Platform = null;
        if (!result.Hit && platform is { } on)
        {
            Feet = Feet with { Z = on.Top };
            Platform = on.Owner;
            KeepOn(on.Owner);
            Land(before, Vector3.UnitZ, delta);
            return;
        }

        if (!result.Hit)
        {
            // Sliding down a steep face: the real fall rate.
            if (Feet.Z < before.Z)
            {
                VerticalSpeed = (Feet.Z - before.Z) / delta;
            }

            OnFloor = false;
            return;
        }

        if (Sliding)
        {
            VerticalSpeed = (Feet.Z - before.Z) / delta;
            OnFloor = true;
            _floorNormal = result.Normal;
            return;
        }

        if (VerticalSpeed > 0f)
        {
            VerticalSpeed = 0f;
            OnFloor = false;
            return;
        }

        ClimbOut(result);
        Land(before, result.Normal, delta);
    }

    /// <summary>Deviation from the original (#30): a floor without a side wall at its edge lets the
    /// walking box in from the side (the BSP has faces, no volume), and every fall move then stops
    /// at once on its top (bsp_sweep_node 0x409680, damp_collide_move 0x465e34): Kurt walked on 2
    /// inside level 8's walkway until a jump. Sunk deeper than a step, up to his waist, he climbs
    /// onto the top when there is room above. Not on a ride (the board, the bomber).</summary>
    private void ClimbOut(ArenaSpace.Result result)
    {
        var sunk = ArenaSpace.Sunk(result, Feet);
        if (sunk <= SunkMin || sunk > SunkMax || Ride != null)
        {
            return;
        }

        var up = space.Move(Feet, new Vector3(0f, 0f, sunk), ArenaSpace.Motion.Fall, FallSlide);
        if (up.Hit)
        {
            return;
        }

        Feet = up.Feet;
    }

    private void Land(Vector3 before, Vector3 normal, float delta)
    {
        if (VerticalSpeed < HardLandingSpeed && Ride == null)
        {
            HardLanding();
        }
        else if (Vector3.DistanceSquared(Feet, before) < CreepSpeed * delta * (CreepSpeed * delta))
        {
            // No creeping on a landing.
            Feet = before;
        }

        VerticalSpeed = 0f;
        OnFloor = true;
        _floorNormal = normal;
    }

    /// <summary>A hard landing: 10 damage (no knock-down) and K_CRASHL; the board skips it.</summary>
    private void HardLanding()
    {
        if (Health > 0)
        {
            Damage(HardLandingDamage);
        }

        KnockDamage = 0f;
        if (Health == 0 || Current is State.Dead or State.Knocked or State.GetUp)
        {
            return;
        }

        StopFiring();
        ChuteOpen = false;
        SetState(State.HardLand);
    }

    /// <summary>States from 800 on: climbing a ledge, or the hard landing's K_CRASHL with gravity
    /// (held at tick 8 while still falling).</summary>
    private void UpdateSpecial(float delta)
    {
        if (Current == State.Hang)
        {
            UpdateClimb(delta);
            return;
        }

        StateTime += delta;
        VerticalSpeed = MathF.Max(VerticalSpeed - Gravity * delta, -MaxFallSpeed);
        Fall(delta);
        UpdateTouched();
        var ticks = AnimationFrame * CrashTicksPerFrame;
        var next = ticks + Ticks * delta;
        if (ticks <= CrashHoldTick && next >= CrashHoldTick && !OnFloor && VerticalSpeed <= 0f && !ChuteOpen)
        {
            next = CrashHoldTick;
        }

        AnimationFrame = next / CrashTicksPerFrame;
        if (AnimationFrame >= frameCount(State.HardLand))
        {
            SetState(State.Still);
        }
    }

    /// <summary>How long Kurt has been off the floor (seconds).</summary>
    private float _airFor;
    /// <summary>A jump's frames that follow the speed reached its top (0x573a48): the chute may open,
    /// without waiting to fall faster than <see cref="FallStartSpeed"/>.</summary>
    private bool _pastJumpTop;

    private void UpdateState(float forward, float strafe, float delta)
    {
        StateTime += delta;
        _airFor = OnFloor ? 0f : _airFor + delta;
        if (OnFloor || (VerticalSpeed > 0f && !ChuteOpen))
        {
            _pastJumpTop = false;
        }
        if (UpdateCombatState(delta))
        {
            return;
        }

        var done = AnimationFrame >= frameCount(Current) - 1;
        if (!OnFloor)
        {
            if (ChuteOpen)
            {
                SetState(State.Chute);
            }
            else if (Current == State.Chute)
            {
                // Closed in the air: its closing frame, then the fall.
                ChuteClosing = true;
                SetState(State.Fall);
                return;
            }
            else if (VerticalSpeed > 0f && Current is not (State.Jump or State.RunJump or State.Fall))
            {
                SetState(MathF.Abs(ForwardSpeed) > MovingSpeed ? State.RunJump : State.Jump);
            }
            // A jump falls when its frames run out (Animate), as the original's priorities have it.
            else if (VerticalSpeed < FallStartSpeed && Current is not (State.Jump or State.RunJump)
                && (_airFor >= FallGrace || Current == State.Fall))
            {
                SetState(State.Fall);
            }
        }
        else if (Current is State.Jump or State.RunJump or State.Fall or State.Chute)
        {
            mixer.Play("LAND");
            SetState(State.Land);
        }
        else if (Current == State.Land && !done && forward == 0f && strafe == 0f)
        {
        }
        else if (forward != 0f || MathF.Abs(ForwardSpeed) > MovingSpeed)
        {
            SetState(Firing ? State.RunFire : State.Run);
        }
        else if (strafe != 0f || MathF.Abs(StrafeSpeed) > MovingSpeed)
        {
            SetState(State.Side);
        }
        else if (MathF.Abs(TurnRate) > MovingSpeed)
        {
            SetState(State.Turn);
        }
        else if (Firing)
        {
            SetState(State.Shot);
        }
        else if (Current == State.Idle && !done)
        {
        }
        else if (Current == State.Still && StateTime > IdleDelay)
        {
            SetState(State.Idle);
        }
        else if (Current != State.Still)
        {
            SetState(State.Still);
        }

        Animate(delta);
    }

    private void Animate(float delta)
    {
        switch (Current)
        {
            case State.Run or State.RunFire:
                var count = frameCount(Current);
                var previous = Wrap((int)MathF.Floor(AnimationFrame), count);
                AnimationFrame += RunRate() * Ticks * delta;
                var current = Wrap((int)MathF.Floor(AnimationFrame), count);
                Footstep(previous, current);
                break;
            case State.Turn:
                AnimationFrame = Yaw / TurnFrameDegrees * frameCount(State.Turn);
                break;
            case State.Jump or State.RunJump:
                AnimateJump(delta);
                break;
            default:
                AnimationFrame += Ticks * delta;
                break;
        }
    }

    /// <summary>A jump's frames (0x4646a4): timed, then following the vertical speed down through its
    /// top, then the fall. Played through by time, the jump ended while Kurt still rose, and he hung on
    /// its last frame until falling fast enough.</summary>
    private void AnimateJump(float delta)
    {
        var (timed, frames) = Current == State.Jump ? (JumpTimedFrames, JumpFrames) : (RunJumpTimedFrames, RunJumpFrames);
        if (AnimationFrame < timed)
        {
            AnimationFrame = MathF.Min(AnimationFrame + Ticks * delta, timed);
            return;
        }

        var frame = (int)AnimationFrame;
        while (frame < frames && VerticalSpeed <= JumpSpeeds[frame])
        {
            frame++;
        }

        AnimationFrame = frame;
        _pastJumpTop |= VerticalSpeed <= 0f;
        if (frame >= frames)
        {
            SetState(State.Fall);
        }
    }

    private static int Wrap(int frame, int count) => ((frame % count) + count) % count;

    private void Footstep(int previous, int current)
    {
        if (current == previous)
        {
            return;
        }

        var (left, right) = Current == State.RunFire ? (FireLeftStep, FireRightStep) : (LeftStep, RightStep);
        if (current == left)
        {
            mixer.Play(_footstepPair ? "FOOT3" : "FOOT1");
        }
        else if (current == right)
        {
            mixer.Play(_footstepPair ? "FOOT4" : "FOOT2");
            _footstepPair = !_footstepPair;
        }
    }

    /// <summary>damp_run_anim_frame: the run animation follows the speed, backwards when backing up.</summary>
    private float RunRate()
    {
        var u = MathF.Abs(ForwardSpeed) / Ticks * 1.5f;
        var rate = u <= 1f ? 0.75f * u + 0.25f : 0.25f * u + 0.75f;
        return ForwardSpeed < 0f && !BackingUp ? -rate : rate;
    }

    private void SetState(State state)
    {
        if (state == Current && StateTime > 0f)
        {
            return;
        }

        Current = state;
        StateTime = 0f;
        AnimationFrame = 0f;
    }
}
