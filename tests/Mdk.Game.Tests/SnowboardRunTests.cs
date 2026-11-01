using System.Numerics;
using Mdk.Engine.Audio;
using Mdk.Engine.Platform;
using Mdk.Formats;
using Mdk.Game.Audio;
using Mdk.Game.Collision;
using Mdk.Game.Level;
using Mdk.Game.Scripts;

namespace Mdk.Game.Tests;

/// <summary>Found by playtest: LEVEL4's first board run (MEAT_1 → CMEAT_1 → MEAT_3) never ended. Its
/// script waits in MEAT_1 for Kurt in a box of MEAT_3; the board stayed in MEAT_1, frozen once Kurt
/// left CMEAT_1. The original moves objects with flag 0x80000 (the ridden board) into the arena whose
/// connection they cross (0x45e810, 0x43ca00).</summary>
public class SnowboardRunTests
{
    private const int Level = 4;
    private const float Step = 1f / 60f;
    private const int StepsPerSecond = 60;
    private const string Start = "MEAT_1";
    private const string End = "MEAT_3";
    /// <summary>Kurt lands on group 1 of MEAT_1 (its hit script spawns the board and XS); XS's death
    /// makes the board rideable.</summary>
    private const int LandingGroup = 1;
    private const string BoardType = "XSNOWB";
    private static readonly Vector3 Landing = new(0f, 0f, 80f);
    private static readonly Vector3 AboveBoard = new(0f, 0f, 3f);
    private const float Yaw = 90f;
    /// <summary>The run takes about 45 s; the script lets him off in MEAT_3 (y −2674…3176).</summary>
    private const int RunSeconds = 60;

    private static readonly MdkData Data = MdkData.Find() ?? throw new InvalidOperationException("MDK data not found");
    private static readonly AudioDevice Device = new(Output.Muted);

    /// <summary>The second run's corridor, near its board (first key 420, 4285).</summary>
    private const string SecondRun = "CMEAT_3";
    private static readonly Vector3 SecondBoard = new(420f, 4280f, -578f);
    private const int RideSeconds = 5;
    private const string SecondEnd = "MEAT_5";
    /// <summary>Without keys the ride reaches MEAT_5 after about 145 s.</summary>
    private const int SecondRunSeconds = 180;
    /// <summary>The third run, from MEAT_7 (its board at 0, 14803): its script lets Kurt off in MEAT_10, y ≥ 23970.</summary>
    private const string ThirdRun = "MEAT_7";
    private static readonly Vector3 ThirdStart = new(0f, 14700f, 40f);
    private const int ThirdRunSeconds = 240;
    private const string ThirdEnd = "MEAT_10";
    private const float ThirdEndY = 23970f;
    /// <summary>The scripts' random numbers: with some, MEAT_4 stops a rider without keys.</summary>
    private const int Seed = 4;

    private static (ScriptRuntime Runtime, ArenaSpace Space) CreateRuntime() => CreateRuntime(out _);

    private static (ScriptRuntime Runtime, ArenaSpace Space) CreateRuntime(out TriangleGroups groups)
    {
        var level = new LevelData(Data, Level);
        var cmi = Cmi.Load(Data.PathOf($"TRAVERSE/LEVEL{Level}/LEVEL{Level}.CMI"));
        var sprites = Bni.Load(Data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        var space = new ArenaSpace();
        groups = new TriangleGroups();
        foreach (var arena in level.Arenas.Where(a => level.IsReachable(a.Name)))
        {
            space.Add(arena);
            groups.Add(arena);
        }

        var mixer = new SoundMixer(Device, _ => null);
        var runtime = new ScriptRuntime(level, cmi, sprites, space, groups, mixer, new Kurt.Kurt(space, mixer, _ => 1), Seed);
        return (runtime, space);
    }

    /// <summary>Kurt and the scripts for up to that many steps, as the game runs them.</summary>
    private static void Run(ScriptRuntime runtime, ArenaSpace space, int steps, Func<bool>? until = null)
    {
        var input = new Input();
        for (var i = 0; i < steps && until?.Invoke() != true; i++)
        {
            runtime.Kurt.Update(input, Step);
            runtime.Update(Step);
            space.SetSolid(runtime.SolidArenas);
        }
    }

    [DataFact]
    public void FirstRunEndsInMeat3()
    {
        var (runtime, space) = CreateRuntime();
        runtime.TeleportKurt(Start, Landing, Yaw);
        Run(runtime, space, StepsPerSecond / 2);
        runtime.HitGroup(Start, LandingGroup, 0, ScriptRuntime.HitKurt, ScriptRuntime.HitTypeKurt);
        Run(runtime, space, StepsPerSecond);
        runtime.Kill(runtime.FindObjectNamed("XS")!);
        Run(runtime, space, StepsPerSecond);

        // Onto the board.
        var board = runtime.FindObjectNamed(BoardType)!;
        runtime.TeleportKurt(Start, board.Position + AboveBoard, Yaw);
        Run(runtime, space, StepsPerSecond, () => runtime.Rides.OnBoard());
        Assert.True(runtime.Rides.OnBoard());

        // Down the run, without keys: the script lets him off in MEAT_3.
        Run(runtime, space, RunSeconds * StepsPerSecond, () => !runtime.Rides.OnBoard());

        Assert.False(runtime.Rides.OnBoard());
        Assert.Equal(End, runtime.CurrentArena);
        Assert.Equal(End, board.Arena);
    }

    /// <summary>Found by playtest: on the second run (CMEAT_3) the slope vanished. Its board runs
    /// group_state_near_player 2, 10, 31, 1, 2: the original (0x453a1e) gives the groups near Kurt
    /// the opposite op (shown) and the others the op (hidden), not the reverse.</summary>
    [DataFact]
    public void SecondRunShowsSlopeUnderKurt()
    {
        var (runtime, space) = CreateRuntime(out var groups);
        runtime.TeleportKurt(SecondRun, SecondBoard, Yaw);
        Run(runtime, space, StepsPerSecond);
        var board = runtime.FindObjectNamed(BoardType)!;
        runtime.TeleportKurt(SecondRun, board.Position + AboveBoard, Yaw);
        Run(runtime, space, StepsPerSecond, () => runtime.Rides.OnBoard());
        Assert.True(runtime.Rides.OnBoard());

        Run(runtime, space, RideSeconds * StepsPerSecond);

        var floor = runtime.GetKurtFloorGroup();
        Assert.NotEqual(0, floor);
        Assert.Equal(SecondRun, runtime.CurrentArena);
        Assert.False(groups.Get(SecondRun, floor)!.State.HasFlag(TriangleGroups.State.Hidden));
    }

    /// <summary>Found by playtest: the second run didn't end in MEAT_5. Its board's script lets Kurt off
    /// in a box there, but the board stayed in CMEAT_4: MEAT_5's arena_show NONE dropped CMEAT_4
    /// before the board's tick. The original runs the arenas' scripts after the objects.</summary>
    [DataFact]
    public void SecondRunEndsInMeat5()
    {
        var (runtime, space) = CreateRuntime();
        runtime.TeleportKurt(SecondRun, SecondBoard, Yaw);
        Run(runtime, space, StepsPerSecond);
        var board = runtime.FindObjectNamed(BoardType)!;
        runtime.TeleportKurt(SecondRun, board.Position + AboveBoard, Yaw);
        Run(runtime, space, StepsPerSecond, () => runtime.Rides.OnBoard());
        Assert.True(runtime.Rides.OnBoard());

        // Down CMEAT_3, MEAT_4 and CMEAT_4, without keys: the script lets him off in MEAT_5.
        Run(runtime, space, SecondRunSeconds * StepsPerSecond, () => !runtime.Rides.OnBoard());

        Assert.False(runtime.Rides.OnBoard());
        Assert.Equal(SecondEnd, runtime.CurrentArena);
        Assert.Equal(SecondEnd, board.Arena);
    }

    [DataFact]
    public void ThirdRunEnds()
    {
        var (runtime, space) = CreateRuntime();
        runtime.TeleportKurt(ThirdRun, ThirdStart, Yaw);
        Run(runtime, space, StepsPerSecond);
        var board = runtime.Objects.First(o => !o.Dead && o.TypeName == BoardType && o.Arena == ThirdRun);
        runtime.TeleportKurt(ThirdRun, board.Position + AboveBoard, Yaw);
        Run(runtime, space, StepsPerSecond, () => runtime.Rides.OnBoard());
        Assert.True(runtime.Rides.OnBoard());

        // Down CMEAT_7, MEAT_8 and CMEAT_8 (god: its aliens shoot): the script lets him off at the end.
        runtime.Kurt.Mortality = Kurt.Mortality.God;
        Run(runtime, space, ThirdRunSeconds * StepsPerSecond, () => !runtime.Rides.OnBoard());

        Assert.False(runtime.Rides.OnBoard());
        Assert.Equal(ThirdEnd, runtime.CurrentArena);
        Assert.InRange(runtime.Kurt.Feet.Y, ThirdEndY, float.MaxValue);
    }
}
