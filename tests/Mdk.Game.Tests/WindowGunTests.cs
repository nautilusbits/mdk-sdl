using System.Numerics;
using Mdk.Engine.Audio;
using Mdk.Formats;
using Mdk.Game.Audio;
using Mdk.Game.Collision;
using Mdk.Game.Level;
using Mdk.Game.Scripts;

namespace Mdk.Game.Tests;

/// <summary>Issue #36: LEVEL7 DANT_6's cannon XD6GUN blew up as soon as Kurt came near. Kurt in
/// (383..667, 3822..4215) makes the arena spawn four XG in the windows, each counting itself in
/// var 1.0; the cannon dies once that count is 0. The original puts a new object at the head of
/// its arena's list (0x45fd4c), so the XG count themselves before the cannon looks.</summary>
public class WindowGunTests
{
    private const int Level = 7;
    private const string Arena = "DANT_6";
    private const string Gun = "XD6GUN";
    private const string Alien = "XG";
    private const int Aliens = 4;
    private const float Tick = 1f / 30f;
    /// <summary>In the arena's trigger rect, before the building.</summary>
    private static readonly Vector3 Stand = new(450f, 3900f, -89f);
    private const int Ticks = 90;

    private static readonly Lazy<MdkData> Data = new(() => MdkData.Find()!);
    private static readonly AudioDevice Device = new(Output.Muted);

    private static ScriptRuntime CreateRuntime()
    {
        var level = new LevelData(Data.Value, Level);
        var cmi = Cmi.Load(Data.Value.PathOf($"TRAVERSE/LEVEL{Level}/LEVEL{Level}.CMI"));
        var sprites = Bni.Load(Data.Value.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        var space = new ArenaSpace();
        var groups = new TriangleGroups();
        foreach (var arena in level.Arenas.Where(a => level.IsReachable(a.Name)))
        {
            space.Add(arena);
            groups.Add(arena);
        }

        var mixer = new SoundMixer(Device, _ => null);
        return new ScriptRuntime(level, cmi, sprites, space, groups, mixer, new Kurt.Kurt(space, mixer, _ => 1));
    }

    [DataFact]
    public void GunOutlivesTheAliensArrival()
    {
        var runtime = CreateRuntime();
        runtime.TeleportKurt(Arena, Stand, 0f);
        for (var i = 0; i < Ticks; i++)
        {
            runtime.Update(Tick);
        }

        Assert.Equal(Aliens, runtime.Objects.Count(o => o.TypeName == Alien && o.Arena == Arena && !o.Dead));
        Assert.NotNull(runtime.FindObjectNamed(Gun));
    }
}
