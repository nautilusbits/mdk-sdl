using System.Numerics;
using System.Runtime.InteropServices;
using Mdk.Engine.Render;
using Mdk.Formats;
using Mdk.Game.Level;
using Mdk.Game.Scripts;

namespace Mdk.Game.Objects;

/// <summary>Objects' rope lines (obj+0x2d0, drawn as lines by <c>arena_build_drawlist</c> 0x4185f0;
/// godot-mdk mdk_object.gd <c>update_ropes</c>), in a colour of their arena's palette, rebuilt every
/// frame into one dynamic mesh.
/// <code>
///   mask 0xFF:  reference point i+1 ──── rope point i    (each non-zero point, opcode 242)
///   bit 0, 1:   rope point 0 (+5 up) ──── rope point 1    (a swing's rope: object to pivot)
///               rope point 2 (+5 up) ──── rope point 3
/// </code></summary>
public sealed class RopeView(Renderer renderer, LevelData level)
{
    public const int AllLines = 0xFF;
    private const int Pairs = 2;
    private const float PairLift = 5f;
    /// <summary>Line vertices drawn at most per frame (the rest is dropped).</summary>
    private const int MaxVertices = 1 << 12;
    private const float ByteToUnit = 1f / 255f;

    private readonly int _mesh = renderer.CreateDynamicMesh(MaxVertices);
    private readonly Dictionary<string, Palette> _palettes = [];
    private readonly List<Vertex> _vertices = [];
    private readonly List<(int First, int Count, Material Material, DepthBand Band)> _draws = [];

    /// <summary>The steps' frames: ropes where the tick left them.</summary>
    private static readonly FrameBlend Steps = new(Blending.Off);

    /// <summary>The line ends of an object's ropes, two points each.</summary>
    public static List<Vector3> LinesOf(MdkObject obj) => LinesOf(obj, Steps);

    /// <summary>The line ends as <paramref name="blend"/>'s frame draws them: the ends on the
    /// object move with it.</summary>
    public static List<Vector3> LinesOf(MdkObject obj, FrameBlend blend)
    {
        var lines = new List<Vector3>();
        AddLines(obj, blend, lines);
        return lines;
    }

    /// <summary><see cref="LinesOf"/> added to <paramref name="lines"/>.</summary>
    private static void AddLines(MdkObject obj, FrameBlend blend, List<Vector3> lines)
    {
        if (obj.RopeMask == AllLines)
        {
            for (var i = 0; i < obj.RopePoints.Length; i++)
            {
                if (obj.RopePoints[i] != Vector3.Zero)
                {
                    lines.Add(blend.Carried(obj, obj.ReferencePoint(i + 1)));
                    lines.Add(obj.RopePoints[i]);
                }
            }

            return;
        }

        for (var i = 0; i < Pairs; i++)
        {
            if ((obj.RopeMask & (1 << i)) != 0)
            {
                lines.Add(blend.Carried(obj, obj.RopePoints[i * 2]) + Vector3.UnitZ * PairLift);
                lines.Add(obj.RopePoints[i * 2 + 1]);
            }
        }
    }

    /// <summary>An object's line ends (kept: no list per frame).</summary>
    private readonly List<Vector3> _lines = [];

    /// <summary>Queues the ropes of the drawn objects, as <paramref name="blend"/>'s frame draws them,
    /// in their arenas' bands (<paramref name="camera"/>: the camera's arena).</summary>
    public void Draw(List<MdkObject> objects, FrameBlend blend, string camera)
    {
        _vertices.Clear();
        _draws.Clear();
        foreach (var obj in objects)
        {
            if (obj.RopeMask == 0 || !obj.Visible || obj.Dead)
            {
                continue;
            }

            var lines = _lines;
            lines.Clear();
            AddLines(obj, blend, lines);
            if (lines.Count == 0 || _vertices.Count + lines.Count > MaxVertices)
            {
                continue;
            }

            _draws.Add((_vertices.Count, lines.Count, Material.Flat(Colour(obj.Arena, obj.RopeColor), Pass.Solid), ArenaBand.Of(obj.Arena, camera)));
            foreach (var point in lines)
            {
                _vertices.Add(new Vertex(point, Vector2.Zero));
            }
        }

        if (_vertices.Count == 0)
        {
            return;
        }

        renderer.UpdateMesh(_mesh, CollectionsMarshal.AsSpan(_vertices));
        foreach (var (first, count, material, band) in _draws)
        {
            renderer.Band = band;
            renderer.DrawLines(_mesh, first, count, material);
        }

        renderer.Band = DepthBand.Front;
    }

    /// <summary>A colour of an arena's palette (the level's for an unknown arena).</summary>
    private Vector4 Colour(string arena, int index)
    {
        if (!_palettes.TryGetValue(arena, out var palette))
        {
            var found = level.ArenaNamed(arena);
            palette = _palettes[arena] = found != null ? level.PaletteOf(found) : level.Dti.Palette;
        }

        var rgba = palette.Rgba;
        var at = Math.Clamp(index, 0, byte.MaxValue) * 4;
        return new Vector4(rgba[at], rgba[at + 1], rgba[at + 2], byte.MaxValue) * ByteToUnit;
    }
}
