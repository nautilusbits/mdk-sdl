using System.IO.Compression;

namespace Mdk.Game.Mods;

/// <summary>A mod that is a zip (<c>mods/walls.zip</c>): <c>mod.txt</c> at its root, or in its one
/// top folder (a zipped folder: that folder is the mod's top). Untrusted: entries are read in
/// memory, never extracted; those leaving the mod (<c>..</c>, absolute, drive letters,
/// backslashes) are ignored; entries too large or inflating far beyond their packed size (zip
/// bombs) are refused, a zip too large as a whole too. Paths ignore case. The zip stays open
/// after the first read, until <see cref="Close"/>.</summary>
internal sealed class ZipFiles : ModFiles
{
    public const string Extension = ".zip";
    /// <summary>Any one entry, unpacked (an 8K × 8K RGBA image is 256 MB unpacked, less as PNG).</summary>
    private const long MaxEntry = 256L << 20;
    /// <summary>All entries, unpacked.</summary>
    private const long MaxTotal = 2L << 30;
    private const int MaxEntries = 100_000;
    /// <summary>Unpacked to packed, above <see cref="RatioFrom"/> (PNGs and models barely pack).</summary>
    private const long MaxRatio = 100;
    private const long RatioFrom = 1L << 20;
    private const char Separator = '/';

    private readonly string _path;
    /// <summary>The mod's paths to the entries' names.</summary>
    private readonly Dictionary<string, string> _entries;
    private readonly Lock _lock = new();
    private ZipArchive? _archive;

    private ZipFiles(string path, Dictionary<string, string> entries) : base(Path.GetFileNameWithoutExtension(path))
    {
        _path = path;
        _entries = entries;
    }

    public static ZipFiles? Open(string path)
    {
        try
        {
            using var archive = OpenRead(path);
            return Index(path, archive) is { } entries ? new ZipFiles(path, entries) : null;
        }
        catch (Exception e) when (e is InvalidDataException or IOException or UnauthorizedAccessException)
        {
            Console.Error.WriteLine($"Mod {Path.GetFileName(path)}: {e.Message}");
            return null;
        }
    }

    /// <summary>The mod's paths to the entries' names, or null (refused).</summary>
    private static Dictionary<string, string>? Index(string path, ZipArchive archive)
    {
        var zip = Path.GetFileName(path);
        if (archive.Entries.Count > MaxEntries)
        {
            Console.Error.WriteLine($"Mod {zip}: over {MaxEntries} entries, ignored");
            return null;
        }

        // Directories aside; entries leaving the mod ignored.
        var files = archive.Entries.Where(e => !e.FullName.EndsWith(Separator)).ToList();
        var safe = files.Where(e => Safe(e.FullName)).ToList();
        if (safe.Count < files.Count)
        {
            Console.Error.WriteLine($"Mod {zip}: {files.Count - safe.Count} entries outside the mod ignored");
        }

        var top = Top(safe);
        var entries = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var total = 0L;
        foreach (var entry in safe.Where(e => e.FullName.StartsWith(top, StringComparison.OrdinalIgnoreCase)))
        {
            if (Refusal(entry) is { } why)
            {
                Console.Error.WriteLine($"Mod {zip}: {entry.FullName} {why}, ignored");
                continue;
            }

            total += entry.Length;
            entries.TryAdd(entry.FullName[top.Length..], entry.FullName);
        }

        if (total > MaxTotal)
        {
            Console.Error.WriteLine($"Mod {zip}: {total >> 20} MB unpacked, over {MaxTotal >> 20} MB, ignored");
            return null;
        }

        return entries;
    }

    /// <summary>The mod's top in the zip: "" (mod.txt at the root, or none), else "walls/" (the
    /// one top folder holding mod.txt).</summary>
    private static string Top(List<ZipArchiveEntry> entries)
    {
        var names = entries.Select(e => e.FullName).ToList();
        if (names.Contains(ModInfo.FileName, StringComparer.OrdinalIgnoreCase))
        {
            return "";
        }

        var tops = names.Select(n => n.IndexOf(Separator) is var at and > 0 ? n[..(at + 1)] : "")
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
        if (tops.Count != 1 || tops[0].Length == 0)
        {
            return "";
        }

        return names.Contains(tops[0] + ModInfo.FileName, StringComparer.OrdinalIgnoreCase) ? tops[0] : "";
    }

    /// <summary>A relative path that stays in the mod: "textures/WALL.png", not "../x", "/x",
    /// "C:/x", "a\b", "a/./b" or "a//b".</summary>
    private static bool Safe(string name)
    {
        if (name.Length == 0 || name[0] == Separator || name.Contains('\\') || name.Contains(':') || name.Any(char.IsControl))
        {
            return false;
        }

        return name.Split(Separator).All(part => part is not ("" or "." or ".."));
    }

    /// <summary>Why an entry is refused (too large, a bomb), or null.</summary>
    private static string? Refusal(ZipArchiveEntry entry)
    {
        if (entry.Length > MaxEntry)
        {
            return $"is {entry.Length >> 20} MB unpacked, over {MaxEntry >> 20} MB";
        }

        if (entry.Length > RatioFrom && entry.Length > entry.CompressedLength * MaxRatio)
        {
            return $"unpacks over {MaxRatio} times its size";
        }

        return null;
    }

    public override bool Has(string path) => _entries.ContainsKey(path);

    public override IEnumerable<string> List(string folder)
    {
        var prefix = folder + Separator;
        return _entries.Keys.Where(k => k.StartsWith(prefix, StringComparison.OrdinalIgnoreCase));
    }

    /// <summary>An entry's bytes, never more than it declares (checked again: the zip may have changed).</summary>
    public override byte[] Read(string path)
    {
        if (!_entries.TryGetValue(path, out var name))
        {
            throw new FileNotFoundException($"no {path}", Where(path));
        }

        lock (_lock)
        {
            _archive ??= Reopen();
            var entry = _archive.GetEntry(name) ?? throw new FileNotFoundException($"no {path}", Where(path));
            if (Refusal(entry) is { } why)
            {
                throw new InvalidDataException(why);
            }

            var bytes = new byte[entry.Length];
            using var stream = entry.Open();
            stream.ReadExactly(bytes);
            if (stream.ReadByte() >= 0)
            {
                throw new InvalidDataException("unpacks larger than it declares");
            }

            return bytes;
        }
    }

    /// <summary>The zip again (after <see cref="Close"/>); no access is an <see cref="IOException"/>, as a folder's.</summary>
    private ZipArchive Reopen()
    {
        try
        {
            return OpenRead(_path);
        }
        catch (UnauthorizedAccessException e)
        {
            throw new IOException(e.Message, e);
        }
    }

    /// <summary>Open for reading; the user may still delete or replace the file.</summary>
    private static ZipArchive OpenRead(string path)
    {
        var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        try
        {
            return new ZipArchive(stream, ZipArchiveMode.Read);
        }
        catch
        {
            stream.Dispose();
            throw;
        }
    }

    public override string Where(string path) => $"{_path}:{path}";

    public override void Close()
    {
        lock (_lock)
        {
            _archive?.Dispose();
            _archive = null;
        }
    }
}
