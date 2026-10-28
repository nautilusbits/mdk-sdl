namespace Mdk.Game.Mods;

/// <summary>A mod's files, by '/'-separated path from the mod's top (<c>textures/LEVEL3/WALL.png</c>):
/// a folder of <c>mods/</c>, or a zip (<see cref="ZipFiles"/>). Reading may run on every core.
/// <code>
///   mods/walls/textures/WALL.png  ─┐
///   mods/walls.zip:textures/WALL.png ─┴─► "textures/WALL.png"
/// </code></summary>
public abstract class ModFiles
{
    private protected ModFiles(string name)
    {
        Name = name;
    }

    /// <summary>The mod's name in the settings (the folder's, the zip's without .zip).</summary>
    public string Name { get; }

    public abstract bool Has(string path);

    /// <summary>The files under a folder, at any depth.</summary>
    public abstract IEnumerable<string> List(string folder);

    /// <summary>A file's bytes; <see cref="IOException"/> or <see cref="InvalidDataException"/> when it can't.</summary>
    public abstract byte[] Read(string path);

    /// <summary>A file's place, for messages.</summary>
    public abstract string Where(string path);

    /// <summary>Lets go of open files (a later read opens them again).</summary>
    public virtual void Close()
    {
    }

    public static ModFiles Folder(string folder) => new FolderFiles(folder);

    /// <summary>A zip's files, or null (not a zip, or refused: says why).</summary>
    public static ModFiles? Zip(string path) => ZipFiles.Open(path);
}

/// <summary>One file of a mod (a key: same mod, same path).</summary>
internal readonly record struct ModFile(ModFiles Mod, string Path)
{
    public byte[] Read() => Mod.Read(Path);

    public override string ToString() => Mod.Where(Path);
}

/// <summary>A mod that is a folder.</summary>
internal sealed class FolderFiles(string folder) : ModFiles(Path.GetFileName(folder))
{
    private const char Separator = '/';

    private readonly string _root = Path.GetFullPath(folder);

    public override bool Has(string path) => File.Exists(Where(path));

    public override IEnumerable<string> List(string folder)
    {
        var directory = Where(folder);
        if (!Directory.Exists(directory))
        {
            return [];
        }

        return Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories)
            .Select(f => Path.GetRelativePath(_root, f).Replace(Path.DirectorySeparatorChar, Separator));
    }

    public override byte[] Read(string path)
    {
        try
        {
            return File.ReadAllBytes(Where(path));
        }
        catch (UnauthorizedAccessException e)
        {
            throw new IOException(e.Message, e);
        }
    }

    public override string Where(string path) => Path.Combine(_root, path.Replace(Separator, Path.DirectorySeparatorChar));
}
