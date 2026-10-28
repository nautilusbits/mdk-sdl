using System.Numerics;
using Mdk.Formats;

namespace Mdk.Game.Collision;

/// <summary>The arenas Kurt can be in, and his moves through their BSPs (<c>damp_collide_move</c>
/// 0x465e34): his arena with sliding, then, if that ended free, the arena he moves into without.
/// The arena around a point is the smallest whose bounds (grown by 2) hold it.
/// <code>
///   feet ──► box (0.6, 0.6, 2.5) at feet + 0.5 (walking) │ (0.4, 0.4, 2.5) at feet + 0.01 (falling)
///        ──► Kurt's arena: 4 slides ──free?──► the other arena: no slide ──► new feet
/// </code></summary>
public sealed partial class ArenaSpace
{
    /// <summary>Which of Kurt's two boxes a move uses.</summary>
    public enum Motion { Walk, Fall }

    /// <summary>A move's result: where the feet end, and the triangle and node (plane) it stopped on.</summary>
    public readonly record struct Result(Vector3 Feet, int Triangle, Bsp? Bsp, int Node)
    {
        public bool Hit => Triangle != Bsp.None;

        /// <summary>The plane's normal of the contact, or up without one.</summary>
        public Vector3 Normal => Bsp == null || Node < 0 ? Vector3.UnitZ : Bsp.Arena.Nodes[Node].Normal;
    }

    private static readonly Vector3 WalkBox = new(0.6f, 0.6f, 2.5f);
    private static readonly Vector3 FallBox = new(0.4f, 0.4f, 2.5f);
    /// <summary>The box's bottom above the feet: anything lower is walked over.</summary>
    private const float WalkLift = 0.5f;
    private const float FallLift = 0.01f;
    private const int Slides = 4;
    private const float BoundsMargin = 2f;
    /// <summary>Faces at least this upright are floors (0x421708).</summary>
    private const float FloorNz = 0.5f;

    /// <summary>The arenas, smallest first (equal volumes in the order added).</summary>
    private readonly List<(Bsp Bsp, Vector3 Min, Vector3 Max, float Volume)> _arenas = [];
    /// <summary>The arenas Kurt collides with, his first (empty: those around him by bounds).</summary>
    private readonly List<Bsp> _solid = [];
    /// <summary>The queries' arenas around points (kept: no list per query).</summary>
    private readonly List<Bsp> _around = [];
    private readonly List<Bsp> _kurts = [];

    /// <summary>Kurt collides with his arena and the active second one only (0x465e34); rays and
    /// objects still see every arena.</summary>
    public void SetSolid(IReadOnlyList<string> arenas)
    {
        _solid.Clear();
        for (var i = 0; i < arenas.Count; i++)
        {
            if (Named(arenas[i]) is { } bsp)
            {
                _solid.Add(bsp);
            }
        }
    }

    private Bsp? Named(string name)
    {
        foreach (var arena in _arenas)
        {
            if (arena.Bsp.Arena.Name == name)
            {
                return arena.Bsp;
            }
        }

        return null;
    }

    /// <summary>Adds an arena once (a teleport may add one no connection leads to).</summary>
    public void Add(Arena arena)
    {
        if (arena.Vertices.Length == 0 || _arenas.Exists(a => a.Bsp.Arena == arena))
        {
            return;
        }

        var min = arena.Vertices.Aggregate(Vector3.Min);
        var max = arena.Vertices.Aggregate(Vector3.Max);
        var size = max - min;
        var volume = size.X * size.Y * size.Z;
        var at = _arenas.Count;
        while (at > 0 && _arenas[at - 1].Volume > volume)
        {
            at--;
        }

        _arenas.Insert(at, (new Bsp(arena), min, max, volume));
    }

    /// <summary>The arenas around a point, smallest first.</summary>
    public IEnumerable<Bsp> At(Vector3 point)
    {
        var around = new List<Bsp>();
        AddAt(point, around);
        return around;
    }

    /// <summary>Adds the arenas around a point not in <paramref name="arenas"/> yet, smallest first.</summary>
    private void AddAt(Vector3 point, List<Bsp> arenas)
    {
        foreach (var arena in _arenas)
        {
            if (Contains(arena.Min, arena.Max, point) && !arenas.Contains(arena.Bsp))
            {
                arenas.Add(arena.Bsp);
            }
        }
    }

    /// <summary>The smallest arena around a point, or null.</summary>
    private Bsp? First(Vector3 point)
    {
        foreach (var arena in _arenas)
        {
            if (Contains(arena.Min, arena.Max, point))
            {
                return arena.Bsp;
            }
        }

        return null;
    }

    private static bool Contains(Vector3 min, Vector3 max, Vector3 p) =>
        p.X >= min.X - BoundsMargin && p.Y >= min.Y - BoundsMargin && p.Z >= min.Z - BoundsMargin
        && p.X <= max.X + BoundsMargin && p.Y <= max.Y + BoundsMargin && p.Z <= max.Z + BoundsMargin;

    /// <summary>The name of the arena around a point, or null.</summary>
    public string? ArenaAt(Vector3 point) => First(point)?.Arena.Name;

    /// <summary>Moves Kurt's feet by <paramref name="delta"/>, sliding along faces met obliquely
    /// enough (<c>(D·n)² ≤ slideK·|D|²</c>).</summary>
    public Result Move(Vector3 feet, Vector3 delta, Motion motion, float slideK)
    {
        var (box, lift) = motion == Motion.Walk ? (WalkBox, WalkLift) : (FallBox, FallLift);
        var a = feet + new Vector3(0f, 0f, box.Z + lift);
        var arenas = _solid;
        if (arenas.Count == 0)
        {
            arenas = _around;
            arenas.Clear();
            AddAt(feet, arenas);
            AddAt(feet + delta, arenas);
        }

        if (arenas.Count == 0)
        {
            return new Result(feet + delta, Bsp.None, null, Bsp.None);
        }

        var own = arenas[0];
        var triangle = own.SweepBox(a, a + delta, box, Slides, slideK, out var end, out var node);
        var hit = own;
        for (var i = 1; i < arenas.Count; i++)
        {
            var other = arenas[i];
            if (triangle != Bsp.None)
            {
                break;
            }

            triangle = other.SweepBox(a, end, box, 0, slideK, out end, out node);
            hit = other;
        }

        return new Result(feet + (end - a), triangle, triangle == Bsp.None ? null : hit, node);
    }

    /// <summary>The same move, then against the solid objects around (damp_collide_move's object
    /// pass): they change only the XY, and the BSP is swept again to the new end.</summary>
    public Result Move(Vector3 feet, Vector3 delta, Motion motion, float slideK, IReadOnlyList<Solids.Solid> solids)
    {
        var result = Move(feet, delta, motion, slideK);
        var (box, lift) = motion == Motion.Walk ? (WalkBox, WalkLift) : (FallBox, FallLift);
        var a = feet + new Vector3(0f, 0f, box.Z + lift);
        if (solids.Count == 0 || Solids.Walk(a, a + (result.Feet - feet), box, solids) is not { } end)
        {
            return result;
        }

        var again = Move(feet, new Vector3(end.X - a.X, end.Y - a.Y, 0f), motion, slideK);
        return again with { Feet = new Vector3(again.Feet.X, again.Feet.Y, result.Feet.Z) };
    }

    /// <summary>How far the falling box at <paramref name="feet"/> lies below the floor plane a move
    /// stopped on (0: above it, or no floor, |nz| &lt; 0.5). Its lowest corner counts: on a slope
    /// that is more than under the centre.</summary>
    public static float Sunk(Result result, Vector3 feet)
    {
        if (result.Bsp == null || result.Node < 0)
        {
            return 0f;
        }

        // The plane, facing up.
        var plane = result.Bsp.Arena.Nodes[result.Node];
        var (n, d) = plane.Normal.Z < 0f ? (-plane.Normal, -plane.D) : (plane.Normal, plane.D);
        if (n.Z < FloorNz)
        {
            return 0f;
        }

        // The box's reach towards the plane, less the centre's height above it, upright.
        var centre = feet + new Vector3(0f, 0f, FallBox.Z + FallLift);
        var reach = MathF.Abs(FallBox.X * n.X) + MathF.Abs(FallBox.Y * n.Y) + FallBox.Z * n.Z;
        return MathF.Max(0f, (reach - (Vector3.Dot(n, centre) + d)) / n.Z);
    }

    /// <summary>The owners of the solids Kurt's walking box at <paramref name="feet"/> touches or is in.</summary>
    public static void Touching(Vector3 feet, IReadOnlyList<Solids.Solid> solids, List<object> owners) =>
        Solids.Touching(feet + new Vector3(0f, 0f, WalkBox.Z + WalkLift), WalkBox, solids, owners);

    /// <summary>The nearest floor a segment crosses (0x421708), in any arena around its ends.</summary>
    public bool Floor(Vector3 from, Vector3 to, out Vector3 point)
    {
        point = to;
        var best = float.MaxValue;
        _around.Clear();
        AddAt(from, _around);
        AddAt(to, _around);
        foreach (var bsp in _around)
        {
            if (bsp.Segment(from, to, Bsp.SegmentMode.Floor, out var hit) == Bsp.None)
            {
                continue;
            }

            var distance = Vector3.DistanceSquared(from, hit);
            if (distance < best)
            {
                best = distance;
                point = hit;
            }
        }

        return best < float.MaxValue;
    }
}
