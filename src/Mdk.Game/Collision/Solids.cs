using System.Numerics;
using Mdk.Formats;

namespace Mdk.Game.Collision;

/// <summary>Kurt against the solid objects of his arena (damp_collide_move 0x465e34, object pass;
/// damp_platform_floor 0x41d2c4). Each visible part of an object is a box; only the XY of a walk is
/// changed. Platforms (0x100, no 0x10) are stood on: a ray from 3 above the feet to 3 below meets
/// their triangles (0x414668). See godot-mdk docs/bsp.md "Kurt's move".
/// <code>
///      part box grown by Kurt's box     segment A ──► B (box centres, XY slabs, z only rejects)
///   ┌───────────────────────┐
///   │                       │ ◄──── A·──────────► B     first hit: B = entry, the rest slid along
///   │                       │                           the face; later hits: B = entry
///   └───────────────────────┘
/// </code></summary>
public static class Solids
{
    /// <summary>A part's box in the world, its object, whether Kurt may stand on it, and the part.</summary>
    public readonly record struct Solid(Box Box, object Owner, Footing Footing, int Part = 0);

    /// <summary>An owner whose parts' triangles a ray meets (0x414668).</summary>
    internal interface ISurface
    {
        /// <summary>Whether it has triangles (else the ray meets the box's top).</summary>
        bool HasMesh { get; }

        /// <summary>Where the segment <paramref name="from"/> → <paramref name="to"/> first meets the
        /// part's triangles (world z), or null.</summary>
        float? Floor(int part, Vector3 from, Vector3 to);
    }

    /// <summary>A wall, a platform (a wall stood on), or a floor only (standable, else passed through).</summary>
    public enum Footing { Wall, Platform, Floor }

    /// <summary>A ray from 3 above the feet to 3 below finds the platform (damp_platform_floor).</summary>
    public const float PlatformReach = 3f;
    /// <summary>Kurt stops this far before a face, so the next move doesn't start inside the box.</summary>
    private const float Gap = 0.01f;

    /// <summary>The walk from box centre <paramref name="a"/> to <paramref name="b"/> against the
    /// boxes (grown by Kurt's half extents <paramref name="half"/>). Returns the new end, or null
    /// when no box was hit. Boxes Kurt is already inside are ignored (an object moved into him).</summary>
    public static Vector3? Walk(Vector3 a, Vector3 b, Vector3 half, IReadOnlyList<Solid> solids)
    {
        Vector3? end = null;
        foreach (var solid in solids)
        {
            var box = new Box(solid.Box.Min - half, solid.Box.Max + half);
            if (solid.Footing == Footing.Floor || a.Z < box.Min.Z || a.Z > box.Max.Z || Inside(box, a))
            {
                continue;
            }

            var target = end ?? b;
            if (Entry(box, a, target) is not var (t, axis))
            {
                continue;
            }

            var entry = a + (target - a) * t;
            entry[axis] -= MathF.Sign(target[axis] - a[axis]) * Gap;
            if (end != null)
            {
                end = entry;
                continue;
            }

            // The first hit slides along the face.
            var slid = b - entry;
            slid[axis] = 0f;
            end = entry + slid;
        }

        return end;
    }

    /// <summary>The highest platform surface under the feet, met by the ray from 3 above to
    /// <paramref name="bottom"/> (at least 3 below), or null. Owners without triangles
    /// (<see cref="ISurface"/>) are met at their box's top.</summary>
    public static (float Top, object Owner)? Platform(Vector3 feet, float bottom, IReadOnlyList<Solid> solids)
    {
        (float Top, object Owner)? best = null;
        var from = feet.Z + PlatformReach;
        var to = MathF.Min(bottom, feet.Z - PlatformReach);
        foreach (var solid in solids)
        {
            var box = solid.Box;
            if (solid.Footing == Footing.Wall || feet.X < box.Min.X || feet.X > box.Max.X || feet.Y < box.Min.Y || feet.Y > box.Max.Y
                || box.Min.Z > from || box.Max.Z < to)
            {
                continue;
            }

            var top = Surface(solid, feet with { Z = from }, feet with { Z = to });
            if (top is not { } z || z < to || (best is { } b && b.Top >= z))
            {
                continue;
            }

            best = (z, solid.Owner);
        }

        return best;
    }

    /// <summary>Where the ray meets the solid: its triangles, or its box's top.</summary>
    private static float? Surface(Solid solid, Vector3 from, Vector3 to)
    {
        if (solid.Owner is ISurface { HasMesh: true } surface)
        {
            return surface.Floor(solid.Part, from, to);
        }

        return solid.Box.Max.Z <= from.Z ? solid.Box.Max.Z : null;
    }

    /// <summary>The owners of the boxes (grown by <paramref name="half"/> and twice the gap) that hold
    /// the box centre <paramref name="a"/>: what a walk stopped against, or what Kurt is in.</summary>
    public static IEnumerable<object> Touching(Vector3 a, Vector3 half, IReadOnlyList<Solid> solids)
    {
        var owners = new List<object>();
        Touching(a, half, solids, owners);
        return owners;
    }

    /// <summary><see cref="Touching(Vector3, Vector3, IReadOnlyList{Solid})"/> added to
    /// <paramref name="owners"/>, each once.</summary>
    public static void Touching(Vector3 a, Vector3 half, IReadOnlyList<Solid> solids, List<object> owners)
    {
        var reach = half + new Vector3(Gap * 2f);
        foreach (var s in solids)
        {
            var box = new Box(s.Box.Min - reach, s.Box.Max + reach);
            if (a.X >= box.Min.X && a.X <= box.Max.X && a.Y >= box.Min.Y && a.Y <= box.Max.Y && a.Z >= box.Min.Z && a.Z <= box.Max.Z
                && !owners.Contains(s.Owner))
            {
                owners.Add(s.Owner);
            }
        }
    }

    private static bool Inside(Box box, Vector3 p) => p.X > box.Min.X && p.X < box.Max.X && p.Y > box.Min.Y && p.Y < box.Max.Y;

    /// <summary>Where the segment enters the box in XY: the fraction and the axis (0 X, 1 Y) of the face.</summary>
    private static (float T, int Axis)? Entry(Box box, Vector3 a, Vector3 b)
    {
        var enter = 0f;
        var leave = 1f;
        var axis = -1;
        for (var i = 0; i < 2; i++)
        {
            var step = b[i] - a[i];
            if (step == 0f)
            {
                if (a[i] <= box.Min[i] || a[i] >= box.Max[i])
                {
                    return null;
                }

                continue;
            }

            var t0 = (box.Min[i] - a[i]) / step;
            var t1 = (box.Max[i] - a[i]) / step;
            // On the face (t = 0) counts as outside: entered at once (0x45f588: A ≤ min).
            var near = MathF.Min(t0, t1);
            if (near > enter || (near == enter && axis < 0))
            {
                enter = near;
                axis = i;
            }

            leave = MathF.Min(leave, MathF.Max(t0, t1));
            if (enter >= leave)
            {
                return null;
            }
        }

        return axis < 0 ? null : (enter, axis);
    }
}
