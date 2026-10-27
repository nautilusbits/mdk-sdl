using Mdk.Game.Flow;

namespace Mdk.Game.Mods;

/// <summary>A mod switched on or off in the settings (<c>mod.&lt;folder&gt;=Off</c>).</summary>
public enum ModState { On, Off }

/// <summary>The mods in the user folder's <c>mods/</c>: each subfolder or zip is one (hidden ones
/// aren't; a folder wins over a zip of its name), ordered by priority (highest first), then by
/// folder name. The first enabled mod with a file for something wins. New mods are on.
/// <code>
///   mods/
///     hd-textures/      mod.txt  textures/...           (made by "Make HD textures")
///     clean-walls/      mod.txt  textures/LEVEL3/...  images/...  models/...
///     clean-walls.zip   ignored: the folder wins
///     hud.zip           mod.txt  images/...  (or hud/mod.txt  hud/images/...)
/// </code></summary>
public sealed class ModCatalog
{
    public const string FolderName = "mods";
    private const char Hidden = '.';

    /// <summary>The mods' files by folder name.</summary>
    private readonly Dictionary<string, ModFiles> _files;

    private ModCatalog(Dictionary<string, ModFiles> files, List<ModInfo> mods)
    {
        _files = files;
        Mods = mods;
    }

    /// <summary>Every mod found, in order.</summary>
    public IReadOnlyList<ModInfo> Mods { get; }

    /// <summary>The mods folder in a user folder.</summary>
    public static string FolderIn(string userFolder) => Path.Combine(userFolder, FolderName);

    public static ModCatalog Scan(string folder)
    {
        if (!Directory.Exists(folder))
        {
            return new ModCatalog([], []);
        }

        var files = new Dictionary<string, ModFiles>(StringComparer.OrdinalIgnoreCase);
        foreach (var directory in Directory.GetDirectories(folder).Where(d => !Hides(d)))
        {
            files.TryAdd(Path.GetFileName(directory), ModFiles.Folder(directory));
        }

        AddZips(folder, files);
        var mods = files.Values
            .Select(ModInfo.Load)
            .OrderByDescending(m => m.Priority)
            .ThenBy(m => m.Folder, StringComparer.OrdinalIgnoreCase)
            .ToList();
        return new ModCatalog(files, mods);
    }

    /// <summary>The zips of the mods folder, after the folders: a folder of the name wins.</summary>
    private static void AddZips(string folder, Dictionary<string, ModFiles> files)
    {
        var zips = Directory.GetFiles(folder)
            .Where(f => !Hides(f) && f.EndsWith(ZipFiles.Extension, StringComparison.OrdinalIgnoreCase))
            .Order(StringComparer.Ordinal);
        foreach (var zip in zips)
        {
            var name = Path.GetFileNameWithoutExtension(zip);
            if (files.ContainsKey(name))
            {
                Console.Error.WriteLine($"Mod {name}: the folder wins over {Path.GetFileName(zip)}");
                continue;
            }

            if (ModFiles.Zip(zip) is { } mod)
            {
                files[name] = mod;
            }
        }
    }

    private static bool Hides(string path) => Path.GetFileName(path).StartsWith(Hidden);

    /// <summary>The mods the settings leave on, in order.</summary>
    public IReadOnlyList<ModInfo> Enabled(Settings settings) => [.. Mods.Where(m => settings.StateOf(m.Folder) == ModState.On)];

    /// <summary>The enabled mods' files, in order.</summary>
    public IReadOnlyList<ModFiles> Files(Settings settings) => [.. Enabled(settings).Select(m => _files[m.Folder])];

    /// <summary>The enabled mods' images for a level (null: the menus).</summary>
    public ModImages ModImages(Settings settings, int? level) => Mdk.Game.Mods.ModImages.Open(Files(settings), level);

    /// <summary>Lets go of the mods' open zips (a scan again replaces this catalogue).</summary>
    public void Close()
    {
        foreach (var files in _files.Values)
        {
            files.Close();
        }
    }

    /// <summary>Only these mods on (--mod; tests), the others off.</summary>
    public void Only(Settings settings, IReadOnlyCollection<string> folders)
    {
        foreach (var mod in Mods)
        {
            var on = folders.Contains(mod.Folder, StringComparer.OrdinalIgnoreCase);
            settings.Mods[mod.Folder] = on ? ModState.On : ModState.Off;
        }
    }
}
