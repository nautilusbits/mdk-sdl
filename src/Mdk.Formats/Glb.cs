using System.Buffers.Binary;
using System.Numerics;
using System.Text.Json;

namespace Mdk.Formats;

/// <summary>glTF 2.0 binary files (.glb), the subset mods use: named nodes with triangle meshes
/// (positions, first UVs, indices), materials of a base colour and an embedded PNG. Read: node
/// transforms (matrix or TRS, parents' included) are applied, normals and the rest ignored; written:
/// one node per mesh, no transform. Coordinates stay glTF's (Y up); callers convert.
/// <code>
///   "glTF" u32 version 2, u32 length │ u32 length "JSON" text │ u32 length "BIN\0" data   (little-endian, 4-aligned)
///   node ─► mesh ─► primitives ─► accessors (POSITION, TEXCOORD_0, indices) ─► bufferViews ─► BIN
///                              └► material ─► pbrMetallicRoughness: baseColorFactor, baseColorTexture ─► image (PNG in a bufferView)
/// </code></summary>
public static class Glb
{
    /// <summary>A material: its name, base colour (linear RGBA as written) and PNG, if any.</summary>
    public sealed record Material(string Name, Vector4 Colour, byte[]? Png);

    /// <summary>Triangles of one material (-1: none).</summary>
    public sealed record Primitive(Vector3[] Positions, Vector2[] Uvs, int[] Indices, int Material);

    /// <summary>A node's mesh, named after the node (else the mesh).</summary>
    public sealed record Mesh(string Name, List<Primitive> Primitives);

    public sealed record Scene(List<Mesh> Meshes, List<Material> Materials);

    private const uint Magic = 0x46546C67;
    private const uint Version = 2;
    private const uint JsonChunk = 0x4E4F534A;
    private const uint BinChunk = 0x004E4942;
    private const int HeaderSize = 12;
    private const int ChunkHeader = 8;
    private const int Alignment = 4;

    /// <summary>Accessor component types.</summary>
    private const int UnsignedByte = 5121;
    private const int UnsignedShort = 5123;
    private const int UnsignedInt = 5125;
    private const int Float = 5126;

    private const int Triangles = 4;
    private const int ArrayBuffer = 34962;
    private const int ElementArrayBuffer = 34963;
    private const string PngType = "image/png";

    // --- Reading ---------------------------------------------------------------------------------

    /// <summary>A file's meshes; <see cref="InvalidDataException"/> for any broken one (a mod's
    /// file is untrusted: indices past the arrays, numbers out of range).</summary>
    public static Scene Read(ReadOnlySpan<byte> bytes)
    {
        try
        {
            return Parse(bytes);
        }
        catch (Exception e) when (e is ArgumentException or FormatException or InvalidOperationException
            or IndexOutOfRangeException or OverflowException or KeyNotFoundException)
        {
            throw new InvalidDataException($"Broken glTF: {e.Message}", e);
        }
    }

    private static Scene Parse(ReadOnlySpan<byte> bytes)
    {
        var (json, bin) = Chunks(bytes);
        using var document = ParseJson(json);
        var root = document.RootElement;
        if (Text(Get(root, "asset"), "version") is not { } version || !version.StartsWith("2."))
        {
            throw new InvalidDataException("Not glTF 2.0");
        }

        var file = new Reader(root, bin);
        var materials = Array(root, "materials").Select(file.Material).ToList();
        var meshes = new List<Mesh>();
        foreach (var (node, world) in file.Nodes())
        {
            if (Int(node, "mesh") is not { } index)
            {
                continue;
            }

            var mesh = Array(root, "meshes").ElementAt(index);
            var name = Text(node, "name") ?? Text(mesh, "name") ?? $"mesh{index}";
            meshes.Add(new Mesh(name, [.. Array(mesh, "primitives").Select(p => file.Primitive(p, world))]));
        }

        return new Scene(meshes, materials);
    }

    private static JsonDocument ParseJson(byte[] json)
    {
        try
        {
            return JsonDocument.Parse(json);
        }
        catch (JsonException e)
        {
            throw new InvalidDataException($"glTF JSON: {e.Message}");
        }
    }

    /// <summary>The JSON chunk and the BIN chunk (empty when absent).</summary>
    private static (byte[] Json, byte[] Bin) Chunks(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length < HeaderSize || BinaryPrimitives.ReadUInt32LittleEndian(bytes) != Magic)
        {
            throw new InvalidDataException("Not a glTF binary file");
        }

        if (BinaryPrimitives.ReadUInt32LittleEndian(bytes[4..]) != Version)
        {
            throw new InvalidDataException("Not glTF 2.0");
        }

        byte[]? json = null;
        byte[] bin = [];
        var at = HeaderSize;
        while (at + ChunkHeader <= bytes.Length)
        {
            var length = (int)BinaryPrimitives.ReadUInt32LittleEndian(bytes[at..]);
            var type = BinaryPrimitives.ReadUInt32LittleEndian(bytes[(at + 4)..]);
            if (length < 0 || length > bytes.Length - at - ChunkHeader)
            {
                throw new InvalidDataException("glTF chunk past the end");
            }

            var data = bytes.Slice(at + ChunkHeader, length).ToArray();
            if (type == JsonChunk)
            {
                json = data;
            }
            else if (type == BinChunk)
            {
                bin = data;
            }

            at += ChunkHeader + length;
        }

        return (json ?? throw new InvalidDataException("glTF without JSON"), bin);
    }

    /// <summary>A file's accessors and nodes over its BIN chunk.</summary>
    private sealed class Reader(JsonElement root, byte[] bin)
    {
        /// <summary>Every node under the scene's roots, with its transform to the scene.</summary>
        public IEnumerable<(JsonElement Node, Matrix4x4 World)> Nodes()
        {
            var nodes = Array(root, "nodes").ToList();
            var stack = new Stack<(int, Matrix4x4)>();
            foreach (var index in Roots(nodes).Reverse())
            {
                stack.Push((index, Matrix4x4.Identity));
            }

            var seen = new HashSet<int>();
            while (stack.Count > 0)
            {
                var (index, parent) = stack.Pop();
                if (index < 0 || index >= nodes.Count || !seen.Add(index))
                {
                    continue;
                }

                var node = nodes[index];
                var world = Local(node) * parent;
                yield return (node, world);
                foreach (var child in Array(node, "children").Select(c => c.GetInt32()).Reverse())
                {
                    stack.Push((child, world));
                }
            }
        }

        /// <summary>The default scene's nodes, else every node no other one holds.</summary>
        private IEnumerable<int> Roots(List<JsonElement> nodes)
        {
            var scenes = Array(root, "scenes").ToList();
            if (scenes.Count > 0)
            {
                var scene = scenes[Math.Clamp(Int(root, "scene") ?? 0, 0, scenes.Count - 1)];
                return Array(scene, "nodes").Select(n => n.GetInt32()).ToList();
            }

            var children = nodes.SelectMany(n => Array(n, "children")).Select(c => c.GetInt32()).ToHashSet();
            return Enumerable.Range(0, nodes.Count).Where(i => !children.Contains(i)).ToList();
        }

        /// <summary>A node's transform: its matrix (column-major: System.Numerics' row-vector layout), else scale, rotation, translation.</summary>
        private static Matrix4x4 Local(JsonElement node)
        {
            var m = Floats(node, "matrix");
            if (m.Length == 16)
            {
                return new Matrix4x4(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
            }

            var s = Floats(node, "scale");
            var r = Floats(node, "rotation");
            var t = Floats(node, "translation");
            var scale = s.Length == 3 ? Matrix4x4.CreateScale(s[0], s[1], s[2]) : Matrix4x4.Identity;
            var rotation = r.Length == 4 ? Matrix4x4.CreateFromQuaternion(new Quaternion(r[0], r[1], r[2], r[3])) : Matrix4x4.Identity;
            var translation = t.Length == 3 ? Matrix4x4.CreateTranslation(t[0], t[1], t[2]) : Matrix4x4.Identity;
            return scale * rotation * translation;
        }

        public Primitive Primitive(JsonElement primitive, Matrix4x4 world)
        {
            if ((Int(primitive, "mode") ?? Triangles) != Triangles)
            {
                throw new InvalidDataException("glTF primitive not of triangles");
            }

            var attributes = Get(primitive, "attributes");
            var position = Int(attributes, "POSITION") ?? throw new InvalidDataException("glTF primitive without POSITION");
            var points = Vectors(position, 3);
            var positions = new Vector3[points.Length];
            for (var i = 0; i < points.Length; i++)
            {
                positions[i] = Vector3.Transform(new Vector3(points[i][0], points[i][1], points[i][2]), world);
            }

            var uvs = new Vector2[positions.Length];
            if (Int(attributes, "TEXCOORD_0") is { } texcoord)
            {
                var read = Vectors(texcoord, 2);
                for (var i = 0; i < Math.Min(read.Length, uvs.Length); i++)
                {
                    uvs[i] = new Vector2(read[i][0], read[i][1]);
                }
            }

            var indices = Int(primitive, "indices") is { } accessor
                ? Vectors(accessor, 1).Select(v => (int)v[0]).ToArray()
                : Enumerable.Range(0, positions.Length).ToArray();
            if (indices.Any(i => i < 0 || i >= positions.Length) || indices.Length % 3 != 0)
            {
                throw new InvalidDataException("glTF indices out of range");
            }

            return new Primitive(positions, uvs, indices, Int(primitive, "material") ?? -1);
        }

        public Material Material(JsonElement material)
        {
            var pbr = Get(material, "pbrMetallicRoughness");
            var factor = Floats(pbr, "baseColorFactor");
            var colour = factor.Length == 4 ? new Vector4(factor[0], factor[1], factor[2], factor[3]) : Vector4.One;
            byte[]? png = null;
            if (Int(Get(pbr, "baseColorTexture"), "index") is { } texture
                && Int(Array(root, "textures").ElementAt(texture), "source") is { } source)
            {
                png = Image(Array(root, "images").ElementAt(source));
            }

            return new Material(Text(material, "name") ?? "", colour, png);
        }

        /// <summary>An embedded image's bytes (external files aren't read).</summary>
        private byte[] Image(JsonElement image)
        {
            if (Int(image, "bufferView") is not { } view)
            {
                throw new InvalidDataException("glTF image not embedded");
            }

            if (Text(image, "mimeType") is { } type && type != PngType)
            {
                throw new InvalidDataException($"glTF image of {type}, not PNG");
            }

            var (offset, length, _) = View(view);
            return bin.AsSpan(offset, length).ToArray();
        }

        private (int Offset, int Length, int Stride) View(int index)
        {
            var view = Array(root, "bufferViews").ElementAt(index);
            if ((Int(view, "buffer") ?? 0) != 0)
            {
                throw new InvalidDataException("glTF data outside the GLB's buffer");
            }

            var offset = Int(view, "byteOffset") ?? 0;
            var length = Int(view, "byteLength") ?? 0;
            if (offset < 0 || length < 0 || (long)offset + length > bin.Length)
            {
                throw new InvalidDataException("glTF buffer view past the end");
            }

            return (offset, length, Int(view, "byteStride") ?? 0);
        }

        /// <summary>An accessor's elements of <paramref name="components"/> numbers: floats, or
        /// unsigned integers (normalized when the accessor says so).</summary>
        private float[][] Vectors(int index, int components)
        {
            var accessor = Array(root, "accessors").ElementAt(index);
            if (Get(accessor, "sparse").ValueKind != JsonValueKind.Undefined)
            {
                throw new InvalidDataException("glTF sparse accessors aren't read");
            }

            var count = Int(accessor, "count") ?? 0;
            var type = Int(accessor, "componentType") ?? Float;
            var normalized = Get(accessor, "normalized").ValueKind == JsonValueKind.True;
            var size = type switch
            {
                UnsignedByte => 1,
                UnsignedShort => 2,
                UnsignedInt or Float => 4,
                _ => throw new InvalidDataException($"glTF component type {type}"),
            };
            var (offset, length, stride) = Int(accessor, "bufferView") is { } view ? View(view) : (0, 0, 0);
            offset = checked(offset + (Int(accessor, "byteOffset") ?? 0));
            stride = stride != 0 ? stride : size * components;
            if (count < 0 || offset < 0 || stride < 0
                || (count > 0 && offset + (long)(count - 1) * stride + size * components > bin.Length))
            {
                throw new InvalidDataException("glTF accessor past the end");
            }

            var result = new float[count][];
            for (var i = 0; i < count; i++)
            {
                result[i] = new float[components];
                for (var c = 0; c < components; c++)
                {
                    var at = bin.AsSpan(offset + i * stride + c * size);
                    result[i][c] = type switch
                    {
                        Float => BinaryPrimitives.ReadSingleLittleEndian(at),
                        UnsignedByte => normalized ? at[0] / (float)byte.MaxValue : at[0],
                        UnsignedShort => normalized ? BinaryPrimitives.ReadUInt16LittleEndian(at) / (float)ushort.MaxValue : BinaryPrimitives.ReadUInt16LittleEndian(at),
                        _ => BinaryPrimitives.ReadUInt32LittleEndian(at),
                    };
                }
            }

            return result;
        }
    }

    private static JsonElement Get(JsonElement element, string name) =>
        element.ValueKind == JsonValueKind.Object && element.TryGetProperty(name, out var value) ? value : default;

    private static IEnumerable<JsonElement> Array(JsonElement element, string name) =>
        Get(element, name) is { ValueKind: JsonValueKind.Array } array ? array.EnumerateArray() : [];

    private static int? Int(JsonElement element, string name) =>
        Get(element, name) is { ValueKind: JsonValueKind.Number } number ? number.GetInt32() : null;

    private static string? Text(JsonElement element, string name) =>
        Get(element, name) is { ValueKind: JsonValueKind.String } text ? text.GetString() : null;

    private static float[] Floats(JsonElement element, string name) => [.. Array(element, name).Select(e => e.GetSingle())];

    // --- Writing ---------------------------------------------------------------------------------

    /// <summary>A scene as a GLB: per mesh a node of its name; positions, UVs and u32 indices per
    /// primitive; each material's PNG embedded; both faces shown.</summary>
    public static byte[] Write(Scene scene)
    {
        var bin = new MemoryStream();
        var views = new List<(int Offset, int Length, int? Target)>();
        var accessors = new List<Action<Utf8JsonWriter>>();

        int AddView(ReadOnlySpan<byte> data, int? target)
        {
            while (bin.Length % Alignment != 0)
            {
                bin.WriteByte(0);
            }

            views.Add(((int)bin.Length, data.Length, target));
            bin.Write(data);
            return views.Count - 1;
        }

        int AddAccessor(ReadOnlySpan<byte> data, int count, int componentType, string type, int? target, Vector3? min = null, Vector3? max = null)
        {
            var view = AddView(data, target);
            accessors.Add(w =>
            {
                w.WriteStartObject();
                w.WriteNumber("bufferView", view);
                w.WriteNumber("componentType", componentType);
                w.WriteNumber("count", count);
                w.WriteString("type", type);
                if (min is { } low && max is { } high)
                {
                    WriteVector(w, "min", low);
                    WriteVector(w, "max", high);
                }

                w.WriteEndObject();
            });
            return accessors.Count - 1;
        }

        var json = new MemoryStream();
        using (var w = new Utf8JsonWriter(json))
        {
            w.WriteStartObject();
            w.WriteStartObject("asset");
            w.WriteString("version", "2.0");
            w.WriteString("generator", "mdk-sdl");
            w.WriteEndObject();
            w.WriteNumber("scene", 0);
            w.WriteStartArray("scenes");
            w.WriteStartObject();
            w.WriteStartArray("nodes");
            for (var i = 0; i < scene.Meshes.Count; i++)
            {
                w.WriteNumberValue(i);
            }

            w.WriteEndArray();
            w.WriteEndObject();
            w.WriteEndArray();

            w.WriteStartArray("nodes");
            for (var i = 0; i < scene.Meshes.Count; i++)
            {
                w.WriteStartObject();
                w.WriteString("name", scene.Meshes[i].Name);
                w.WriteNumber("mesh", i);
                w.WriteEndObject();
            }

            w.WriteEndArray();

            w.WriteStartArray("meshes");
            foreach (var mesh in scene.Meshes)
            {
                w.WriteStartObject();
                w.WriteString("name", mesh.Name);
                w.WriteStartArray("primitives");
                foreach (var p in mesh.Primitives)
                {
                    var min = p.Positions.Length == 0 ? Vector3.Zero : p.Positions.Aggregate(Vector3.Min);
                    var max = p.Positions.Length == 0 ? Vector3.Zero : p.Positions.Aggregate(Vector3.Max);
                    var position = AddAccessor(Bytes(p.Positions.SelectMany(v => new[] { v.X, v.Y, v.Z })), p.Positions.Length, Float, "VEC3", ArrayBuffer, min, max);
                    var uv = AddAccessor(Bytes(p.Uvs.SelectMany(v => new[] { v.X, v.Y })), p.Uvs.Length, Float, "VEC2", ArrayBuffer);
                    var indices = AddAccessor(Bytes(p.Indices), p.Indices.Length, UnsignedInt, "SCALAR", ElementArrayBuffer);
                    w.WriteStartObject();
                    w.WriteStartObject("attributes");
                    w.WriteNumber("POSITION", position);
                    w.WriteNumber("TEXCOORD_0", uv);
                    w.WriteEndObject();
                    w.WriteNumber("indices", indices);
                    if (p.Material >= 0)
                    {
                        w.WriteNumber("material", p.Material);
                    }

                    w.WriteNumber("mode", Triangles);
                    w.WriteEndObject();
                }

                w.WriteEndArray();
                w.WriteEndObject();
            }

            w.WriteEndArray();

            var images = new List<int>();
            w.WriteStartArray("materials");
            foreach (var material in scene.Materials)
            {
                w.WriteStartObject();
                w.WriteString("name", material.Name);
                w.WriteBoolean("doubleSided", true);
                w.WriteStartObject("pbrMetallicRoughness");
                w.WriteStartArray("baseColorFactor");
                foreach (var c in new[] { material.Colour.X, material.Colour.Y, material.Colour.Z, material.Colour.W })
                {
                    w.WriteNumberValue(c);
                }

                w.WriteEndArray();
                if (material.Png is { } png)
                {
                    w.WriteStartObject("baseColorTexture");
                    w.WriteNumber("index", images.Count);
                    w.WriteEndObject();
                    images.Add(AddView(png, null));
                }

                w.WriteNumber("metallicFactor", 0);
                w.WriteNumber("roughnessFactor", 1);
                w.WriteEndObject();
                w.WriteEndObject();
            }

            w.WriteEndArray();

            if (images.Count > 0)
            {
                w.WriteStartArray("textures");
                for (var i = 0; i < images.Count; i++)
                {
                    w.WriteStartObject();
                    w.WriteNumber("source", i);
                    w.WriteEndObject();
                }

                w.WriteEndArray();
                w.WriteStartArray("images");
                foreach (var view in images)
                {
                    w.WriteStartObject();
                    w.WriteNumber("bufferView", view);
                    w.WriteString("mimeType", PngType);
                    w.WriteEndObject();
                }

                w.WriteEndArray();
            }

            w.WriteStartArray("accessors");
            foreach (var accessor in accessors)
            {
                accessor(w);
            }

            w.WriteEndArray();

            while (bin.Length % Alignment != 0)
            {
                bin.WriteByte(0);
            }

            w.WriteStartArray("bufferViews");
            foreach (var (offset, length, target) in views)
            {
                w.WriteStartObject();
                w.WriteNumber("buffer", 0);
                w.WriteNumber("byteOffset", offset);
                w.WriteNumber("byteLength", length);
                if (target is { } t)
                {
                    w.WriteNumber("target", t);
                }

                w.WriteEndObject();
            }

            w.WriteEndArray();
            w.WriteStartArray("buffers");
            w.WriteStartObject();
            w.WriteNumber("byteLength", bin.Length);
            w.WriteEndObject();
            w.WriteEndArray();
            w.WriteEndObject();
        }

        return Pack(json.ToArray(), bin.ToArray());
    }

    private static void WriteVector(Utf8JsonWriter w, string name, Vector3 v)
    {
        w.WriteStartArray(name);
        w.WriteNumberValue(v.X);
        w.WriteNumberValue(v.Y);
        w.WriteNumberValue(v.Z);
        w.WriteEndArray();
    }

    private static byte[] Bytes(IEnumerable<float> values) =>
        [.. values.SelectMany(v => BitConverter.GetBytes(v))];

    private static byte[] Bytes(int[] values) =>
        [.. values.SelectMany(v => BitConverter.GetBytes((uint)v))];

    /// <summary>The header and both chunks, the JSON padded with spaces, the BIN with zeros.</summary>
    private static byte[] Pack(byte[] json, byte[] bin)
    {
        var jsonLength = Aligned(json.Length);
        var total = HeaderSize + ChunkHeader + jsonLength + (bin.Length > 0 ? ChunkHeader + bin.Length : 0);
        var file = new byte[total];
        BinaryPrimitives.WriteUInt32LittleEndian(file, Magic);
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(4), Version);
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(8), (uint)total);
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(HeaderSize), (uint)jsonLength);
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(HeaderSize + 4), JsonChunk);
        file.AsSpan(HeaderSize + ChunkHeader, jsonLength).Fill((byte)' ');
        json.CopyTo(file, HeaderSize + ChunkHeader);
        if (bin.Length == 0)
        {
            return file;
        }

        var at = HeaderSize + ChunkHeader + jsonLength;
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(at), (uint)bin.Length);
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(at + 4), BinChunk);
        bin.CopyTo(file, at + ChunkHeader);
        return file;
    }

    private static int Aligned(int length) => (length + Alignment - 1) / Alignment * Alignment;
}
