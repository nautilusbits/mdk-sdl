using System.Numerics;
using Mdk.Formats;
using Mdk.Game.HdTextures;

namespace Mdk.Game.Mods;

/// <summary>A mod's replacement of a model's parts, from a glTF file (<see cref="Glb"/>): each mesh
/// node named as a part (case ignored; a model of one unnamed part: the model's name) replaces that
/// part's look; parts without a node keep theirs. The original geometry still collides, is hit and
/// animates: a replaced part follows its original through <see cref="PartFit"/>.
/// <code>
///   glTF (Y up)  x, y, z  ─►  MDK (Z up)  x, −z, y        1 unit = 1 MDK unit, the model's rest pose
///   node "HEAD" ─► part HEAD: its triangles, UVs (0-1), materials
///   material: its PNG ─► a mod image │ none, named as an original (WALL1, PEN_12, GLASS1) ─► that │ else its colour
/// </code></summary>
public sealed class ModelSwap
{
    /// <summary>A replacement part in the model's space (rest pose): triangles of vertices with UVs, a material each.</summary>
    public sealed record Mesh(Vector3[] Vertices, Vector2[] Uvs, int[] Triangles, int[] Materials);

    /// <summary>A replacement material: its name, colour (sRGB) and image (premultiplied), if any.</summary>
    public sealed record Surface(string Name, Vector4 Colour, HdImage? Image);

    private const float Gamma = 2.2f;
    private const int Corners = 3;

    /// <summary>Per original part, its replacement (null: kept).</summary>
    public Mesh?[] Parts { get; }
    public IReadOnlyList<Surface> Surfaces { get; }
    private readonly PartFit[] _fits;
    /// <summary>The draw's transform of each replaced part (scratch).</summary>
    private readonly Matrix4x4[] _poses;

    private ModelSwap(Model model, Mesh?[] parts, List<Surface> surfaces)
    {
        Parts = parts;
        Surfaces = surfaces;
        _fits = [.. model.PartList.Select(p => new PartFit(p.Vertices, p.TriangleIndices))];
        _poses = new Matrix4x4[parts.Length];
    }

    /// <summary>A glTF point (Y up) in MDK coordinates (Z up).</summary>
    public static Vector3 ToMdk(Vector3 gltf) => new(gltf.X, -gltf.Z, gltf.Y);

    /// <summary>An MDK point in glTF coordinates.</summary>
    public static Vector3 ToGltf(Vector3 mdk) => new(mdk.X, mdk.Z, -mdk.Y);

    /// <summary>A part's name in glTF files: its own, or the model's for a model of one unnamed part.</summary>
    public static string PartName(Model model, int part) =>
        model.PartList[part].Name.Length != 0 ? model.PartList[part].Name : model.Name;

    /// <summary>The replacement a scene makes of a model; null when no node names a part. Nodes naming
    /// no part are listed in <paramref name="unknown"/>.</summary>
    public static ModelSwap? Of(Glb.Scene scene, Model model, List<string> unknown)
    {
        // Parts of the same name take the nodes of that name in order.
        var byName = new Dictionary<string, Queue<int>>(StringComparer.OrdinalIgnoreCase);
        for (var p = 0; p < model.PartList.Count; p++)
        {
            var name = PartName(model, p);
            if (!byName.TryGetValue(name, out var queue))
            {
                byName[name] = queue = new Queue<int>();
            }

            queue.Enqueue(p);
        }

        var parts = new Mesh?[model.PartList.Count];
        foreach (var mesh in scene.Meshes)
        {
            if (!byName.TryGetValue(mesh.Name, out var queue) || !queue.TryDequeue(out var part))
            {
                unknown.Add(mesh.Name);
                continue;
            }

            parts[part] = Merge(mesh);
        }

        if (parts.All(p => p == null))
        {
            return null;
        }

        var surfaces = scene.Materials.Select(m => new Surface(m.Name, ToSrgb(m.Colour), m.Png is { } png ? Decode(png) : null)).ToList();
        return new ModelSwap(model, parts, surfaces);
    }

    /// <summary>A node's primitives as one mesh, in MDK coordinates; no material: a white one at the end.</summary>
    private static Mesh Merge(Glb.Mesh mesh)
    {
        var vertices = new List<Vector3>();
        var uvs = new List<Vector2>();
        var triangles = new List<int>();
        var materials = new List<int>();
        foreach (var primitive in mesh.Primitives)
        {
            var first = vertices.Count;
            vertices.AddRange(primitive.Positions.Select(ToMdk));
            uvs.AddRange(primitive.Uvs);
            triangles.AddRange(primitive.Indices.Select(i => first + i));
            materials.AddRange(Enumerable.Repeat(primitive.Material, primitive.Indices.Length / Corners));
        }

        return new Mesh([.. vertices], [.. uvs], [.. triangles], [.. materials]);
    }

    /// <summary>A material's PNG for the renderer, or null (broken: its colour instead).</summary>
    private static HdImage? Decode(byte[] png)
    {
        try
        {
            var image = Png.Decode(png);
            ModImages.Premultiply(image.Rgba);
            return new HdImage(image.Width, image.Height, 1, image.Rgba);
        }
        catch (Exception e) when (e is InvalidDataException or IOException)
        {
            Console.Error.WriteLine($"Mod model image: {e.Message}");
            return null;
        }
    }

    /// <summary>glTF's base colour is linear; the game's colours sRGB.</summary>
    public static Vector4 ToSrgb(Vector4 linear) =>
        new(MathF.Pow(linear.X, 1f / Gamma), MathF.Pow(linear.Y, 1f / Gamma), MathF.Pow(linear.Z, 1f / Gamma), linear.W);

    public static Vector4 ToLinear(Vector4 srgb) =>
        new(MathF.Pow(srgb.X, Gamma), MathF.Pow(srgb.Y, Gamma), MathF.Pow(srgb.Z, Gamma), srgb.W);

    /// <summary>Fits each replaced, shown part to its pose (a draw; no allocation).</summary>
    public void Pose(Vector3[][] pose, int hidden)
    {
        for (var p = 0; p < Parts.Length; p++)
        {
            if (Parts[p] == null || (hidden & (1 << p)) != 0)
            {
                continue;
            }

            _poses[p] = _fits[p].Fit(pose[p]);
        }
    }

    /// <summary>A replaced part's vertex in the pose last fitted.</summary>
    public Vector3 Vertex(int part, int index) => Vector3.Transform(Parts[part]!.Vertices[index], _poses[part]);
}
