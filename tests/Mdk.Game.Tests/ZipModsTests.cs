using System.Buffers.Binary;
using System.IO.Compression;
using Mdk.Formats;
using Mdk.Game.Flow;
using Mdk.Game.Mods;

namespace Mdk.Game.Tests;

/// <summary>Mods packed as zips: read like folders (root or one top folder), never outside the
/// mod, with entries too large or too compressed (zip bombs) refused.</summary>
public class ZipModsTests : IDisposable
{
    private const int Rgba = 4;
    /// <summary>The central directory's file header and its uncompressed size's offset.</summary>
    private const uint CentralHeader = 0x02014b50;
    private const int CentralSize = 24;

    private readonly string _folder = Directory.CreateTempSubdirectory("mdk-zip-mods-").FullName;

    /// <summary>What the tests opened: closed before the folder goes (Windows keeps open files).</summary>
    private readonly List<Action> _close = [];

    public void Dispose()
    {
        _close.ForEach(close => close());
        Directory.Delete(_folder, recursive: true);
    }

    private ModFiles? Open(string path)
    {
        var files = ModFiles.Zip(path);
        _close.Add(() => files?.Close());
        return files;
    }

    private ModCatalog Scan()
    {
        var catalog = ModCatalog.Scan(_folder);
        _close.Add(catalog.Close);
        return catalog;
    }

    /// <summary>A zip in the mods folder of these entries (name, bytes).</summary>
    private string Zip(string name, params (string Name, byte[] Bytes)[] entries)
    {
        var path = Path.Combine(_folder, name);
        using var archive = ZipFile.Open(path, ZipArchiveMode.Create);
        foreach (var (entryName, bytes) in entries)
        {
            using var stream = archive.CreateEntry(entryName, CompressionLevel.NoCompression).Open();
            stream.Write(bytes);
        }

        return path;
    }

    private static byte[] Text(string text) => System.Text.Encoding.UTF8.GetBytes(text);

    private static byte[] Image(byte red)
    {
        var rgba = new byte[2 * 2 * Rgba];
        for (var i = 0; i < rgba.Length; i += Rgba)
        {
            (rgba[i], rgba[i + 3]) = (red, byte.MaxValue);
        }

        return Png.Encode(2, 2, rgba, Png.Channels.Rgba);
    }

    private static Texture Texture(string name) =>
        new() { Name = name, Width = 2, Height = 2, FrameCount = 1, Indices = [1, 1, 1, 1] };

    /// <summary>Sets the first entry's uncompressed size in the central directory (a lying zip).</summary>
    private static void DeclareSize(string path, uint size)
    {
        var bytes = File.ReadAllBytes(path);
        for (var i = 0; i + sizeof(uint) <= bytes.Length; i++)
        {
            if (BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(i)) != CentralHeader)
            {
                continue;
            }

            BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(i + CentralSize), size);
            File.WriteAllBytes(path, bytes);
            return;
        }

        throw new InvalidDataException("no central directory");
    }

    /// <summary>mod.txt at the root: the mod is the zip's name, its files by path, case ignored.</summary>
    [Fact]
    public void ReadsTheRoot()
    {
        Zip("walls.zip", ("mod.txt", Text("name=Clean walls\npriority=3\n")), ("textures/WALL.png", Image(7)));

        var catalog = Scan();

        var mod = Assert.Single(catalog.Mods);
        Assert.Equal(("walls", "Clean walls", 3), (mod.Folder, mod.Name, mod.Priority));
        var files = Assert.Single(catalog.Files(new Settings()));
        Assert.True(files.Has("TEXTURES/wall.PNG"));
        Assert.Equal(["textures/WALL.png"], files.List("textures"));
        var image = ModImages.Open([files], 3).Texture("WALL", Texture("WALL"), new Palette());
        Assert.Equal(7, image!.Rgba[0]);
    }

    /// <summary>A zip of a folder (one top folder holding mod.txt) reads as that folder.</summary>
    [Fact]
    public void ReadsOneTopFolder()
    {
        var path = Zip("walls.zip", ("walls/mod.txt", Text("name=Nested\n")), ("walls/textures/LEVEL3/WALL.png", Image(1)));

        var files = Open(path)!;

        Assert.Equal("walls", files.Name);
        Assert.True(files.Has("mod.txt"));
        Assert.Equal(["textures/LEVEL3/WALL.png"], files.List("textures"));
        Assert.Equal("Nested", ModInfo.Load(files).Name);
    }

    /// <summary>Without mod.txt in it, a lone top folder is the mod's own (textures/).</summary>
    [Fact]
    public void KeepsATopFolderWithoutModInfo()
    {
        var path = Zip("walls.zip", ("textures/WALL.png", Image(1)));

        Assert.Equal(["textures/WALL.png"], Open(path)!.List("textures"));
    }

    /// <summary>Entries that would leave the mod are ignored.</summary>
    [Fact]
    public void IgnoresPathsOutside()
    {
        var path = Zip("evil.zip",
            ("../textures/A.png", Image(1)), ("/textures/B.png", Image(1)), ("C:/textures/C.png", Image(1)),
            ("textures\\D.png", Image(1)), ("textures/../../E.png", Image(1)), ("textures/./F.png", Image(1)),
            ("textures//G.png", Image(1)), ("textures/OK.png", Image(1)));

        var files = Open(path)!;

        Assert.Equal(["textures/OK.png"], files.List("textures"));
        Assert.False(files.Has("../textures/A.png"));
        Assert.Throws<FileNotFoundException>(() => files.Read("textures/../../E.png"));
    }

    /// <summary>An entry larger than the limit, or one inflating far beyond its packed size (a zip
    /// bomb), is refused; the others stay.</summary>
    [Fact]
    public void RefusesOversizedEntries()
    {
        var big = Zip("big.zip", ("textures/BIG.png", Image(1)), ("textures/OK.png", Image(2)));
        DeclareSize(big, int.MaxValue);
        Assert.Equal(["textures/OK.png"], Open(big)!.List("textures"));

        var bomb = Path.Combine(_folder, "bomb.zip");
        using (var archive = ZipFile.Open(bomb, ZipArchiveMode.Create))
        {
            using var stream = archive.CreateEntry("textures/BOMB.png", CompressionLevel.Optimal).Open();
            stream.Write(new byte[8 << 20]);
        }

        Assert.Empty(Open(bomb)!.List("textures"));
    }

    /// <summary>An entry inflating beyond the size it declares is refused when read.</summary>
    [Fact]
    public void RefusesEntriesLargerThanDeclared()
    {
        var path = Zip("liar.zip", ("textures/WALL.png", Image(1)));
        DeclareSize(path, 4);

        var files = Open(path)!;

        Assert.ThrowsAny<Exception>(() => files.Read("textures/WALL.png"));
        Assert.Null(ModImages.Open([files], 3).Texture("WALL", Texture("WALL"), new Palette()));
    }

    /// <summary>A broken zip is no mod (and no crash).</summary>
    [Fact]
    public void SkipsABrokenZip()
    {
        File.WriteAllText(Path.Combine(_folder, "broken.zip"), "not a zip");

        Assert.Empty(Scan().Mods);
    }

    /// <summary>A folder and a zip of one name: the folder wins; the switch is the name's.</summary>
    [Fact]
    public void FolderWinsOverZip()
    {
        Zip("walls.zip", ("mod.txt", Text("name=Zipped\n")));
        Zip("other.ZIP", ("mod.txt", Text("name=Other\n")));
        Zip(".hidden.zip", ("mod.txt", Text("name=Hidden\n")));
        var folder = Directory.CreateDirectory(Path.Combine(_folder, "walls")).FullName;
        File.WriteAllText(Path.Combine(folder, ModInfo.FileName), "name=Folder\n");
        var settings = new Settings();
        settings.Mods["other"] = ModState.Off;

        var catalog = Scan();

        Assert.Equal(["Other", "Folder"], catalog.Mods.Select(m => m.Name));
        Assert.Equal(["walls"], catalog.Enabled(settings).Select(m => m.Folder));
    }
}
