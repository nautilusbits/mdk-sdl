using System.Numerics;
using Mdk.Engine.Audio;
using Mdk.Engine.Platform;
using Mdk.Formats;
using Mdk.Game.Audio;
using Mdk.Game.Collision;
using Mdk.Game.Objects;
using Mdk.Game.Scripts;

namespace Mdk.Game.Tests;

/// <summary>Level 3's HMO_9 hover ship (XGS): its pilot flies it down by the corridor; shot, an
/// empty ship (set_targetable 2: flag 0x800000) waits for Kurt, then flies him through the low
/// opening north (path 126468). Landed on it, Kurt stays on it (damp_gravity 0x469efc sets
/// 0x573b8c; each step his XY is the ship's), so the opening's edge doesn't scrape him off.</summary>
public class HoverShipTests
{
    private const int Seed = 1;
    private const int Level = 3;
    private const float Step = Viewer.Step;
    private const string Arena = "HMO_9";
    private const string Ship = "XGS";
    /// <summary>The pilot's ship lands with 30 hit points (its path done).</summary>
    private const int LandedHealth = 30;
    private const int LandSteps = 20 * 60;
    private const int WaitSteps = 2 * 60;
    private const int RideSteps = 4 * 60;
    /// <summary>Kurt is put this far above the empty ship.</summary>
    private const float Drop = 3f;
    /// <summary>Past the opening (y 2680): the ship's way on north.</summary>
    private const float BeyondOpening = 2690f;
    /// <summary>The ship's deck above its origin (SURF's floor; its fins reach 3).</summary>
    private const float Deck = 0.35f;
    private const float DeckTolerance = 0.1f;
    private static readonly Vector3 Corridor = new(-178.15f, 2602.88f, -300.95f);

    private static readonly Lazy<MdkData> Data = new(() => MdkData.Find()!);
    private static readonly AudioDevice Device = new(Output.Muted);

    private sealed record World(ScriptRuntime Runtime, Kurt.Kurt Kurt, ArenaSpace Space);

    private static World Create()
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

        var mixer = new SoundMixer(Device, _ => null);
        var kurt = new Kurt.Kurt(space, mixer, _ => 1);
        return new World(new ScriptRuntime(level, cmi, sprites, space, groups, mixer, kurt, Seed), kurt, space);
    }

    /// <summary>Steps Kurt and the scripts as the viewer does, until <paramref name="done"/> or
    /// <paramref name="steps"/> run out.</summary>
    private static void Run(World world, int steps, Func<bool>? done = null)
    {
        var input = new Input();
        for (var i = 0; i < steps && done?.Invoke() != true; i++)
        {
            world.Kurt.Update(input, Step);
            world.Runtime.Update(Step);
            world.Space.SetSolid(world.Runtime.SolidArenas);
        }
    }

    [DataFact]
    public void ShipFliesKurtThroughTheOpening()
    {
        var world = Create();
        var runtime = world.Runtime;
        world.Runtime.TeleportKurt(Arena, Corridor, 90f);
        Run(world, 1);
        var pilot = runtime.FindObjectNamed(Ship)!;
        Run(world, LandSteps, () => pilot.Health == LandedHealth);

        // The pilot's death leaves the empty ship; Kurt lands on it.
        runtime.Kill(pilot);
        Run(world, WaitSteps);
        var ship = runtime.FindObjectNamed(Ship)!;
        Assert.NotSame(pilot, ship);
        runtime.TeleportKurt(Arena, ship.Position + new Vector3(0f, 0f, Drop), 90f);
        Run(world, RideSteps);

        Assert.True(ship.Position.Y > BeyondOpening);
        Assert.Same(ship, world.Kurt.Platform);
        Assert.Equal(new Vector2(ship.Position.X, ship.Position.Y), new Vector2(world.Kurt.Feet.X, world.Kurt.Feet.Y));
    }

    /// <summary>damp_platform_floor (0x41d2c4) casts its ray at the platform's triangles
    /// (0x414668), not its box: Kurt stands on the deck, not level with the fins.</summary>
    [DataFact]
    public void KurtStandsOnTheShipsDeck()
    {
        var world = Create();
        var runtime = world.Runtime;
        runtime.TeleportKurt(Arena, Corridor, 90f);
        Run(world, 1);
        var pilot = runtime.FindObjectNamed(Ship)!;
        Run(world, LandSteps, () => pilot.Health == LandedHealth);
        runtime.Kill(pilot);
        Run(world, WaitSteps);
        var ship = runtime.FindObjectNamed(Ship)!;

        runtime.TeleportKurt(Arena, ship.Position + new Vector3(0f, 0f, Drop), 90f);
        Run(world, RideSteps / 2);

        Assert.Same(ship, world.Kurt.Platform);
        Assert.InRange(world.Kurt.Feet.Z - ship.Position.Z, Deck - DeckTolerance, Deck + DeckTolerance);
    }
}
