using Mdk.Formats;
using Mdk.Game.HdTextures;

namespace Mdk.Game.Mods;

/// <summary>The enabled mods' images for one level (or the menus: no level): a texture's or a 2D
/// image's replacement, of any size, premultiplied for the renderer (<see cref="HdImage"/>).
/// Mods are searched in order; within one, the exact view first (<c>NAME@key</c>: the texture
/// through that palette, <see cref="HdKey"/>; any subfolder), then the level's folder, then all
/// levels'. Names ignore case. Animated textures: one strip of the frames stacked downwards, or one
/// image per frame (<c>NAME_0</c>, <c>NAME_1</c>...), all of one size. The level's folders are
/// decoded at opening, on every core; the rest when asked.
/// <code>
///   mods/m/textures/WALL@9f3a....png   exact view (HD textures)
///   mods/m/textures/LEVEL3/WALL.png    LEVEL3 only
///   mods/m/textures/WALL.png           every level
///   mods/m/images/SC_STAT.png          a 2D image (HUD, menus)
/// </code></summary>
public sealed class ModImages
{
    private const int Channels = 4;
    private const int Alpha = 3;
    private const string PngExtension = ".png";
    private const char KeyMark = '@';
    private const char FrameMark = '_';
    private const char Separator = '/';

    /// <summary>What a folder of a mod holds.</summary>
    public enum Kind { Textures, Images }

    /// <summary>One kind of a mod's files: by key, in the level's folder, for all levels.</summary>
    private sealed class Index
    {
        public readonly Dictionary<string, ModFile> Keys = new(StringComparer.OrdinalIgnoreCase);
        public readonly Dictionary<string, ModFile> Level = new(StringComparer.OrdinalIgnoreCase);
        public readonly Dictionary<string, ModFile> Names = new(StringComparer.OrdinalIgnoreCase);
    }

    private readonly List<Dictionary<Kind, Index>> _mods = [];
    /// <summary>Decoded files (premultiplied), null when broken.</summary>
    private readonly Dictionary<ModFile, Png.Image?> _decoded = [];
    private readonly List<string> _found = [];

    private ModImages()
    {
    }

    /// <summary>The names replaced so far (each once).</summary>
    public IReadOnlyList<string> Found => _found;

    /// <summary>The images of <paramref name="mods"/> (first wins) for a level (null: no level's folder).</summary>
    public static ModImages Open(IReadOnlyList<ModFiles> mods, int? level)
    {
        var images = new ModImages();
        var levelFiles = new List<ModFile>();
        foreach (var mod in mods)
        {
            var kinds = new Dictionary<Kind, Index>();
            foreach (var kind in Enum.GetValues<Kind>())
            {
                kinds[kind] = Scan(mod, FolderOf(kind), level, levelFiles);
            }

            images._mods.Add(kinds);
        }

        images.Decode(levelFiles);
        return images;
    }

    public static string FolderOf(Kind kind) => kind == Kind.Textures ? "textures" : "images";

    /// <summary>The level's folder name (LEVEL3).</summary>
    public static string LevelFolder(int level) => $"LEVEL{level}";

    private static Index Scan(ModFiles mod, string folder, int? level, List<ModFile> levelFiles)
    {
        var index = new Index();
        var levelFolder = level is { } n ? LevelFolder(n) : null;
        foreach (var file in mod.List(folder))
        {
            if (!file.EndsWith(PngExtension, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            // "textures/LEVEL3/WALL.png" is in "LEVEL3" ("" for the top).
            var path = new ModFile(mod, file);
            var stem = Path.GetFileNameWithoutExtension(file);
            var inFolder = file[(folder.Length + 1)..];
            var directory = inFolder.LastIndexOf(Separator) is var slash and >= 0 ? inFolder[..slash] : "";
            var mark = stem.IndexOf(KeyMark);
            var inLevel = levelFolder != null && string.Equals(directory, levelFolder, StringComparison.OrdinalIgnoreCase);
            if (inLevel)
            {
                levelFiles.Add(path);
            }

            if (mark >= 0)
            {
                index.Keys.TryAdd(stem[(mark + 1)..], path);
            }
            else if (inLevel)
            {
                index.Level.TryAdd(stem, path);
            }
            else if (directory.Length == 0)
            {
                index.Names.TryAdd(stem, path);
            }
        }

        return index;
    }

    private void Decode(List<ModFile> paths)
    {
        var images = new Png.Image?[paths.Count];
        Parallel.For(0, paths.Count, i => images[i] = Load(paths[i]));
        for (var i = 0; i < paths.Count; i++)
        {
            _decoded[paths[i]] = images[i];
        }
    }

    /// <summary>A file decoded and premultiplied, or null (broken).</summary>
    private static Png.Image? Load(ModFile path)
    {
        try
        {
            var image = Png.Decode(path.Read());
            Premultiply(image.Rgba);
            return image;
        }
        catch (Exception e) when (e is InvalidDataException or IOException)
        {
            Console.Error.WriteLine($"Mod image {path}: {e.Message}");
            return null;
        }
    }

    private Png.Image? Decoded(ModFile path)
    {
        if (!_decoded.TryGetValue(path, out var image))
        {
            image = _decoded[path] = Load(path);
        }

        return image;
    }

    /// <summary>Decodes, on every core, the files first in line to replace these single-frame
    /// textures (as <see cref="Texture"/> would look them up), so that their lookups find them.</summary>
    public void Prefetch(IReadOnlyList<(string Name, Texture Texture)> textures, Palette palette)
    {
        var paths = new HashSet<ModFile>();
        foreach (var (name, texture) in textures)
        {
            if (FirstFile(name, texture, palette) is { } path && !_decoded.ContainsKey(path))
            {
                paths.Add(path);
            }
        }

        Decode([.. paths]);
    }

    /// <summary>A texture's first file to try (the exact view, the level's, all levels').</summary>
    private ModFile? FirstFile(string name, Texture texture, Palette palette)
    {
        string? key = null;
        foreach (var mod in _mods)
        {
            var index = mod[Kind.Textures];
            if (index.Keys.Count > 0 && index.Keys.TryGetValue(key ??= HdKey.Of(texture, palette), out var exact))
            {
                return exact;
            }

            if (index.Level.TryGetValue(name, out var level) || index.Names.TryGetValue(name, out level))
            {
                return level;
            }
        }

        return null;
    }

    /// <summary>A 3D texture's replacement (arenas, models, Kurt's frames), or null.</summary>
    public HdImage? Texture(string name, Texture texture, Palette palette) => Find(Kind.Textures, name, texture, palette);

    /// <summary>A 2D image's replacement (HUD, menus, fonts), or null.</summary>
    public HdImage? Image(string name, Texture texture, Palette palette) => Find(Kind.Images, name, texture, palette);

    private HdImage? Find(Kind kind, string name, Texture texture, Palette palette)
    {
        string? key = null;
        foreach (var mod in _mods)
        {
            var index = mod[kind];
            if (index.Keys.Count > 0 && index.Keys.TryGetValue(key ??= HdKey.Of(texture, palette), out var exact)
                && Strip(exact, texture.FrameCount) is { } byKey)
            {
                return Report(name, byKey);
            }

            if (Frames(index.Level, name, texture.FrameCount) is { } byLevel)
            {
                return Report(name, byLevel);
            }

            if (Frames(index.Names, name, texture.FrameCount) is { } byName)
            {
                return Report(name, byName);
            }
        }

        return null;
    }

    private HdImage Report(string name, HdImage image)
    {
        if (!_found.Contains(name))
        {
            _found.Add(name);
        }

        return image;
    }

    /// <summary>A file of the name (a strip of the frames), else one per frame.</summary>
    private HdImage? Frames(Dictionary<string, ModFile> files, string name, int frames)
    {
        if (files.TryGetValue(name, out var path))
        {
            return Strip(path, frames);
        }

        if (frames == 1)
        {
            return null;
        }

        var images = new Png.Image[frames];
        for (var f = 0; f < frames; f++)
        {
            if (!files.TryGetValue($"{name}{FrameMark}{f}", out var frame) || Decoded(frame) is not { } image)
            {
                return null;
            }

            images[f] = image;
        }

        if (images.Any(i => i.Width != images[0].Width || i.Height != images[0].Height))
        {
            Console.Error.WriteLine($"Mod image {name}: frames of different sizes");
            return null;
        }

        return new HdImage(images[0].Width, images[0].Height, frames, [.. images.SelectMany(i => i.Rgba)]);
    }

    /// <summary>One file of all the frames stacked downwards.</summary>
    private HdImage? Strip(ModFile path, int frames)
    {
        if (Decoded(path) is not { } image)
        {
            return null;
        }

        if (image.Height % frames != 0)
        {
            Console.Error.WriteLine($"Mod image {path}: {image.Height} rows aren't {frames} frames");
            return null;
        }

        return new HdImage(image.Width, image.Height / frames, frames, image.Rgba);
    }

    /// <summary>Straight alpha to premultiplied, in place (the renderer's colour textures).</summary>
    internal static void Premultiply(byte[] rgba)
    {
        for (var i = 0; i < rgba.Length; i += Channels)
        {
            // Opaque pixels keep their colour: (c × 255 + 127) / 255 is c.
            var alpha = rgba[i + Alpha];
            if (alpha == byte.MaxValue)
            {
                continue;
            }

            for (var c = 0; c < Alpha; c++)
            {
                rgba[i + c] = (byte)((rgba[i + c] * alpha + byte.MaxValue / 2) / byte.MaxValue);
            }
        }
    }
}
