using System.Buffers.Binary;
using System.Numerics;
using System.Text;

namespace Mdk.Formats.Tests;

/// <summary>glTF binary (.glb): the subset mods use (named mesh nodes, positions, UVs, indices, a
/// base colour or an embedded PNG per material) reads back what <see cref="Glb.Write"/> wrote, and a
/// file written by hand (another exporter's layout: u16 indices, node transforms, a hierarchy) reads.</summary>
public class GlbTests
{
    private static Glb.Scene Triangle()
    {
        var png = Png.Encode(1, 1, [10, 20, 30, 255], Png.Channels.Rgba);
        var primitive = new Glb.Primitive([new(0, 0, 0), new(1, 0, 0), new(0, 1, 2)], [new(0, 0), new(1, 0), new(0, 1)], [0, 1, 2], 0);
        var flat = new Glb.Primitive([new(5, 5, 5), new(6, 5, 5), new(5, 6, 5)], [new(0, 0), new(0, 0), new(0, 0)], [0, 2, 1], 1);
        return new Glb.Scene(
            [new Glb.Mesh("HEAD", [primitive, flat])],
            [new Glb.Material("WALL", Vector4.One, png), new Glb.Material("PEN_12", new Vector4(0.5f, 0.25f, 1f, 1f), null)]);
    }

    [Fact]
    public void RoundTrips()
    {
        var scene = Triangle();

        var read = Glb.Read(Glb.Write(scene));

        var mesh = Assert.Single(read.Meshes);
        Assert.Equal("HEAD", mesh.Name);
        Assert.Equal(2, mesh.Primitives.Count);
        for (var i = 0; i < 2; i++)
        {
            Assert.Equal(scene.Meshes[0].Primitives[i].Positions, mesh.Primitives[i].Positions);
            Assert.Equal(scene.Meshes[0].Primitives[i].Uvs, mesh.Primitives[i].Uvs);
            Assert.Equal(scene.Meshes[0].Primitives[i].Indices, mesh.Primitives[i].Indices);
            Assert.Equal(scene.Meshes[0].Primitives[i].Material, mesh.Primitives[i].Material);
        }

        Assert.Equal("WALL", read.Materials[0].Name);
        Assert.Equal(scene.Materials[0].Png, read.Materials[0].Png);
        Assert.Equal("PEN_12", read.Materials[1].Name);
        Assert.Null(read.Materials[1].Png);
        Assert.Equal(new Vector4(0.5f, 0.25f, 1f, 1f), read.Materials[1].Colour);
    }

    /// <summary>A root node scaled 2 with a child moved 10 along x: the child's mesh comes out in the
    /// scene's space; u16 indices; no UVs (zero); no material (-1, white).</summary>
    [Fact]
    public void ReadsAnotherExportersFile()
    {
        var bin = new List<byte>();
        foreach (var v in new[] { 0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f, 0f })
        {
            bin.AddRange(BitConverter.GetBytes(v));
        }

        foreach (var i in new ushort[] { 0, 1, 2 })
        {
            bin.AddRange(BitConverter.GetBytes(i));
        }

        bin.AddRange([0, 0]);
        const string json = """
            {"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
             "nodes":[{"name":"root","scale":[2,2,2],"children":[1]},{"name":"ARM","translation":[10,0,0],"mesh":0}],
             "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
             "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
                          {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
             "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
             "buffers":[{"byteLength":44}]}
            """;

        var scene = Glb.Read(Pack(json, [.. bin]));

        var mesh = Assert.Single(scene.Meshes);
        Assert.Equal("ARM", mesh.Name);
        var primitive = Assert.Single(mesh.Primitives);
        Assert.Equal([new(20, 0, 0), new(22, 0, 0), new(20, 2, 0)], primitive.Positions);
        Assert.Equal([0, 1, 2], primitive.Indices);
        Assert.All(primitive.Uvs, uv => Assert.Equal(Vector2.Zero, uv));
        Assert.Equal(-1, primitive.Material);
    }

    [Fact]
    public void RefusesOtherFiles()
    {
        Assert.Throws<InvalidDataException>(() => Glb.Read(Encoding.ASCII.GetBytes("not a glb file at all")));
        Assert.Throws<InvalidDataException>(() => Glb.Read(Pack("""{"asset":{"version":"1.0"}}""", [])));
    }

    /// <summary>Crafted files (a mod from the internet) are refused, never a crash: a chunk's
    /// length near the end of the int, offsets that overflow, indices past the arrays.</summary>
    [Fact]
    public void RefusesCraftedFiles()
    {
        var chunk = Pack("""{"asset":{"version":"2.0"}}""", []);
        BinaryPrimitives.WriteUInt32LittleEndian(chunk.AsSpan(12), int.MaxValue - 8);
        Assert.Throws<InvalidDataException>(() => Glb.Read(chunk));

        const string views = """
            {"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
             "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
             "accessors":[{"bufferView":0,"byteOffset":2147483000,"componentType":5126,"count":3,"type":"VEC3"}],
             "bufferViews":[{"buffer":0,"byteOffset":2147483000,"byteLength":2000}]}
            """;
        Assert.Throws<InvalidDataException>(() => Glb.Read(Pack(views, new byte[36])));

        const string missing = """
            {"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":5}]}
            """;
        Assert.Throws<InvalidDataException>(() => Glb.Read(Pack(missing, [])));

        const string big = """
            {"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":1e20}]}
            """;
        Assert.Throws<InvalidDataException>(() => Glb.Read(Pack(big, [])));
    }

    /// <summary>A GLB of a JSON chunk and a BIN chunk.</summary>
    private static byte[] Pack(string json, byte[] bin)
    {
        var text = Encoding.UTF8.GetBytes(json).ToList();
        while (text.Count % 4 != 0)
        {
            text.Add((byte)' ');
        }

        var file = new List<byte>();
        file.AddRange(Encoding.ASCII.GetBytes("glTF"));
        file.AddRange(BitConverter.GetBytes(2u));
        file.AddRange(BitConverter.GetBytes((uint)(12 + 8 + text.Count + (bin.Length > 0 ? 8 + bin.Length : 0))));
        file.AddRange(BitConverter.GetBytes((uint)text.Count));
        file.AddRange(Encoding.ASCII.GetBytes("JSON"));
        file.AddRange(text);
        if (bin.Length > 0)
        {
            file.AddRange(BitConverter.GetBytes((uint)bin.Length));
            file.AddRange(Encoding.ASCII.GetBytes("BIN\0"));
            file.AddRange(bin);
        }

        var bytes = file.ToArray();
        Assert.Equal(bytes.Length, (int)BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(8)));
        return bytes;
    }
}
