using System.Numerics;
using Mdk.Engine.Audio;
using Mdk.Engine.Platform;
using Mdk.Formats;
using Mdk.Game.Audio;
using Mdk.Game.Collision;
using Mdk.Game.Objects;
using Mdk.Game.Scripts;

namespace Mdk.Game.Tests;

/// <summary>Level 6's OLYM_4 gun platforms (XBGUN): one keeps Kurt (set_targetable 2: flag
/// 0x800000); fired from, it flies him into COLYM_4. Kept, his XY is the platform's each step, in
/// the air too (0x46634e), so a jump on it doesn't leave him behind (issue #40).</summary>
public class GunPlatformTests
{
    private const int Level = 6;
    private const string Arena = "OLYM_4";
    private const string Gun = "XBGUN";
    /// <summary>The keeping gun of the row (x −1732).</summary>
    private const float KeeperX = -1732f;
    private const float KeeperReach = 10f;
    private static readonly Vector3 Above = new(0f, 0f, 12f);
    private const int SettleSteps = 60;
    /// <summary>Firing this long, the gun is on its way.</summary>
    private const int FireSteps = 150;
    private const int JumpSteps = 60;
    private const float Tolerance = 0.01f;

    private static readonly Lazy<MdkData> Data = new(() => MdkData.Find()!);
    private static readonly AudioDevice Device = new(Output.Muted);

    [DataFact]
    public void AJumpOnTheFlyingGunStaysOverIt()
    {
        var level = new Level.LevelData(Data.Value, Level);
        var cmi = Cmi.Load(Data.Value.PathOf($"TRAVERSE/LEVEL{Level}/LEVEL{Level}.CMI"));
        var sprites = Bni.Load(Data.Value.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        var space = new ArenaSpace();
        var groups = new Level.TriangleGroups();
        foreach (var arena in level.Arenas.Where(a => level.IsReachable(a.Name)))
        {
            space.Add(arena);
            groups.Add(arena);
        }

        var kurt = new Kurt.Kurt(space, new SoundMixer(Device, _ => null), _ => 1);
        var runtime = new ScriptRuntime(level, cmi, sprites, space, groups, new SoundMixer(Device, _ => null), kurt, 1);
        var input = new Input();
        void Step(int steps)
        {
            for (var i = 0; i < steps; i++)
            {
                kurt.Update(input, Viewer.Step);
                runtime.Update(Viewer.Step);
                space.SetSolid(runtime.SolidArenas);
            }
        }

        runtime.TeleportKurt(Arena, Vector3.Zero, 0f);
        Step(2);
        var gun = runtime.Objects.First(o => o.TypeName == Gun && MathF.Abs(o.Position.X - KeeperX) < KeeperReach);
        runtime.TeleportKurt(Arena, gun.Position + Above, 0f);
        Step(SettleSteps);
        input.Hold(Key.Fire, Input.State.Down);
        Step(FireSteps);

        // A jump while it flies: Kurt is over the gun every step.
        input.Hold(Key.Jump, Input.State.Down);
        var airborne = 0;
        for (var i = 0; i < JumpSteps && (gun.Flags & MdkObject.FlagStandable) != 0; i++)
        {
            Step(1);
            input.Hold(Key.Jump, Input.State.Up);
            airborne += kurt.OnFloor ? 0 : 1;
            Assert.Equal(gun.Position.X, kurt.Feet.X, Tolerance);
            Assert.Equal(gun.Position.Y, kurt.Feet.Y, Tolerance);
        }

        Assert.True(airborne > 0);
    }
}
