using System.Numerics;
using Mdk.Engine.Render;
using Mdk.Formats;
using Mdk.Game.Scripts;

namespace Mdk.Game.Level;

/// <summary>The level's arenas on the GPU: a vertex buffer per triangle group, its triangles in
/// batches by surface. Hidden groups and unreachable arenas aren't drawn; a group that changes
/// texture is rebuilt.
/// At the end of a level the triangles torn off Kurt's arena are collapsed in their group's mesh
/// (made dynamic) and drawn as flying pieces.
/// <code>
///   arena ─┬─ group 0 (the rest)   ─► mesh, batches
///          └─ group n (scripted)   ─► mesh, batches   (hidden? retextured?)
///   end of level: torn triangles ─► collapsed in their mesh;  pieces ─► one dynamic mesh
/// </code></summary>
public sealed class LevelView
{
    /// <summary>Triangles (or outlines) of one surface; <paramref name="Animated"/> names a texture
    /// whose frame the scripts set (opcode 133).</summary>
    private readonly record struct Batch(int First, int Count, Material Material, string? Animated = null,
        Primitive Primitive = Primitive.Triangles);

    private sealed class GroupMesh(int mesh, List<Batch> batches, Vertex[] vertices)
    {
        public int Mesh = mesh;
        public readonly List<Batch> Batches = batches;
        public readonly Vertex[] Vertices = vertices;
        public bool Dynamic;
    }

    /// <summary>Where a triangle is drawn: its group, first vertex, surface and UV scale.</summary>
    private readonly record struct Placement(int Group, int First, Material Material, Vector2 Scale);

    private sealed class ArenaView(Arena arena, Palette palette, List<TextureArchive> archives, bool reachable)
    {
        public readonly Arena Arena = arena;
        public readonly Vector3[] Positions = [.. arena.TriangleIndices.Select(i => arena.Vertices[i])];
        public readonly int[] Layers = Level.Layers.Of(arena);
        public readonly Palette Palette = palette;
        public readonly List<TextureArchive> Archives = archives;
        public bool Reachable = reachable;
        public readonly Dictionary<int, GroupMesh?> Groups = [];
        public readonly Dictionary<int, Placement> Placements = [];
    }

    private const int TriangleVertices = 3;

    private readonly Renderer _renderer;
    private readonly MaterialResolver _resolver;
    private readonly TriangleGroups _groups;
    private readonly OutlineEdges _edges;
    private readonly Dictionary<string, ArenaView> _arenas = [];
    private int _tornShown;
    private int _piecesMesh = -1;
    private Vertex[] _pieceVertices = [];

    /// <summary>The arenas' surfaces come from <paramref name="resolver"/> (shared with the objects
    /// and effects: one GPU copy of each texture, one upload per bullet hole).</summary>
    public LevelView(Renderer renderer, LevelData level, TriangleGroups groups, Shading shading, MaterialResolver resolver)
    {
        _renderer = renderer;
        _groups = groups;
        _resolver = resolver;
        _edges = shading == Shading.Original ? OutlineEdges.Flagged : OutlineEdges.Frame;
        foreach (var arena in level.Arenas)
        {
            groups.Add(arena);
            var view = new ArenaView(arena, level.PaletteOf(arena), level.ArchivesOf(arena), level.IsReachable(arena.Name));
            _arenas[arena.Name] = view;
            foreach (var group in groups.Of(arena.Name))
            {
                view.Groups[group.Number] = Build(view, group);
            }
        }

        groups.Changed += Rebuild;
    }

    public int TriangleCount { get; private set; }

    /// <summary>The outlined edges built (glass frames).</summary>
    public int OutlineCount { get; private set; }

    /// <summary>Shows an arena Kurt can only reach by a teleport.</summary>
    public void Enter(string arena)
    {
        if (_arenas.TryGetValue(arena, out var view))
        {
            view.Reachable = true;
        }
    }

    /// <summary>Queues the visible groups of <paramref name="arenas"/> (the original draws Kurt's
    /// arena and the active second one, 0x41e344), or of every reachable arena when empty, animated
    /// textures at their <paramref name="frames"/>; the <paramref name="camera"/>'s arena over the
    /// others (<see cref="ArenaBand"/>).</summary>
    public void Draw(IReadOnlyList<string> arenas, AnimatedTextures frames, string camera)
    {
        if (arenas.Count == 0)
        {
            foreach (var view in _arenas.Values)
            {
                if (view.Reachable)
                {
                    Draw(view, frames);
                }
            }

            return;
        }

        for (var i = 0; i < arenas.Count; i++)
        {
            if (_arenas.TryGetValue(arenas[i], out var view))
            {
                _renderer.Band = ArenaBand.Of(arenas[i], camera);
                Draw(view, frames);
            }
        }

        _renderer.Band = DepthBand.Front;
    }

    private void Draw(ArenaView view, AnimatedTextures frames)
    {
        foreach (var (number, mesh) in view.Groups)
        {
            if (mesh == null || (_groups.Get(view.Arena.Name, number)!.State & TriangleGroups.State.Hidden) != 0)
            {
                continue;
            }

            foreach (var batch in mesh.Batches)
            {
                if (batch.Primitive == Primitive.Lines)
                {
                    _renderer.DrawLines(mesh.Mesh, batch.First, batch.Count, batch.Material);
                    continue;
                }

                var frame = batch.Animated != null ? frames.FrameOf(view.Arena.Name, batch.Animated) : 0;
                _renderer.Draw(mesh.Mesh, batch.First, batch.Count, batch.Material, frame);
            }
        }
    }

    /// <summary>The end of a level: the torn triangles vanish from the arena and fly as pieces, in
    /// its band (<paramref name="camera"/>: the camera's arena).</summary>
    public void DrawEnd(EndLevel? end, string camera)
    {
        if (end == null || !_arenas.TryGetValue(end.Arena, out var view))
        {
            return;
        }

        Tear(view, end.Torn);
        _renderer.Band = ArenaBand.Of(end.Arena, camera);
        DrawPieces(view, end.Pieces);
        _renderer.Band = DepthBand.Front;
    }

    /// <summary>Collapses the newly torn triangles in their groups' meshes (made dynamic).</summary>
    private void Tear(ArenaView view, IReadOnlyList<int> torn)
    {
        if (torn.Count == _tornShown)
        {
            return;
        }

        var changed = new HashSet<GroupMesh>();
        for (; _tornShown < torn.Count; _tornShown++)
        {
            if (!view.Placements.TryGetValue(torn[_tornShown], out var place) || view.Groups[place.Group] is not { } mesh)
            {
                continue;
            }

            for (var k = 1; k < TriangleVertices; k++)
            {
                mesh.Vertices[place.First + k] = mesh.Vertices[place.First];
            }

            changed.Add(mesh);
        }

        foreach (var mesh in changed)
        {
            if (!mesh.Dynamic)
            {
                mesh.Mesh = _renderer.CreateDynamicMesh(mesh.Vertices.Length);
                mesh.Dynamic = true;
            }

            _renderer.UpdateMesh(mesh.Mesh, mesh.Vertices);
        }
    }

    /// <summary>The pieces, turned by their angle around their centre, both faces, batched by surface.</summary>
    private void DrawPieces(ArenaView view, IReadOnlyList<EndLevel.Piece> pieces)
    {
        if (_piecesMesh < 0)
        {
            _pieceVertices = new Vertex[view.Arena.TriangleCount * TriangleVertices];
            _piecesMesh = _renderer.CreateDynamicMesh(_pieceVertices.Length);
        }

        var count = 0;
        var batches = new List<Batch>();
        foreach (var bySurface in pieces.Where(p => view.Placements.ContainsKey(p.Triangle)).GroupBy(p => view.Placements[p.Triangle].Material))
        {
            var first = count;
            foreach (var piece in bySurface)
            {
                count = AddPiece(view, piece, count);
            }

            var material = bySurface.Key.Pass == Pass.Solid ? bySurface.Key with { Pass = Pass.DoubleSided } : bySurface.Key;
            batches.Add(new Batch(first, count - first, material));
        }

        if (count == 0)
        {
            return;
        }

        _renderer.UpdateMesh(_piecesMesh, _pieceVertices.AsSpan(0, count));
        foreach (var batch in batches)
        {
            _renderer.Draw(_piecesMesh, batch.First, batch.Count, batch.Material);
        }
    }

    private int AddPiece(ArenaView view, EndLevel.Piece piece, int at)
    {
        var place = view.Placements[piece.Triangle];
        var t = piece.Triangle;
        var corners = new Vector3[TriangleVertices];
        var middle = Vector3.Zero;
        for (var k = 0; k < TriangleVertices; k++)
        {
            corners[k] = view.Positions[t * TriangleVertices + k];
            middle += corners[k] / TriangleVertices;
        }

        var turn = Matrix4x4.CreateRotationZ(float.DegreesToRadians(piece.Angle));
        foreach (var k in (int[])[0, 1, 2])
        {
            var position = piece.Centre + Vector3.Transform(corners[k] - middle, turn);
            _pieceVertices[at++] = new Vertex(position, view.Arena.TriangleUvs[t * TriangleVertices + k] * place.Scale);
        }

        return at;
    }

    /// <summary>A group took another texture: its mesh is built again (the old buffer stays until the level ends).</summary>
    private void Rebuild(string arena, int number)
    {
        var view = _arenas[arena];
        var group = _groups.Get(arena, number)!;
        if (group.Material is not null)
        {
            view.Groups[number] = Build(view, group);
        }
    }

    /// <summary>The outlines of the flat-coloured surfaces, after the triangles, in their colour.</summary>
    private void AddOutlines(Arena arena, Dictionary<Material, (Texture? Texture, List<int> Triangles)> bySurface,
        List<Vertex> vertices, List<Batch> batches)
    {
        foreach (var (material, (texture, triangles)) in bySurface)
        {
            // Mirrors show the panorama: no colour for lines.
            if (texture != null || material.Pass == Pass.Mirror)
            {
                continue;
            }

            var lines = Outlines.Of(arena, triangles, _edges);
            if (lines.Count == 0)
            {
                continue;
            }

            OutlineCount += lines.Count / 2;
            batches.Add(new Batch(vertices.Count, lines.Count, material, Primitive: Primitive.Lines));
            vertices.AddRange(lines.Select(p => new Vertex(p, Vector2.Zero)));
        }
    }

    private GroupMesh? Build(ArenaView view, TriangleGroups.Group group)
    {
        var arena = view.Arena;

        // Triangles by surface, so each batch is one draw.
        var bySurface = new Dictionary<Material, (Texture? Texture, List<int> Triangles)>();
        foreach (var t in group.Triangles)
        {
            // The 1996 demo's triangles that only stop Kurt; the retail's disabled ones.
            if ((arena.TriangleFlags[t] & (arena.ClipFlag | TriangleGroups.Disabled)) != 0)
            {
                continue;
            }

            var value = group.Material ?? arena.TriangleMaterials[t];
            var surface = _resolver.Resolve(value, arena.Materials, view.Palette, view.Archives, Pass.Solid);
            if (surface is not { } s)
            {
                continue;
            }

            // Details over the surfaces they lie on.
            var material = s.Material with { DepthLayer = view.Layers[t] };
            if (!bySurface.TryGetValue(material, out var entry))
            {
                bySurface[material] = entry = (s.Texture, []);
            }

            entry.Triangles.Add(t);
        }

        var vertices = new List<Vertex>();
        var batches = new List<Batch>();
        foreach (var (material, (texture, triangles)) in bySurface)
        {
            // UVs are in texels of one frame.
            var scale = texture == null ? Vector2.Zero : new Vector2(1f / texture.Width, 1f / texture.Height);
            var first = vertices.Count;
            foreach (var t in triangles)
            {
                for (var k = 0; k < 3; k++)
                {
                    vertices.Add(new Vertex(view.Positions[t * 3 + k], arena.TriangleUvs[t * 3 + k] * scale));
                }
            }

            var animated = texture is { FrameCount: > 1 } ? texture.Name : null;
            batches.Add(new Batch(first, vertices.Count - first, material, animated));
            for (var i = 0; i < triangles.Count; i++)
            {
                view.Placements[triangles[i]] = new Placement(group.Number, first + i * TriangleVertices, material, scale);
            }
        }

        if (vertices.Count == 0)
        {
            return null;
        }

        TriangleCount += vertices.Count / 3;
        AddOutlines(arena, bySurface, vertices, batches);
        Vertex[] array = [.. vertices];
        return new GroupMesh(_renderer.CreateMesh(array), batches, array);
    }
}
