using Mdk.Formats;

namespace Mdk.Game.Mods;

/// <summary>The enabled mods' model replacements for a level (<see cref="ModelSwap"/>): per model,
/// the first mod's <c>models/LEVELn/NAME.glb</c>, else its <c>models/NAME.glb</c> (names ignore case).
/// Read at the level's load; a broken file keeps the original (and says why).</summary>
public sealed class ModModels
{
    public const string FolderName = "models";
    private const string Extension = ".glb";
    private const char Separator = '/';

    private readonly Dictionary<Model, ModelSwap> _swaps = [];
    private readonly List<string> _found = [];

    /// <summary>The models replaced, by name.</summary>
    public IReadOnlyList<string> Found => _found;

    public static ModModels Load(IReadOnlyList<ModFiles> mods, int level, IEnumerable<Model> models)
    {
        var loaded = new ModModels();
        var files = mods.Select(m => Files(m, level)).ToList();
        foreach (var model in models.Distinct())
        {
            var path = files.Select(f => f.TryGetValue(model.Name, out var p) ? p : (ModFile?)null).FirstOrDefault(p => p != null);
            if (path is not { } file || Read(file, model) is not { } swap)
            {
                continue;
            }

            loaded._swaps[model] = swap;
            if (!loaded._found.Contains(model.Name))
            {
                loaded._found.Add(model.Name);
            }
        }

        return loaded;
    }

    public ModelSwap? Of(Model model) => _swaps.GetValueOrDefault(model);

    /// <summary>A mod's models by name: the level's folder first.</summary>
    private static Dictionary<string, ModFile> Files(ModFiles mod, int level)
    {
        var files = new Dictionary<string, ModFile>(StringComparer.OrdinalIgnoreCase);
        var all = mod.List(FolderName).Where(f => f.EndsWith(Extension, StringComparison.OrdinalIgnoreCase)).ToList();
        foreach (var directory in new[] { FolderName + Separator + ModImages.LevelFolder(level), FolderName })
        {
            // Directly in that folder: "models/LEVEL3/XG.glb", then "models/XG.glb".
            foreach (var path in all.Where(f => string.Equals(f[..f.LastIndexOf(Separator)], directory, StringComparison.OrdinalIgnoreCase)))
            {
                files.TryAdd(Path.GetFileNameWithoutExtension(path), new ModFile(mod, path));
            }
        }

        return files;
    }

    private static ModelSwap? Read(ModFile path, Model model)
    {
        try
        {
            var unknown = new List<string>();
            var swap = ModelSwap.Of(Glb.Read(path.Read()), model, unknown);
            if (unknown.Count > 0)
            {
                Console.Error.WriteLine($"Mod model {path}: no part {string.Join(", ", unknown)} in {model.Name}");
            }

            return swap;
        }
        catch (Exception e) when (e is InvalidDataException or IOException)
        {
            Console.Error.WriteLine($"Mod model {path}: {e.Message}");
            return null;
        }
    }
}
