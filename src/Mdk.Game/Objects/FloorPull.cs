using System.Numerics;
using Mdk.Formats;
using Mdk.Game.Collision;
using Mdk.Game.Kurt;

namespace Mdk.Game.Objects;

/// <summary>The ridden board in front of the floors it stands above. The original draws an object
/// after the floor of its BSP leaf, so the board shows whole even where it dips into the slope
/// (uphill its feet lag a step under it, banked turns sink an edge); with a depth buffer that part
/// hid. As Kurt's sprite (<see cref="KurtSprite.InFront"/>) it's scaled towards the eye: the same
/// on screen, nearer than the floors the eye's rays to its box cross near it (a crest farther on,
/// hiding its nose, doesn't count). It can sink deeper than it is thick (a flat floor, a bank, a
/// slope's change), whole under the floor.
/// <code>
///   eye ●────────────────────╱── floor
///                     board ▭  (under the floor at a corner) ─► pulled in front of it
/// </code></summary>
internal static class FloorPull
{
    /// <summary>The box's points: a grid of 3 along x and y, its bottom and top; the middles catch
    /// a bump under the board or a tube's floor, its ends and edges above it.</summary>
    private const int Grid = 3;
    private const int Layers = 2;
    private const float MinDepth = 1e-3f;

    /// <summary>The board's <paramref name="transform"/> scaled towards the <paramref name="eye"/>
    /// looking along <paramref name="forward"/>.</summary>
    public static Matrix4x4 Of(MdkObject board, Matrix4x4 transform, Vector3 eye, Vector3 forward, ArenaSpace space)
    {
        if (board.Model == null)
        {
            return transform;
        }

        // Floors count where the rays cross them within the board's length of the point: the
        // floor it sank into, not a crest farther towards the eye.
        var box = board.Model.Bounds;
        var reach = Vector3.Distance(Vector3.Transform(box.Min, transform), Vector3.Transform(box.Max, transform));
        var scale = 1f;
        for (var i = 0; i < Grid * Grid * Layers; i++)
        {
            // Point i: x from i % 3, y from i / 3 % 3 (min, middle, max), z from i / 9 (bottom, top).
            var at = new Vector3(i % Grid, i / Grid % Grid, i / (Grid * Grid)) / new Vector3(Grid - 1, Grid - 1, Layers - 1);
            var local = Vector3.Lerp(box.Min, box.Max, at);
            var corner = Vector3.Transform(local, transform);
            if (space.FloorCrossing(eye, corner) is not { } floor || Vector3.Distance(corner, floor.Point) > reach)
            {
                continue;
            }

            // A corner on the eye's plane has no depth to pull to.
            var depth = Vector3.Dot(corner - eye, forward);
            if (depth <= MinDepth)
            {
                continue;
            }

            scale = MathF.Min(scale, KurtSprite.InFront(depth, Vector3.Dot(floor.Point - eye, forward)));
        }

        return scale == 1f ? transform : transform * Matrix4x4.CreateScale(scale, eye);
    }
}
