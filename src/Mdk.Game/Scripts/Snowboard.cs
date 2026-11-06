using System.Numerics;
using Mdk.Engine.Platform;
using Mdk.Game.Audio;
using Mdk.Game.Objects;

namespace Mdk.Game.Scripts;

/// <summary>Kurt on the snowboard of level 4 (<c>XSNOWB</c>, <c>snowboard_update</c> 0x46ac4c;
/// snowboard.gd, godot-mdk docs/gameplay.md "The snowboard"). The board's path only guides the
/// heading <c>H</c>, which turns towards the path at 40°/s (faster carving into the turn, slower
/// against it). Kurt steers with the carve angle <c>S</c> (±30°), which slides him sideways
/// (<c>L</c>), and speeds up or brakes (<c>V</c>); slopes speed him up or slow him down. The board
/// sits under his feet at the yaw <c>H + S</c>.
/// <code>
///   input ──► S ──► L (sideways)     path ──► H (heading)     slope, keys ──► V (forward)
/// </code></summary>
public sealed class Snowboard
{
    private const float Ticks = Kurt.Kurt.Ticks;
    /// <summary>Steering: ±30° at 4°/tick (reached over 15 ticks on the ground), back at 3°/tick.</summary>
    private const float SteerMax = 30f;
    private const float SteerRate = 4f;
    /// <summary>The mouse: 4 × clamp(dx / dt, ±4) °/tick (input_read_axes 0x408334), dx in the
    /// original's units: its walk turns 3° a unit, the port's 0.15° a mouse unit.</summary>
    private const float MouseUnits = 0.05f;
    private const float MouseLimit = 4f;
    private const float SteerRampTicks = 15f;
    private const float SteerReturn = 3f;
    /// <summary>Sideways speed <c>L = −S·V/96</c>, braked in the air by 0.0055556 per tick.</summary>
    private const float SideFactor = -0.8333f * 0.0125f;
    private const float SideAirBrake = 0.0055556f;
    /// <summary>Speeds (units/tick): cruise 1.5, up to 2.5 with the key (or locked), down to 1.1667
    /// braking or uphill, up to 2.6667 downhill; 0.05 per tick², decay above cruise 0.0027778.</summary>
    private const float Cruise = 1.5f;
    private const float Fast = 2.5f;
    private const float Slow = 1.16667f;
    private const float Downhill = 2.66667f;
    private const float Acceleration = 0.05f;
    private const float Decay = 0.0027778f;
    private const float SlopeAcceleration = 0.066667f;
    /// <summary>The thrown board falls by 64 u/s².</summary>
    private const float Gravity = 64f;
    /// <summary>The board's ends are 4 units from its middle; ground up to 3 units above them counts.</summary>
    private const float BoardHalf = 4f;
    private const float RayHeight = 3f;
    private const float RayBelow = 0.01f;
    /// <summary>The board pitches towards the ground at 90°/s, between −45° and 45°.</summary>
    private const float PitchRate = 90f;
    private const float PitchLimit = 45f;
    private const float HalfTurn = 180f;
    private const float FullTurn = 360f;
    /// <summary>Carving tilts the nose up by a quarter of <c>|S|</c>.</summary>
    private const float CarveTilt = 0.25f;
    /// <summary>Jump: 28.5 units/s up, the key counts up to 12 ticks before landing; not at the end of CMEAT_1.</summary>
    private const float JumpSpeed = 28.5f;
    private const float JumpLift = 1f;
    private const int JumpBuffer = 12;
    private const float JumpHeld = 50f;
    private const string NoJumpArena = "CMEAT_1";
    private const float NoJumpY = 2131f;
    /// <summary>Heading: 4/3 °/tick ± S/30 on the ground, 0.4 in the air; Kurt's yaw follows it at 360°/s.</summary>
    private const float HeadingRate = 1.33333f;
    private const float HeadingCarve = 1.33333f / 30f * 0.75f;
    private const float HeadingAir = 0.4f;
    private const float KurtTurn = 360f;
    /// <summary>The path search's steps (path frames), and the look ahead for its heading.</summary>
    private static readonly float[] PathSteps = [1f, 0.2f];
    private const float PathAhead = 0.2f;
    /// <summary>Kurt nudged back towards the path when stuck (moving under 0.5 units/tick at speed).</summary>
    private const float UnstickSpeed = 0.5f;
    private const float UnstickStep = 2f;
    /// <summary>The board sits 0.25 below Kurt's feet; thrown at 1.1 × his speed.</summary>
    private const float BoardDrop = 0.25f;
    private const float ThrowFactor = 1.1f;
    /// <summary>Sounds: SKITURN beyond 22.5°, SKILAND after 15 ticks in the air, SKI stops after 7.</summary>
    private const float TurnSound = 22.5f;
    private const float LandTicks = 15f;
    private const float QuietTicks = 7f;
    private static readonly string[] Sounds = ["SKI", "SKITURN", "SKILAND"];
    /// <summary>Ramming: objects without these flags and with 0 &lt; health &lt; 65000 die; Kurt takes 5.</summary>
    private const int RamSpared = 0x40304000;
    private const int RamDamage = 5;
    /// <summary>K_SURF loops; K_SURFJ holds on frame 5 in the air, lands from frame 6 to 10.</summary>
    private const string SurfFrames = "K_SURF";
    private const string JumpFrames = "K_SURFJ";
    private const int SurfLoop = 8;
    private const int JumpHoldFrame = 5;
    private const int JumpLandFrame = 6;
    private const float LateLanding = 3f;
    private const float LandEnd = 11f;
    /// <summary>The camera pivot dips by 0.2 a frame while K_SURFJ takes off, then comes back up by 1 u/s.</summary>
    private const float PivotDip = 0.2f;

    private enum Pose { Surf, Jump, Land }

    public readonly MdkObject Board;

    private readonly ScriptRuntime _runtime;
    private readonly Kurt.Kurt _kurt;
    private float _speed;
    private float _side;
    private float _heading;
    private float _steer;
    private float _pitch;
    private float _turnTicks;
    private float _jumpTicks;
    private float _airTicks;
    private bool _grounded = true;
    private float _pathHeading;
    private Pose _pose = Pose.Surf;
    private float _frame;

    public Snowboard(ScriptRuntime runtime, MdkObject board)
    {
        _runtime = runtime;
        _kurt = runtime.Kurt;
        Board = board;
        _heading = board.Yaw;
        _pathHeading = board.Yaw;
    }

    /// <summary>Forward speed (u/tick), for tests.</summary>
    public float Speed => _speed;

    /// <summary>One step of riding (instead of Kurt's walking).</summary>
    public void Update(Input input, float delta)
    {
        var dt = delta * Ticks;
        var controls = (Board.Flags & Rides.FlagLocked) == 0;
        _pitch -= MathF.Abs(_steer) * CarveTilt;

        _steer = Steer(_steer, controls ? TurnInput(input, dt) : 0f, _grounded, ref _turnTicks, dt);
        if (_grounded)
        {
            UpdateSpeed(controls ? Axis(input, Key.Forward, Key.Back) : 0f, controls, dt);
        }
        else
        {
            _side = MoveToward(_side, 0f, SideAirBrake * dt);
        }

        // Forward and sideways, sticking to downhill floors, falling.
        var h = float.DegreesToRadians(_heading);
        var move = new Vector3(MathF.Cos(h), MathF.Sin(h), 0f) * _speed + new Vector3(MathF.Sin(h), -MathF.Cos(h), 0f) * _side;
        var previous = _kurt.Feet;
        _kurt.Glide(move * Ticks, delta);

        UpdatePitch(delta);
        if (_grounded)
        {
            _speed = SlopeSpeed(_speed, _pitch, dt);
        }

        Jump(input, controls, dt);
        UpdateSounds(dt);
        if (_pose == Pose.Jump)
        {
            _kurt.StopFiring();
        }
        else
        {
            _kurt.UpdateGun(input, delta);
        }

        FollowPath(previous, dt);
        Turn(delta, dt);
        Place(delta);
        Ram();
        Animate(dt);
    }

    /// <summary>Getting off (the script clears the rideable flag, or Kurt died): he keeps the board's
    /// speeds as his walking ones, and the board is thrown on at 1.1 × his velocity.</summary>
    public void GetOff()
    {
        foreach (var sound in Sounds)
        {
            _runtime.Mixer.Stop(sound);
        }

        var y = float.DegreesToRadians(_kurt.Yaw);
        var throwing = new Vector3(_speed * MathF.Cos(y) + _side * MathF.Sin(y), _speed * MathF.Sin(y) - _side * MathF.Cos(y), 0f);
        Board.Velocity = (throwing * Ticks * ThrowFactor) with { Z = _kurt.VerticalSpeed };
        Board.Gravity = Gravity;
        Board.Friction = 0f;
        Board.Position = _kurt.Feet;
        _kurt.GetOffBoard(_speed * Ticks, _side * Ticks);
    }

    /// <summary>The carve angle <c>S</c> (positive left): the turn input <paramref name="right"/>
    /// (°/tick, &gt; 0 right) turns it, ramping up over 15 ticks on the ground; the other way snaps
    /// it straight; without input it goes back at 3°/tick.</summary>
    public static float Steer(float steer, float right, bool grounded, ref float turnTicks, float dt)
    {
        if (right == 0f)
        {
            turnTicks = 0f;
            return MoveToward(steer, 0f, SteerReturn * dt);
        }

        turnTicks += dt;
        if ((right > 0f && steer > 0f) || (right < 0f && steer < 0f))
        {
            return 0f;
        }

        var rate = right * (grounded && turnTicks < SteerRampTicks ? turnTicks * 2f / Ticks : 1f);
        return Math.Clamp(steer - rate * dt, -SteerMax, SteerMax);
    }

    /// <summary>Speed on the ground: the keys speed up (to 2.5) or brake (to 1.1667), else back to
    /// cruise 1.5; locked controls speed up to 2.5 on their own. Carving slides sideways.</summary>
    private void UpdateSpeed(float forward, bool controls, float dt)
    {
        if (_steer != 0f)
        {
            _side = _steer * SideFactor * _speed;
        }

        _speed = Accelerate(_speed, controls ? forward : 1f, dt);
    }

    /// <summary>The forward speed after a step: <paramref name="forward"/> &gt; 0 speeds up, &lt; 0 brakes, 0 cruises.</summary>
    public static float Accelerate(float speed, float forward, float dt)
    {
        if (forward > 0f)
        {
            return speed < Fast ? MathF.Min(speed + Acceleration * dt, Fast) : speed;
        }

        if (forward < 0f)
        {
            return speed > Slow ? MathF.Max(speed - Acceleration * dt, Slow) : speed;
        }

        return speed < Cruise ? MathF.Min(speed + Acceleration * dt, Cruise) : MathF.Max(speed - Decay * dt, Cruise);
    }

    /// <summary>Slopes: nose down (pitch above 180) speeds up to 2.6667, nose up slows down to 1.1667.</summary>
    public static float SlopeSpeed(float speed, float pitch, float dt)
    {
        var k = MathF.Sin(float.DegreesToRadians(pitch));
        if (pitch > HalfTurn && speed < Downhill)
        {
            return MathF.Min(speed - k * SlopeAcceleration * dt, Downhill);
        }

        if (pitch > 0f && pitch <= HalfTurn && speed > Slow)
        {
            return MathF.Max(speed - k * SlopeAcceleration * dt, Slow);
        }

        return speed;
    }

    /// <summary>The board's pitch from the ground under its ends, turning at 90°/s; any ground under
    /// the board means it's grounded.</summary>
    private void UpdatePitch(float delta)
    {
        var p = _kurt.Feet;
        var yaw = float.DegreesToRadians(_heading + _steer);
        var forward = new Vector3(MathF.Cos(yaw), MathF.Sin(yaw), 0f) * BoardHalf;
        var front = Ground(p + forward);
        var back = Ground(p - forward);
        _grounded = front != null || back != null;
        if (!_grounded)
        {
            return;
        }

        var v = (front, back) switch
        {
            ({ } f, { } b) => f - b,
            ({ } f, null) => f - p,
            _ => p - back!.Value,
        };
        var target = Wrap360(float.RadiansToDegrees(MathF.Atan2(v.Z, new Vector2(v.X, v.Y).Length())));
        if (_pitch > PitchLimit && _pitch < HalfTurn)
        {
            target = PitchLimit;
        }
        else if (_pitch >= HalfTurn && _pitch < FullTurn - PitchLimit)
        {
            target = FullTurn - PitchLimit;
        }

        var step = PitchRate * delta;
        _pitch = Wrap360(WrapHalf(_pitch) + Math.Clamp(WrapHalf(target - _pitch), -step, step));
    }

    /// <summary>The ground under a board end: up to 3 units above it, or null.</summary>
    private Vector3? Ground(Vector3 end) =>
        _runtime.Raycast(end + new Vector3(0f, 0f, RayHeight), end - new Vector3(0f, 0f, RayBelow))?.Point;

    /// <summary>Jumping: on the ground, within 12 ticks of pressing the key.</summary>
    private void Jump(Input input, bool controls, float dt)
    {
        var blocked = _runtime.CurrentArena == NoJumpArena && _kurt.Feet.Y >= NoJumpY;
        if (!controls || !input.IsDown(Key.Jump) || blocked)
        {
            _jumpTicks = 0f;
            return;
        }

        if (_grounded && _jumpTicks <= JumpBuffer)
        {
            _kurt.VerticalSpeed = JumpSpeed;
            _kurt.Feet += new Vector3(0f, 0f, JumpLift);
            _kurt.OnFloor = false;
            _grounded = false;
            _jumpTicks = JumpHeld;
            _pose = Pose.Jump;
            _frame = 0f;
        }

        _jumpTicks += dt;
    }

    private void UpdateSounds(float dt)
    {
        var mixer = _runtime.Mixer;
        if (!_grounded)
        {
            _airTicks += dt;
            if (_airTicks >= QuietTicks && mixer.IsPlaying("SKI"))
            {
                mixer.Stop("SKI");
                mixer.Stop("SKITURN");
            }

            return;
        }

        if (_airTicks >= LandTicks)
        {
            mixer.Play("SKILAND", SoundMixer.Start.Restart);
        }

        _airTicks = 0f;
        mixer.Play("SKI", SoundMixer.Start.Once);
        if (MathF.Abs(_steer) > TurnSound)
        {
            mixer.Play("SKITURN", SoundMixer.Start.Once);
        }
        else
        {
            mixer.Stop("SKITURN");
        }
    }

    /// <summary>The heading follows the board's path: the path time moves on to the point nearest to
    /// Kurt, the direction there is the path's heading; a stuck Kurt is nudged towards the path.</summary>
    private void FollowPath(Vector3 previous, float dt)
    {
        var motion = _runtime.Motion;
        if (Board.Path == 0)
        {
            return;
        }

        var last = motion.PathKeyFrame(Board.Path, motion.PathKeyCount(Board.Path) - 1);
        var t = Board.PathTime;
        if (t >= last)
        {
            return;
        }

        var kurt = new Vector2(_kurt.Feet.X, _kurt.Feet.Y);
        var start = t;
        foreach (var step in PathSteps)
        {
            while (t + step < last && Distance(t + step, kurt) < Distance(t, kurt))
            {
                t += step;
            }
        }

        var moved = new Vector2(_kurt.Feet.X - previous.X, _kurt.Feet.Y - previous.Y).Length();
        var stuck = _speed >= Slow && dt > 0f && moved / dt < UnstickSpeed;
        if (t == start)
        {
            // Stuck where the path time can't move on (against a wall beside it): towards the path ahead.
            if (stuck)
            {
                Nudge(PathPoint(t + PathAhead), kurt);
            }

            return;
        }

        var here = PathPoint(start);
        var ahead = PathPoint(t + PathAhead);
        Board.PathTime = t;
        Board.PathStop = (int)MathF.Round(t);
        _pathHeading = float.RadiansToDegrees(MathF.Atan2(ahead.Y - here.Y, ahead.X - here.X));
        if (stuck)
        {
            Nudge(here, kurt);
        }
    }

    /// <summary>Moves a stuck Kurt a step towards <paramref name="to"/>.</summary>
    private void Nudge(Vector3 to, Vector2 kurt)
    {
        var way = new Vector2(to.X, to.Y) - kurt;
        if (way == Vector2.Zero)
        {
            return;
        }

        _kurt.Feet += new Vector3(Vector2.Normalize(way) * UnstickStep, 0f);
    }

    private Vector3 PathPoint(float t) => _runtime.Motion.PathPosition(Board.Path, t) + Board.PathOrigin;

    private float Distance(float t, Vector2 kurt)
    {
        var p = PathPoint(t);
        return Vector2.DistanceSquared(new Vector2(p.X, p.Y), kurt);
    }

    /// <summary>The heading turns towards the path's (faster carving into the turn); Kurt faces the heading.</summary>
    private void Turn(float delta, float dt)
    {
        var rate = HeadingAir;
        if (_grounded)
        {
            var d = WrapHalf(_pathHeading - _heading);
            rate = HeadingRate + (d >= 0f ? _steer : -_steer) * HeadingCarve;
        }

        _heading = TurnTowards(_heading, _pathHeading, MathF.Max(rate, 0f) * dt);
        _kurt.Yaw = TurnTowards(_kurt.Yaw, _heading, KurtTurn * delta);
    }

    private static float TurnTowards(float from, float to, float step) => Wrap360(from + Math.Clamp(WrapHalf(to - from), -step, step));

    /// <summary>The board under Kurt's feet at the yaw <c>H + S</c>, its nose up while carving.</summary>
    private void Place(float delta)
    {
        _pitch = Wrap360(_pitch + MathF.Abs(_steer) * CarveTilt);
        Board.Position = _kurt.Feet - new Vector3(0f, 0f, BoardDrop);
        Board.Yaw = Wrap360(_heading + _steer);
        Board.Pitch = WrapHalf(_pitch);
        // The view rolls with the board's bank (0x46ac4c).
        _kurt.CameraRoll.Follow(WrapHalf(Board.Roll), delta);
    }

    /// <summary>Whatever Kurt runs into dies, and he takes 5 damage.</summary>
    private void Ram()
    {
        foreach (var owner in _kurt.Touched)
        {
            if (owner is not MdkObject obj || obj == Board || obj.Dead || (obj.Flags & RamSpared) != 0 || obj.Health <= 0 || obj.Health >= ScriptRuntime.Indestructible)
            {
                continue;
            }

            _runtime.Kill(obj);
            _runtime.HurtKurt(RamDamage);
        }
    }

    /// <summary>K_SURF loops; K_SURFJ holds on frame 5 in the air and lands on frames 6-10.</summary>
    private void Animate(float dt)
    {
        _frame += dt;
        switch (_pose)
        {
            case Pose.Surf:
                _kurt.CameraPivot = Kurt.Kurt.DefaultPivot;
                _kurt.Pose = (SurfFrames, (int)_frame % SurfLoop);
                return;
            case Pose.Jump:
                _frame = MathF.Min(_frame, JumpHoldFrame);
                if (_grounded && _kurt.VerticalSpeed <= 0f)
                {
                    _pose = _frame > LateLanding ? Pose.Land : Pose.Surf;
                    _frame = _pose == Pose.Land ? JumpLandFrame : 0f;
                }

                break;
            case Pose.Land:
                if (_frame >= LandEnd)
                {
                    _pose = Pose.Surf;
                    _frame = 0f;
                }

                break;
        }

        _kurt.CameraPivot = JumpPivot(_frame, _kurt.CameraPivot, dt / Ticks);
        _kurt.Pose = (_pose == Pose.Surf ? SurfFrames : JumpFrames, (int)_frame);
    }

    /// <summary>The camera pivot at K_SURFJ frame <paramref name="frame"/> (0x46ac4c), coming from
    /// <paramref name="pivot"/> after <paramref name="seconds"/>.</summary>
    public static float JumpPivot(float frame, float pivot, float seconds) =>
        frame < JumpHoldFrame ? Kurt.Kurt.DefaultPivot - (int)frame * PivotDip : MathF.Min(pivot + seconds, Kurt.Kurt.DefaultPivot);

    /// <summary>The turn input <c>I</c> (°/tick, &gt; 0 right; 0x5014ec): the larger of the turn and
    /// strafe keys × 4 (the strafe on a tie), or the mouse's 4 × clamp(dx / dt, ±4) when it moved.</summary>
    public static float TurnInput(Input input, float dt)
    {
        if (input.MouseX != 0f && dt > 0f)
        {
            return SteerRate * Math.Clamp(input.MouseX * MouseUnits / dt, -MouseLimit, MouseLimit);
        }

        var turn = Axis(input, Key.TurnRight, Key.TurnLeft);
        var strafe = Axis(input, Key.StrafeRight, Key.StrafeLeft);
        return SteerRate * (MathF.Abs(strafe) < MathF.Abs(turn) ? turn : strafe);
    }

    private static float Axis(Input input, Key positive, Key negative) =>
        (input.IsDown(positive) ? 1f : 0f) - (input.IsDown(negative) ? 1f : 0f);

    private static float MoveToward(float from, float to, float step) =>
        MathF.Abs(to - from) <= step ? to : from + MathF.Sign(to - from) * step;

    private static float Wrap360(float degrees) => ((degrees % FullTurn) + FullTurn) % FullTurn;

    private static float WrapHalf(float degrees) => Wrap360(degrees + HalfTurn) - HalfTurn;
}
