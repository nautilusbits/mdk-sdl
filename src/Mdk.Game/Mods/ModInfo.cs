using System.Globalization;

namespace Mdk.Game.Mods;

/// <summary>A mod's <c>mod.txt</c>: <c>name=value</c> lines (<c>#</c> comments); all optional.
/// The name defaults to the folder's; a higher priority wins over lower ones (default 0).
/// <code>
///   name=Clean walls
///   author=Me
///   version=1.0
///   description=Cleaner walls for LEVEL3
///   priority=10
/// </code></summary>
public sealed record ModInfo(string Folder, string Name, string Author, string Version, string Description, int Priority)
{
    public const string FileName = "mod.txt";
    private const char Comment = '#';

    /// <summary>The mod of a folder or zip (its <c>mod.txt</c>, if any).</summary>
    public static ModInfo Load(ModFiles files)
    {
        try
        {
            return Parse(files.Name, files.Has(FileName) ? Text(files.Read(FileName)) : "");
        }
        catch (Exception e) when (e is IOException or InvalidDataException)
        {
            Console.Error.WriteLine($"Mod {files.Name}: {e.Message}");
            return Parse(files.Name, "");
        }
    }

    /// <summary>Text as <see cref="File.ReadAllText(string)"/> reads it (UTF-8, a BOM's encoding).</summary>
    private static string Text(byte[] bytes)
    {
        using var reader = new StreamReader(new MemoryStream(bytes));
        return reader.ReadToEnd();
    }

    public static ModInfo Parse(string folder, string text)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var line in text.Split('\n'))
        {
            var parts = line.Trim().Split('=', 2);
            if (parts.Length != 2 || parts[0].StartsWith(Comment))
            {
                continue;
            }

            values[parts[0].Trim()] = parts[1].Trim();
        }

        var priority = int.TryParse(values.GetValueOrDefault("priority"), CultureInfo.InvariantCulture, out var p) ? p : 0;
        var name = values.GetValueOrDefault("name", "");
        return new ModInfo(folder, name.Length != 0 ? name : folder, values.GetValueOrDefault("author", ""),
            values.GetValueOrDefault("version", ""), values.GetValueOrDefault("description", ""), priority);
    }

    /// <summary>The <c>mod.txt</c> text (a generated mod's).</summary>
    public string Format() => string.Create(CultureInfo.InvariantCulture,
        $"name={Name}\nauthor={Author}\nversion={Version}\ndescription={Description}\npriority={Priority}\n");
}
