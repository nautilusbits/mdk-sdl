namespace Mdk.Formats.Tests;

/// <summary>Copying an installation the user put somewhere (Android: a folder picked on the phone)
/// into a folder of the program's own.</summary>
public sealed class ImportTests : IDisposable
{
    private readonly string _temp = Directory.CreateTempSubdirectory("mdk-import-").FullName;

    public void Dispose() => Directory.Delete(_temp, recursive: true);

    /// <summary>A file of <paramref name="relative"/> under the temp folder, its text its path.</summary>
    private string Put(string relative)
    {
        var path = Path.Combine(_temp, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, relative);
        return path;
    }

    /// <summary>An installation as copied from a CD or a store, in any case, with files the game doesn't read.</summary>
    private FolderTree Installation(string under = "")
    {
        Put(Path.Combine(under, "TRAVERSE/travsprt.bni"));
        Put(Path.Combine(under, "TRAVERSE/LEVEL3/LEVEL3O.MTO"));
        Put(Path.Combine(under, "MISC/MDKFONT.FTI"));
        Put(Path.Combine(under, "FALL3D/FALL3D.BNI"));
        Put(Path.Combine(under, "STREAM/STREAM.MTO"));
        Put(Path.Combine(under, "MDK95.EXE"));
        Put(Path.Combine(under, "Saves/LASTGAME.SAV"));
        return new FolderTree(_temp);
    }

    [Fact]
    public void TheInstallationIsThePickedFolder() =>
        Assert.Equal("", MdkData.FindIn(Installation()));

    [Fact]
    public void TheInstallationIsAFolderOfThePickedOne() =>
        Assert.Equal("MDK", MdkData.FindIn(Installation("MDK")));

    [Fact]
    public void AFolderWithoutTheGameIsNoInstallation()
    {
        Put("Music/song.mp3");
        Put("TRAVERSE/other.bni");
        Assert.Null(MdkData.FindIn(new FolderTree(_temp)));
    }

    [Fact]
    public void TheGameFoldersAreCopied()
    {
        var tree = Installation("MDK");
        var target = Path.Combine(_temp, "copy");
        var steps = new List<(int Done, int Total)>();

        var data = MdkData.Import(tree, "MDK", target, (done, total) => steps.Add((done, total)));

        Assert.EndsWith("LEVEL3O.MTO", File.ReadAllText(data.PathOf("TRAVERSE/LEVEL3/LEVEL3O.MTO")));
        Assert.True(File.Exists(data.PathOf("TRAVERSE/TRAVSPRT.BNI")));
        Assert.True(File.Exists(data.PathOf("MISC/MDKFONT.FTI")));
        Assert.False(File.Exists(Path.Combine(target, "MDK95.EXE")));
        Assert.False(Directory.Exists(Path.Combine(target, "Saves")));
        Assert.Equal((5, 5), steps[^1]);
        Assert.NotNull(MdkData.Imported(target));
    }

    [Fact]
    public void AFolderBesideTheGameIsCopiedElsewhere()
    {
        var tree = Installation("MDK");
        Put("MDK/textures-hd/manifest.txt");
        Put("MDK/textures-hd/LEVEL3/WALL_1.png");
        var target = Path.Combine(_temp, "user", "textures-hd");

        var copied = MdkData.CopyFolder(tree, "MDK", "TEXTURES-HD", target, null);

        Assert.Equal(2, copied);
        Assert.EndsWith("manifest.txt", File.ReadAllText(Path.Combine(target, "manifest.txt")));
        Assert.True(File.Exists(Path.Combine(target, "LEVEL3", "WALL_1.png")));
    }

    /// <summary>Mods come along as they are: folders and zips.</summary>
    [Fact]
    public void ZippedModsAreCopied()
    {
        var tree = Installation("MDK");
        Put("MDK/mods/walls/mod.txt");
        Put("MDK/mods/hud.zip");
        var target = Path.Combine(_temp, "user", "mods");

        Assert.Equal(2, MdkData.CopyFolder(tree, "MDK", "mods", target, null));
        Assert.EndsWith("hud.zip", File.ReadAllText(Path.Combine(target, "hud.zip")));
        Assert.True(File.Exists(Path.Combine(target, "walls", "mod.txt")));
    }

    [Fact]
    public void AMissingFolderCopiesNothing()
    {
        var tree = Installation("MDK");
        var target = Path.Combine(_temp, "user", "textures-hd");

        Assert.Equal(0, MdkData.CopyFolder(tree, "MDK", "textures-hd", target, null));
        Assert.False(Directory.Exists(target));
    }

    [Fact]
    public void AnImportAgainCopiesOnlyChangedFiles()
    {
        var tree = Installation("MDK");
        var target = Path.Combine(_temp, "copy");
        var data = MdkData.Import(tree, "MDK", target, null);

        // Same size: kept; another size: copied again.
        var kept = data.PathOf("MISC/MDKFONT.FTI");
        File.WriteAllText(kept, "MDK/MISC/MDKFONT.FTX");
        var changed = data.PathOf("STREAM/STREAM.MTO");
        File.WriteAllText(changed, "old");
        var steps = new List<(int Done, int Total)>();

        MdkData.Import(tree, "MDK", target, (done, total) => steps.Add((done, total)));

        Assert.Equal("MDK/MISC/MDKFONT.FTX", File.ReadAllText(kept));
        Assert.EndsWith("STREAM.MTO", File.ReadAllText(changed));
        Assert.Equal((5, 5), steps[^1]);
    }

    [Fact]
    public void AnInterruptedCopyIsNoInstallation()
    {
        Put("copy/TRAVERSE/TRAVSPRT.BNI");
        Assert.Null(MdkData.Imported(Path.Combine(_temp, "copy")));
    }

    [Fact]
    public void NoCopyIsNoInstallation() => Assert.Null(MdkData.Imported(Path.Combine(_temp, "none")));

    /// <summary>A tree of the file system, as Android's picked folder would be.</summary>
    private sealed class FolderTree(string root) : IDataTree
    {
        public IReadOnlyList<DataEntry> List(string folder)
        {
            var dir = Path.Combine(root, folder);
            var folders = Directory.GetDirectories(dir).Select(d => new DataEntry(Path.GetFileName(d), EntryKind.Folder));
            var files = Directory.GetFiles(dir).Select(f => new DataEntry(Path.GetFileName(f), EntryKind.File, new FileInfo(f).Length));
            return [.. folders, .. files];
        }

        public Stream Open(string file) => File.OpenRead(Path.Combine(root, file));
    }
}
