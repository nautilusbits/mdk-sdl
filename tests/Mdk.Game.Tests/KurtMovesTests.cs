using System.Numerics;
using Mdk.Engine.Audio;
using Mdk.Engine.Platform;
using Mdk.Formats;
using Mdk.Game.Audio;
using Mdk.Game.Collision;
using Mdk.Game.Kurt;
using Mdk.Game.Level;
using State = Mdk.Game.Kurt.Kurt.State;

namespace Mdk.Game.Tests;

/// <summary>Kurt's other moves (godot-mdk docs/gameplay.md): ledges (damp_ledge_grab 0x469868),
/// sliding (damp_buttslide 0x468db8), hard landings and falling out of the arena (damp_gravity
/// 0x469efc), and the camera's roll, clearance (0x417ee8) and shake.</summary>
public class KurtMovesTests
{
    private const float Step = 1f / 60f;
    private const float Tick = 1f / 30f;
    private const int Frames = 5;
    /// <summary>K_HANG's frames: climbing takes 2 × 16 − 1 ticks; K_CRASHL's: 52 ticks.</summary>
    private const int HangFrames = 16;
    private const int CrashFrames = 26;
    /// <summary>Level 3's start pad (Kurt stands at z 192).</summary>
    private static readonly Vector3 Pad = new(-4f, 0f, 195f);
    /// <summary>Level 3: running and jumping east here, Kurt grabs the ledge at x 83 and climbs it
    /// (the Godot port's Kurt ends at (93, 446, 145) too).</summary>
    private static readonly Vector3 BelowLedge = new(67.6f, 441.8f, 133.5f);

    /// <summary>Read on first use: the tests without the game's files run on CI too.</summary>
    private static readonly Lazy<MdkData> GameFiles = new(() => MdkData.Find() ?? throw new InvalidOperationException("MDK data not found"));
    private static MdkData Data => GameFiles.Value;
    private static readonly AudioDevice Device = new(Output.Muted);

    /// <summary>A level's arena (its start arena by default), Kurt's only solid one.</summary>
    private static ArenaSpace SpaceOf(int level, string? name = null)
    {
        var data = new LevelData(Data, level);
        name ??= data.Dti.Arenas[data.Dti.StartArena].Name;
        var space = new ArenaSpace();
        space.Add(data.Arenas.Find(a => a.Name == name)!);
        space.SetSolid([name]);
        return space;
    }

    private static Kurt.Kurt KurtIn(ArenaSpace space, Vector3 feet, float yaw = 0f) =>
        new(space, new SoundMixer(Device, _ => null), s => s switch { State.Hang => HangFrames, State.HardLand => CrashFrames, _ => Frames }) { Feet = feet, Yaw = yaw };

    private static void Run(Kurt.Kurt kurt, Input input, float seconds, FollowCamera? camera = null)
    {
        for (var t = 0f; t < seconds; t += Step)
        {
            kurt.Update(input, Step);
            camera?.Update(kurt, 4f, input, Step);
        }
    }

    private static Input Holding(params Key[] keys)
    {
        var input = new Input();
        foreach (var key in keys)
        {
            input.Hold(key, Input.State.Down);
        }

        return input;
    }

    /// <summary>Runs until Kurt is in <paramref name="state"/> (at most <paramref name="seconds"/>).</summary>
    private static bool RunUntil(Kurt.Kurt kurt, Input input, State state, float seconds)
    {
        for (var t = 0f; t < seconds && kurt.Current != state; t += Step)
        {
            kurt.Update(input, Step);
        }

        return kurt.Current == state;
    }

    [DataFact]
    public void KurtGrabsTheLedgeAndClimbsIt()
    {
        var kurt = KurtIn(SpaceOf(3), BelowLedge);
        Run(kurt, new Input(), 0.5f);
        var input = Holding(Key.Forward, Key.Jump);

        Assert.True(RunUntil(kurt, input, State.Hang, 3f));

        // He hangs 4.6 below the edge, facing it (its direction − 90°), 1 unit before it.
        Assert.Equal(15f, kurt.Yaw, 0.5f);
        Assert.Equal(82.13f, kurt.Feet.X, 0.05f);
        Assert.Equal(0f, kurt.VerticalSpeed);
        Assert.False(kurt.ChuteOpen);

        // Climbing: 2 × (6.406 − 0.374) × 0.354 up and 2 × (0.685 + 1.293) × 0.354 on, in 31 ticks.
        var hang = kurt.Feet;
        var facing = kurt.Facing;
        Run(kurt, input, 0.5f);
        Assert.Equal(State.Hang, kurt.Current);
        Assert.Equal(7f, kurt.AnimationFrame);
        Assert.True(RunUntil(kurt, input, State.Still, 1f));
        Assert.Equal(hang.Z + 4.2706f, kurt.Feet.Z, 0.01f);
        Assert.Equal(1.4011f, Vector3.Dot(kurt.Feet - hang, facing), 0.01f);

        // On top he runs on.
        Run(kurt, input, 0.5f);
        Assert.True(kurt.OnFloor);
        Assert.InRange(kurt.Feet.Z, 145f, 146.5f);
    }

    /// <summary>Issue #30: level 8's walkway (GUNT_7, top at z −228) is a wedge with no side wall at
    /// its inner edge. Falling short of it, Kurt's box goes in from the side; the original keeps him
    /// 2 below its top. He climbs onto it instead.</summary>
    [DataFact]
    public void KurtDoesNotStayInsideTheWalkway()
    {
        var kurt = KurtIn(SpaceOf(8, "GUNT_7"), new Vector3(60f, 2774f, -220f), 90f);
        Run(kurt, Holding(Key.Forward), 1f);
        Run(kurt, new Input(), 0.5f);

        Assert.True(kurt.OnFloor);
        Assert.Equal(-228f, kurt.Feet.Z, 0.05f);
    }

    [DataFact]
    public void NoLedgeWithoutForward()
    {
        var kurt = KurtIn(SpaceOf(3), BelowLedge);
        Run(kurt, new Input(), 0.5f);

        Assert.False(RunUntil(kurt, Holding(Key.Jump), State.Hang, 3f));
    }

    [Fact]
    public void SlideAccelSquaresThePush()
    {
        var kurt = KurtIn(new ArenaSpace(), Vector3.Zero);
        kurt.SlideAccel(new Vector2(10f, -10f), Tick);
        Assert.Equal(100f / 30f, kurt.SlideVelocity.X, 4);
        Assert.Equal(-100f / 30f, kurt.SlideVelocity.Y, 4);

        // Within ±0.1 it brakes by 2 u/s².
        kurt.SlideAccel(new Vector2(0.05f, 0f), Tick);
        Assert.Equal(100f / 30f - 2f / 30f, kurt.SlideVelocity.X, 4);
        Assert.Equal(-100f / 30f + 2f / 30f, kurt.SlideVelocity.Y, 4);
    }

    /// <summary>Level 6's first wind zone (COLYM_1, wind_zone [1, 90, 10]): its box holds Kurt.</summary>
    private static readonly Vector3 WindMin = new(-1206f, -811f, -115f);
    private static readonly Vector3 WindMax = new(-1096f, -764f, -35f);
    private const float WindYaw = 90f;
    private const float WindSpeed = 10f;

    [DataFact]
    public void TheWindSlidesKurtDownTheTunnel()
    {
        var kurt = KurtIn(SpaceOf(6, "COLYM_1"), new Vector3(-1150f, -790f, -85f), WindYaw);
        var input = new Input();
        for (var t = 0f; t < 4f; t += Step)
        {
            // The script's tick (30 a second), as ScriptRuntime.WindZone.
            var p = kurt.Feet;
            if ((int)(t / Step) % 2 == 0 && p.X >= WindMin.X && p.Y >= WindMin.Y && p.Z >= WindMin.Z && p.X <= WindMax.X && p.Y <= WindMax.Y
                && p.Z <= WindMax.Z && (kurt.Sliding || kurt.OnFloor))
            {
                kurt.StartSlide();
                kurt.Yaw = WindYaw;
                kurt.SlideAccel(new Vector2(0f, WindSpeed), Tick);
            }

            kurt.Update(input, Step);
            Assert.True(kurt.SlideVelocity.Length() <= 50f + 1e-3f);
        }

        Assert.True(kurt.Sliding);
        Assert.Equal(State.Slide, kurt.Current);

        // On the Godot port's path: (-1138, -599, -185) to (-1138, -498, -241).
        var f = kurt.Feet;
        Assert.InRange(f.X, -1145f, -1135f);
        Assert.InRange(f.Y, -640f, -560f);
        Assert.Equal(-185f - 56f / 101f * (f.Y + 599f), f.Z, 3f);
    }

    /// <summary>Level 6's COLYM_3 wind zone (wind_zone [1, 180, 10]), at the top of the spiral under
    /// OLYM_3's hatch.</summary>
    private static readonly Vector3 HatchWindMin = new(-1738f, 179f, -745f);
    private static readonly Vector3 HatchWindMax = new(-1656f, 246f, -680f);
    private const float HatchWindYaw = 180f;

    /// <summary>Issue #20: dropping through the hatch with the chute open, Kurt glided past the wind
    /// box and walked down the spiral. In the box the wind keeps the chute shut (0x573bec, damp_vertical
    /// 0x4694bc): he lands in it and slides.</summary>
    [DataFact]
    public void TheWindShutsTheChute()
    {
        var kurt = KurtIn(SpaceOf(6, "COLYM_3"), new Vector3(-1662f, 212f, -684f), HatchWindYaw);
        var input = Holding(Key.Jump, Key.Forward);
        for (var t = 0f; t < 3f; t += Step)
        {
            // The script's tick (30 a second), as ScriptRuntime.WindZone.
            var p = kurt.Feet;
            var inBox = p.X >= HatchWindMin.X && p.Y >= HatchWindMin.Y && p.Z >= HatchWindMin.Z && p.X <= HatchWindMax.X
                && p.Y <= HatchWindMax.Y && p.Z <= HatchWindMax.Z;
            if ((int)(t / Step) % 2 == 0 && inBox && (kurt.Sliding || kurt.OnFloor))
            {
                kurt.StartSlide();
                kurt.Yaw = HatchWindYaw;
                kurt.SlideAccel(new Vector2(-WindSpeed, 0f), Tick);
            }
            else if ((int)(t / Step) % 2 == 0 && inBox)
            {
                kurt.Blow(Tick);
            }

            kurt.Update(input, Step);
        }

        Assert.True(kurt.Sliding, $"not sliding at {kurt.Feet}, {kurt.Current}");
    }

    [DataFact]
    public void ForwardSpeedsTheSlideUpAndBackBrakesIt()
    {
        var kurt = KurtIn(SpaceOf(6, "COLYM_1"), new Vector3(-1150f, -790f, -85f), WindYaw);
        Run(kurt, new Input(), 0.5f);
        kurt.StartSlide();
        kurt.SlideAccel(new Vector2(0f, 20f), 1f);
        Assert.Equal(State.Slip, kurt.Current);

        Run(kurt, Holding(Key.Forward), 1f);
        Assert.Equal(State.SlideFast, kurt.Current);
        Assert.True(kurt.SlideVelocity.Length() > 50f);

        Run(kurt, Holding(Key.Back), 0.2f);
        Assert.Equal(State.SlideBrake, kurt.Current);
    }

    /// <summary>Level 3: a flat floor (the pad slopes a little: Kurt slides off it).</summary>
    private static readonly Vector3 FlatFloor = new(-86f, 334f, 144f);

    [DataFact]
    public void AtRestKurtGetsUp()
    {
        var kurt = KurtIn(SpaceOf(3), FlatFloor);
        Run(kurt, new Input(), 1f);
        kurt.StartSlide();
        Assert.True(kurt.Sliding);

        // A flat floor gives no push and he has no speed: he gets up from K_BFLIP.
        kurt.Update(new Input(), Step);
        Assert.False(kurt.Sliding);
        Assert.True(kurt.Knocked);
        Assert.True(RunUntil(kurt, new Input(), State.Still, 2f));
    }

    /// <summary>LEVEL6: the slide drops Kurt into OLYM_2 against a wall; still moving, he lay there
    /// until friction stopped him. A new arena ends the slide 15 ticks later (0x573be8 = −15).</summary>
    [DataFact]
    public void ANewArenaEndsTheSlide()
    {
        var kurt = KurtIn(SpaceOf(3), FlatFloor);
        Run(kurt, new Input(), 1f);
        kurt.StartSlide();
        kurt.SlideAccel(new Vector2(0f, 30f), 1f);
        kurt.EnterArena();

        Run(kurt, new Input(), 0.45f);
        Assert.True(kurt.Sliding);

        Run(kurt, new Input(), 0.1f);
        Assert.False(kurt.Sliding);
        Assert.Equal(Vector2.Zero, kurt.SlideVelocity);
        Assert.True(RunUntil(kurt, new Input(), State.Still, 2f));
    }

    /// <summary>The mouse steers the slide as it turns Kurt walking, at most 4 × 45°/s (damp_control
    /// clamps it to ±4 before 0x5014d4).</summary>
    [DataTheory]
    [InlineData(10f, -1.5f)]
    [InlineData(1000f, -3f)]
    public void TheMouseSteersTheSlide(float mouse, float yaw)
    {
        var kurt = KurtIn(SpaceOf(3), FlatFloor);
        Run(kurt, new Input(), 1f);
        kurt.StartSlide();
        kurt.SlideAccel(new Vector2(30f, 0f), 1f);
        var input = new Input();
        input.AddMouseMotion(mouse, 0f);

        kurt.Update(input, Step);
        Assert.Equal(yaw, kurt.Yaw, 3);
    }

    [Fact]
    public void TwentyTicksInTheAirEndTheSlide()
    {
        var kurt = KurtIn(new ArenaSpace(), new Vector3(0f, 0f, 100f));
        kurt.StartSlide();
        Run(kurt, new Input(), 0.6f);
        Assert.True(kurt.Sliding);

        Run(kurt, new Input(), 0.1f);
        Assert.False(kurt.Sliding);
        Assert.Equal(State.Fall, kurt.Current);
    }

    [DataTheory]
    [InlineData(Difficulty.Normal, 300f, 90, State.HardLand)]
    [InlineData(Difficulty.Easy, 300f, 94, State.HardLand)]
    [InlineData(Difficulty.Normal, 250f, 100, State.Land)]
    public void LandingFasterThan100Hurts(Difficulty difficulty, float height, int health, State state)
    {
        var kurt = KurtIn(SpaceOf(3), Pad with { Z = height });
        kurt.Inventory.Difficulty = difficulty;
        Assert.True(RunUntil(kurt, new Input(), state, 5f));
        Assert.Equal(health, kurt.Health);
        Assert.Equal(0f, kurt.KnockDamage);
    }

    [DataFact]
    public void AfterAHardLandingKurtCantMoveForAWhile()
    {
        var kurt = KurtIn(SpaceOf(3), Pad with { Z = 300f });
        Assert.True(RunUntil(kurt, new Input(), State.HardLand, 5f));
        var landed = kurt.Feet;

        // K_CRASHL: 26 frames at 2 ticks each.
        Run(kurt, Holding(Key.Forward, Key.Fire), 1.5f);
        Assert.Equal(State.HardLand, kurt.Current);
        Assert.Equal(landed, kurt.Feet);
        Assert.False(kurt.Firing);
        Assert.True(RunUntil(kurt, new Input(), State.Still, 0.5f));
    }

    [DataFact]
    public void Falling50BelowTheArenaKills()
    {
        var space = SpaceOf(3);
        var bottom = space.Bottom(Pad)!.Value;
        var kurt = KurtIn(space, Pad with { Z = bottom - 49f });
        var died = false;
        kurt.Died += () => died = true;

        Assert.True(RunUntil(kurt, new Input(), State.Dead, 2f));
        Assert.Equal(0, kurt.Health);
        Assert.True(kurt.Feet.Z <= bottom - 50f);
        Run(kurt, new Input(), 5f);
        Assert.True(died);
    }

    /// <summary>Playtest: off LEVEL6's lift (OLYM_8, start of XTR_1000) Kurt falls past the floating
    /// lamps; the arena's bottom is z −2900 (as in the original), so he dies after about 7 s.</summary>
    [DataFact]
    public void FallingOffTheLevel6LiftKills()
    {
        var space = SpaceOf(6, "OLYM_8");
        var kurt = KurtIn(space, new Vector3(-1658f, 3406f, -1866f));

        Assert.True(RunUntil(kurt, new Input(), State.Dead, 8f));
        Assert.True(kurt.Feet.Z <= space.Bottom(kurt.Feet)!.Value - 50f);
    }

    [Fact]
    public void RunningAndTurningRollsTheView()
    {
        // 0.25° a tick up to 10°, the other way first jumps 2° back.
        var roll = new CameraRoll();
        for (var i = 0; i < 4; i++)
        {
            roll.Walk(1f, 1f, Tick);
            roll.Settle(Tick);
        }

        Assert.Equal(1f, roll.Roll, 4);
        for (var i = 0; i < 100; i++)
        {
            roll.Walk(1f, 1f, Tick);
            roll.Settle(Tick);
        }

        Assert.Equal(10f, roll.Roll, 4);
        roll.Walk(-1f, 1f, Tick);
        Assert.Equal(7.75f, roll.Roll, 4);

        // Without input it levels out by 0.35·|roll| a tick (at most 2.5, at least 0.05).
        roll.Settle(Tick);
        roll.Settle(Tick);
        Assert.Equal(5.25f, roll.Roll, 4);
        for (var i = 0; i < 100; i++)
        {
            roll.Settle(Tick);
        }

        Assert.Equal(0f, roll.Roll);

        // Turning on the spot doesn't roll.
        roll.Walk(1f, 0f, Tick);
        roll.Settle(Tick);
        Assert.Equal(0f, roll.Roll);
    }

    [Fact]
    public void SlidingAndTheBoardRollTheView()
    {
        // Slope 30° across the way (normal (0.5, 0, 0.866), facing +y): towards 30°, a tenth a tick.
        var roll = new CameraRoll();
        roll.Slide(new Vector3(0.5f, 0f, MathF.Sqrt(0.75f)), Vector2.UnitY, Tick);
        Assert.Equal(3f, roll.Roll, 3);

        // The board's bank at 45°/s.
        roll = new CameraRoll();
        roll.Follow(10f, Tick);
        roll.Settle(Tick);
        Assert.Equal(1.5f, roll.Roll, 4);
    }

    [Fact]
    public void KurtRollsTheCameraTurningRight()
    {
        var kurt = KurtIn(new ArenaSpace(), new Vector3(0f, 0f, 100f), 90f);
        var camera = new FollowCamera();
        Run(kurt, Holding(Key.Forward, Key.TurnRight), 1f, camera);
        Assert.Equal(7.5f, kurt.CameraRoll.Roll, 0.2f);

        // The camera's up turns towards its right.
        var level = FollowCamera.UpOf(camera.Forward);
        var right = Vector3.Normalize(Vector3.Cross(camera.Forward, level));
        Assert.Equal(MathF.Sin(float.DegreesToRadians(kurt.CameraRoll.Roll)), Vector3.Dot(camera.Up, right), 3);
    }

    /// <summary>Level 7: Kurt with his back 2 units from the corridor's wall (x 14).</summary>
    private static readonly Vector3 BackToWall = new(12f, 100f, 12f);
    private const float FacingWest = 180f;

    [DataFact]
    public void AWallBehindKurtPushesHim()
    {
        var space = SpaceOf(7);
        var kurt = KurtIn(space, BackToWall, FacingWest);
        Run(kurt, new Input(), 1.5f, new FollowCamera(space));

        // The Godot port pushes him to x 7.0 (its ray misses a lower part of the wall the box meets).
        Assert.InRange(kurt.Feet.X, 5f, 7.5f);
    }

    [DataFact]
    public void WithoutClearanceTheWallDoesntPush()
    {
        var kurt = KurtIn(SpaceOf(7), BackToWall, FacingWest);
        Run(kurt, new Input(), 1.5f, new FollowCamera());
        Assert.Equal(BackToWall.X, kurt.Feet.X, 2);
    }

    [DataFact]
    public void AnObjectInTheViewPushesKurtForwardGradually()
    {
        var space = SpaceOf(3);
        var kurt = KurtIn(space, Pad, FacingWest);
        Run(kurt, new Input(), 1f);

        // An object's face 4 units behind him (the camera is 8 behind).
        var start = kurt.Feet.X;
        var face = start + 4f;
        var camera = new FollowCamera(space)
        {
            ClipView = (from, to) => to.X <= face ? to : Vector3.Lerp(from, to, (face - from.X) / (to.X - from.X)),
        };
        Run(kurt, new Input(), Step, camera);

        // One step pushes him only part of the way (at most 40 u/s): a door behind him doesn't
        // "fast-forward" him.
        Assert.InRange(start - kurt.Feet.X, 0.01f, 40f * Step + 0.01f);

        // A few steps on (4 cut off at 40 u/s), he has gone forward by all of it, the camera with him.
        for (var i = 0; i < 5; i++)
        {
            Run(kurt, new Input(), Step, camera);
        }

        Assert.Equal(face, camera.Position.X, 0.01f);
        Assert.Equal(face - 7.97f, kurt.Feet.X, 0.05f);
    }

    [DataFact]
    public void TheShakeTurnsTheViewThenDrains()
    {
        var kurt = KurtIn(SpaceOf(3), Pad);
        var camera = new FollowCamera();
        Run(kurt, new Input(), 1f, camera);
        var steady = camera.Forward;

        // Shake 5: up to ±1.64 × 5 pixels; it drains by 0.25 a tick.
        camera.RaiseShake(5f);
        var shaken = 0;
        for (var t = 0f; t < 0.5f; t += Step)
        {
            kurt.Update(new Input(), Step);
            camera.Update(kurt, 4f, new Input(), Step);
            Assert.InRange(MathF.Abs(camera.ShakeOffset.X), 0f, 8f);
            Assert.InRange(MathF.Abs(camera.ShakeOffset.Y), 0f, 8f);
            shaken += camera.ShakeOffset != Vector2.Zero && camera.Forward != steady ? 1 : 0;
        }

        Assert.True(shaken > 0);
        Run(kurt, new Input(), 0.3f, camera);
        Assert.Equal(Vector2.Zero, camera.ShakeOffset);
        Assert.Equal(steady, camera.Forward);
    }

    [Fact]
    public void FiringOnTheBoardShowsTheMuzzleFlash()
    {
        var kurt = KurtIn(new ArenaSpace(), Vector3.Zero);
        kurt.Ride = (_, _) => { };
        kurt.Pose = ("K_SURF", 0);
        var input = Holding(Key.Fire);
        Kurt.Kurt.MuzzleFlash? seen = null;
        for (var i = 0; i < 8; i++)
        {
            kurt.UpdateGun(input, Step);
            seen ??= kurt.Muzzle;
        }

        // At (−42, 12) plus 0-4 pixels (damp_animate, state 201).
        Assert.NotNull(seen);
        Assert.InRange(seen.Value.X, -42, -38);
        Assert.InRange(seen.Value.Y, 12, 16);
    }
}
