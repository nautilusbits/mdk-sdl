using System.Numerics;
using Mdk.Engine.Audio;
using Mdk.Formats;
using Mdk.Game.Audio;
using Mdk.Game.Collision;
using Mdk.Game.Level;
using Mdk.Game.Scripts;

namespace Mdk.Game.Tests;

/// <summary>LEVEL7's flying ships (XE) carry a bomb (XBANG) or soldiers (XG) on a chain (spawn_chain).
/// The ship has a single reference point, the hook under it: the cargo hangs there, below the ship,
/// not from the ship's centre (inside it).</summary>
public class ShipCargoTests
{
    private const float Tick = 1f / 30f;
    /// <summary>DANT_10's bomb ship script: follow_path, then spawn_chain 1, "XBANG".</summary>
    private const int BombShip = 0x20fbc;

    /// <summary>DANT_10's soldier ship script: follow_path, then spawn_chain 3, "XG".</summary>
    private const int SoldierShip = 0x20fe6;
    /// <summary>A hanging soldier (XG, about 8 high) holds the feet of the one above: about 12 apart.</summary>
    private const float SoldierSpacing = 8f;

    [DataFact]
    public void SoldiersHangInALineBelowTheShip()
    {
        var runtime = CreateRuntime();
        var anyone = runtime.Objects.First(o => o.Arena == "DANT_10" && !o.Dead);
        var ship = runtime.Spawn(anyone, "XE", new Vector3(1301f, 5018f, 315f), 180f, -1, SoldierShip, ScriptRuntime.Spawning.Plain)!;
        for (var i = 0; i < 10; i++)
        {
            runtime.Update(Tick);
        }

        // The chain hangs by the hand its animation raises, not the rest pose's (which bunched it up).
        var soldiers = runtime.Objects.Where(o => o.TypeName == "XG" && o.Leader == ship).OrderBy(o => o.InstanceId).ToList();
        Assert.Equal(3, soldiers.Count);
        var above = ship.Position.Z;
        foreach (var soldier in soldiers)
        {
            Assert.True(above - soldier.Position.Z > SoldierSpacing, $"soldier at {soldier.Position.Z}, the one above at {above}");
            above = soldier.Position.Z;
        }
    }

    private static ScriptRuntime CreateRuntime()
    {
        var data = MdkData.Find()!;
        var level = new LevelData(data, 7);
        var cmi = Cmi.Load(data.PathOf("TRAVERSE/LEVEL7/LEVEL7.CMI"));
        var sprites = Bni.Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        var space = new ArenaSpace();
        foreach (var arena in level.Arenas.Where(a => level.IsReachable(a.Name)))
        {
            space.Add(arena);
        }

        var mixer = new SoundMixer(new AudioDevice(Output.Muted), _ => null);
        var runtime = new ScriptRuntime(level, cmi, sprites, space, new TriangleGroups(), mixer, new Kurt.Kurt(space, mixer, _ => 1));
        runtime.TeleportKurt("DANT_10", new Vector3(545.89f, 5023.39f, 184.54f), 0f);
        runtime.Update(Tick);
        return runtime;
    }

    [DataFact]
    public void TheBombHangsFromTheShipsHook()
    {
        var data = MdkData.Find()!;
        var level = new LevelData(data, 7);
        var cmi = Cmi.Load(data.PathOf("TRAVERSE/LEVEL7/LEVEL7.CMI"));
        var sprites = Bni.Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        var space = new ArenaSpace();
        foreach (var arena in level.Arenas.Where(a => level.IsReachable(a.Name)))
        {
            space.Add(arena);
        }

        var mixer = new SoundMixer(new AudioDevice(Output.Muted), _ => null);
        var runtime = new ScriptRuntime(level, cmi, sprites, space, new TriangleGroups(), mixer, new Kurt.Kurt(space, mixer, _ => 1));
        runtime.TeleportKurt("DANT_10", new Vector3(545.89f, 5023.39f, 184.54f), 0f);
        runtime.Update(Tick);

        var anyone = runtime.Objects.First(o => o.Arena == "DANT_10" && !o.Dead);
        var ship = runtime.Spawn(anyone, "XE", new Vector3(1301f, 5018f, 315f), 180f, -1, BombShip, ScriptRuntime.Spawning.Plain)!;
        for (var i = 0; i < 9; i++)
        {
            runtime.Update(Tick);
        }

        var before = ship.Position;
        runtime.Update(Tick);

        // The bomb, newer, runs before the ship (0x45fd4c): it hangs where the hook was a tick ago.
        var bomb = runtime.Objects.First(o => o.TypeName == "XBANG" && o.Leader == ship);
        var hook = ship.ReferencePoint(0) - (ship.Position - before);
        Assert.Single(ship.Model!.ReferencePoints);
        Assert.True(Vector3.Distance(bomb.ReferencePoint(0), hook) < 0.5f,
            $"bomb hook {bomb.ReferencePoint(0)}, ship hook a tick ago {hook}");
        Assert.True(bomb.Position.Z < ship.Position.Z - 5f, $"bomb at {bomb.Position}, ship at {ship.Position}");
    }
}
