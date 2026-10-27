using System.Numerics;
using Mdk.Engine.Audio;
using Mdk.Engine.Render;
using Mdk.Formats;
using Mdk.Game.Audio;
using Mdk.Game.Collision;
using Mdk.Game.Level;
using Mdk.Game.Scripts;

namespace Mdk.Game.Tests;

/// <summary>Found by playtest: at LEVEL4's snowboard ride end (CMEAT_1 by the tube to MEAT_3) Kurt
/// looked half sunk: MEAT_3's flat shell lies above CMEAT_1's floor and won the depth test. The
/// original has no depth buffer: it draws the other arena first and the camera's over it (0x41df35),
/// so the camera's arena covers the other one.</summary>
public class DepthBandTests
{
    private const int Level = 4;
    private const string Tube = "CMEAT_1";
    private const string Shell = "MEAT_3";
    private static readonly Vector3 Feet = new(62.35f, 2582.81f, -392.90f);
    /// <summary>The follow camera behind Kurt there (--trace).</summary>
    private static readonly Vector3 Camera = new(62.35f, 2574.84f, -387.84f);

    private const string Room = "MEAT_7";
    private const string Corridor = "CMEAT_7";
    private const float Doorway = 14822f;
    private const float Floor = 21f;

    private static readonly MdkData Data = MdkData.Find() ?? throw new InvalidOperationException("MDK data not found");
    private static readonly AudioDevice Device = new(Output.Muted);

    private static ScriptRuntime CreateRuntime()
    {
        var space = new ArenaSpace();
        var level = new LevelData(Data, Level);
        var cmi = Cmi.Load(Data.PathOf($"TRAVERSE/LEVEL{Level}/LEVEL{Level}.CMI"));
        var sprites = Bni.Load(Data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        foreach (var arena in level.Arenas.Where(a => level.IsReachable(a.Name)))
        {
            space.Add(arena);
        }

        var mixer = new SoundMixer(Device, _ => null);
        return new ScriptRuntime(level, cmi, sprites, space, new TriangleGroups(), mixer, new Kurt.Kurt(space, mixer, _ => 1));
    }

    [Fact]
    public void FrontIsNearerThanBack()
    {
        var front = DepthBands.Of(DepthBand.Front);
        var back = DepthBands.Of(DepthBand.Back);
        Assert.Equal(0f, front.Min);
        Assert.True(front.Max <= back.Min);
        Assert.Equal(1f, back.Max);
    }

    [Fact]
    public void CameraArenaIsInFront()
    {
        Assert.Equal(DepthBand.Front, ArenaBand.Of(Tube, Tube));
        Assert.Equal(DepthBand.Back, ArenaBand.Of(Shell, Tube));
    }

    [DataFact]
    public void TubeCoversTheShell()
    {
        var runtime = CreateRuntime();
        runtime.TeleportKurt(Tube, Feet, 90f);
        runtime.Update(ScriptRuntime.Tick);

        Assert.Contains(Shell, runtime.DrawnArenas);
        Assert.Equal(Tube, runtime.CameraArena(Camera));
    }

    /// <summary>Kurt just through a doorway, the camera still behind it: the arena left is the
    /// camera's (0x490db4), drawn over Kurt's.</summary>
    [DataFact]
    public void CameraBehindTheDoorway()
    {
        var runtime = CreateRuntime();
        runtime.TeleportKurt(Room, new Vector3(0f, Doorway - 100f, Floor), 90f);
        runtime.Update(ScriptRuntime.Tick);
        runtime.Kurt.Teleport(new Vector3(0f, Doorway + 3f, Floor), runtime.Kurt.Yaw);
        runtime.Update(ScriptRuntime.Tick);
        Assert.Equal(Corridor, runtime.CurrentArena);

        Assert.Equal(Room, runtime.CameraArena(new Vector3(0f, Doorway - 5f, Floor + 5f)));
        Assert.Equal(Corridor, runtime.CameraArena(new Vector3(0f, Doorway + 1f, Floor + 5f)));
    }
}
