using System.Diagnostics;
using System.Globalization;
using Mdk.Engine.Render;
using Mdk.Engine.Upscale;
using Mdk.Formats;
using Mdk.Game.Audio;
using Mdk.Game.Level;
using Mdk.Game.Mods;

namespace Mdk.Game.HdTextures;

/// <summary>The upscaler's network: Real-ESRGAN x4plus (General) or animevideov3 (Anime).</summary>
public enum HdModel { General, Anime }

/// <summary>What to make: the levels, the upscaler's model, the images' scale (2 or 4).</summary>
public sealed record HdOptions(IReadOnlyList<int> Levels, HdModel Model, int Scale)
{
    public static readonly int[] AllLevels = [3, 4, 5, 6, 7, 8];
    public static readonly int[] Scales = [2, 4];
    public static HdOptions Default { get; } = new(AllLevels, HdModel.General, 2);

    /// <summary>Where the upscaler is downloaded from (settings.cfg).</summary>
    public UpscalerSource Source { get; init; } = UpscalerSource.Default;

    /// <summary>The engine's model.</summary>
    public UpscaleModel Upscaler => Model == HdModel.General ? UpscaleModel.General : UpscaleModel.Anime;
}

/// <summary>Where a run is, for a progress screen on another thread.</summary>
public sealed class HdProgress
{
    public enum Stage { Download, Read, Export, Upscale, Save, Done, Failed, Cancelled }

    private readonly Lock _lock = new();
    private Stage _stage;
    private long _done;
    private long _total;
    private string _message = "";

    public void Set(Stage stage, long done, long total, string message = "")
    {
        lock (_lock)
        {
            (_stage, _done, _total, _message) = (stage, done, total, message);
        }
    }

    public (Stage Stage, long Done, long Total, string Message) Read()
    {
        lock (_lock)
        {
            return (_stage, _done, _total, _message);
        }
    }

    /// <summary>One short line (a menu's): "Upscaling 120/900", "Downloading 12/45 MB", "Done: 311 made".</summary>
    public string Describe()
    {
        var (stage, done, total, message) = Read();
        return stage switch
        {
            Stage.Download => $"Downloading {done >> 20}/{total >> 20} MB",
            Stage.Read => $"Reading level {done}",
            Stage.Export => $"Exporting {done}/{total}",
            Stage.Upscale => $"Upscaling {done}/{total}",
            Stage.Save => $"Saving {done}/{total}",
            Stage.Done => $"Done: {done} made",
            Stage.Cancelled => "Cancelled",
            _ => $"Failed: {(message.Length > MaxMessage ? message[..MaxMessage] : message)}",
        };
    }

    /// <summary>A failure's message is cut to this length (one line on the menu).</summary>
    private const int MaxMessage = 24;

    public bool Finished => Read().Stage is Stage.Done or Stage.Failed or Stage.Cancelled;
}

/// <summary>Makes the HD textures from the user's game files: the levels' textures and 2D images
/// (<see cref="TextureExport"/>) through Real-ESRGAN (<see cref="RealEsrgan"/>), into a mod of the
/// user's folder (<see cref="ModFolder"/>, priority below other mods: theirs win). Nothing of the
/// game leaves the computer. Images already made for the same texture, palette, model and scale are
/// kept; images of textures no longer exported are deleted. Each image's name holds its key: it
/// replaces exactly that texture through that palette (<see cref="ModImages"/>).
/// <code>
///   tools/realesrgan/ ◄── download (once, SHA-256 checked)
///   mods/hd-textures/work/in/&lt;key&gt;_&lt;frame&gt;.png ─► upscaler ─► work/out/ ─► crop, scale, alpha
///     ─► mods/hd-textures/textures/LEVELn/&lt;NAME&gt;@&lt;key&gt;.png (images/ for 2D), manifest.txt, mod.txt
/// </code></summary>
public static class HdGenerator
{
    /// <summary>The mod's folder in <c>mods/</c>.</summary>
    public const string ModFolder = "hd-textures";
    /// <summary>Below other mods (0 by default).</summary>
    private const int ModPriority = -100;
    /// <summary>Where older builds kept the HD textures, in the user folder.</summary>
    public const string OldFolder = "textures-hd";
    /// <summary>Source texels wrapped around each frame for the upscaler (seamless tiling).</summary>
    private const int Margin = 8;
    private const string ToolsFolder = "tools";
    private const string ToolFolder = "realesrgan";
    private const string WorkFolder = "work";
    private const int BytesPerMegabyte = 1 << 20;
    private const int Channels = 4;

    /// <summary>The upscaler's folder in a user folder.</summary>
    public static string ToolIn(string userFolder) => Path.Combine(userFolder, ToolsFolder, ToolFolder);

    /// <summary>Makes the cache; returns a summary line. Throws on failure (the cache keeps what it had).</summary>
    public static string Run(MdkData data, string userFolder, HdOptions options, HdProgress progress, CancellationToken cancel)
    {
        if (!HdOptions.Scales.Contains(options.Scale))
        {
            throw new ArgumentException($"HD textures are made {string.Join(" or ", HdOptions.Scales)} times larger");
        }

        // An older build's cache first: its current images aren't made again.
        Migrate(userFolder);
        var clock = Stopwatch.StartNew();
        progress.Set(HdProgress.Stage.Download, 0, 0);
        var tool = RealEsrgan.Install(ToolIn(userFolder), options.Source, (done, total) => progress.Set(HdProgress.Stage.Download, done, total), cancel);

        var folder = FolderIn(userFolder);
        Directory.CreateDirectory(folder);
        var model = RealEsrgan.NameOf(options.Upscaler);
        var manifest = Current(folder, model, options.Scale);
        File.WriteAllText(Path.Combine(folder, ModInfo.FileName), Info(model, options.Scale).Format());

        // What each level shows, and what the cache lacks.
        var todo = new List<HdSource>();
        var keys = new HashSet<string>();
        var sizes = new List<string>();
        var sprites = Bni.Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        foreach (var number in options.Levels)
        {
            progress.Set(HdProgress.Stage.Read, number, 0);
            var level = new LevelData(data, number);
            var sources = TextureExport.Collect(level, Cmi.Load(data.PathOf($"TRAVERSE/LEVEL{number}/LEVEL{number}.CMI")));
            sources = [.. sources.Concat(TextureExport.Kurt(level, sprites, SoundBank.ForLevel(data, number).Animation))
                .Concat(CanvasExport.Level(level, sprites)).Append(TextureExport.Sky(number, level.Dti)).DistinctBy(s => s.Key)];
            Prune(folder, manifest, number, sources);
            // Levels share some images: each is made once.
            todo.AddRange(sources.Where(s => !manifest.Entries.TryGetValue(s.Key, out var e) || !File.Exists(Path.Combine(folder, e.File)))
                .Where(s => keys.Add(s.Key)));
            sizes.Add(Sizes(number, sources, options.Scale));
        }

        // The menus' 2D images (no level's), the fonts left as made (upscaled, they blur).
        var menus = CanvasExport.Menus(data, CanvasExport.Fonts.Skipped).DistinctBy(s => s.Key).ToList();
        Prune(folder, manifest, CanvasExport.NoLevel, menus);
        todo.AddRange(menus.Where(s => !manifest.Entries.TryGetValue(s.Key, out var e) || !File.Exists(Path.Combine(folder, e.File)))
            .Where(s => keys.Add(s.Key)));

        manifest.Save(folder);
        var work = Path.Combine(folder, WorkFolder);
        try
        {
            if (todo.Count > 0)
            {
                Make(tool, todo, work, folder, manifest, options, progress, cancel);
            }
        }
        finally
        {
            if (Directory.Exists(work))
            {
                Directory.Delete(work, recursive: true);
            }
        }

        manifest.Save(folder);
        var summary = string.Create(CultureInfo.InvariantCulture,
            $"{todo.Count} textures made, {manifest.Entries.Count} in the mod, {FolderBytes(folder) / BytesPerMegabyte} MB on disk, {clock.Elapsed.TotalSeconds:0} s");
        foreach (var line in sizes)
        {
            Console.WriteLine(line);
        }

        progress.Set(HdProgress.Stage.Done, todo.Count, todo.Count, summary);
        return summary;
    }

    /// <summary>The mod's folder in a user folder.</summary>
    public static string FolderIn(string userFolder) => Path.Combine(ModCatalog.FolderIn(userFolder), ModFolder);

    /// <summary>The mod's <c>mod.txt</c>: the upscaler's model and scale.</summary>
    private static ModInfo Info(string model, int scale) =>
        new(ModFolder, "HD textures (Real-ESRGAN)", "made on this computer", "", $"{model}, {scale}x", ModPriority);

    /// <summary>Moves older builds' <c>textures-hd/</c> into the mod (once; its images keep their
    /// keys). Returns how many images moved.</summary>
    public static int Migrate(string userFolder)
    {
        var old = Path.Combine(userFolder, OldFolder);
        var folder = FolderIn(userFolder);
        if (HdManifest.Load(old) is not { } manifest || HdManifest.Load(folder) != null)
        {
            return 0;
        }

        var moved = new HdManifest(manifest.Model, manifest.Scale) { Version = manifest.Version };
        foreach (var entry in manifest.Entries.Values)
        {
            var source = Path.Combine(old, entry.File);
            if (!File.Exists(source))
            {
                continue;
            }

            var file = FileOf(entry.Level, entry.Name, entry.Key);
            var target = Path.Combine(folder, file);
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Move(source, target, overwrite: true);
            moved.Entries[entry.Key] = entry with { File = file };
        }

        // An old cache without images still becomes the (empty) mod.
        Directory.CreateDirectory(folder);
        moved.Save(folder);
        File.WriteAllText(Path.Combine(folder, ModInfo.FileName), Info(manifest.Model, manifest.Scale).Format());
        Directory.Delete(old, recursive: true);
        return moved.Entries.Count;
    }

    /// <summary>An image's file in the mod: its level's folder (the menus' none), its name, its key.</summary>
    private static string FileOf(int level, string name, string key, ModImages.Kind kind = ModImages.Kind.Textures) =>
        level == CanvasExport.NoLevel
            ? $"{ModImages.FolderOf(kind)}/{SafeName(name)}@{key}.png"
            : $"{ModImages.FolderOf(kind)}/{ModImages.LevelFolder(level)}/{SafeName(name)}@{key}.png";

    /// <summary>The cache's manifest when it was made the same way; else the old images go.</summary>
    private static HdManifest Current(string folder, string model, int scale)
    {
        var manifest = HdManifest.Load(folder);
        if (manifest != null && manifest.Version == HdManifest.Format && manifest.Model == model && manifest.Scale == scale)
        {
            return manifest;
        }

        foreach (var entry in manifest?.Entries.Values.ToList() ?? [])
        {
            File.Delete(Path.Combine(folder, entry.File));
        }

        return new HdManifest(model, scale);
    }

    /// <summary>Forgets (and deletes) a level's images whose texture isn't exported any more (stale).</summary>
    private static void Prune(string folder, HdManifest manifest, int level, List<HdSource> sources)
    {
        var keys = sources.Select(s => s.Key).ToHashSet();
        foreach (var entry in manifest.Entries.Values.Where(e => e.Level == level && !keys.Contains(e.Key)).ToList())
        {
            File.Delete(Path.Combine(folder, entry.File));
            manifest.Entries.Remove(entry.Key);
        }
    }

    /// <summary>A level's colour textures with their mips, original and HD (the GPU memory they take).</summary>
    private static string Sizes(int level, List<HdSource> sources, int scale)
    {
        long original = 0, hd = 0;
        foreach (var s in sources)
        {
            var t = s.Texture;
            original += (long)ColourMips.ChainSize(t.Width, t.Height) * t.FrameCount;
            hd += (long)ColourMips.ChainSize(t.Width * scale, t.Height * scale) * t.FrameCount;
        }

        return $"LEVEL{level}: {sources.Count} textures, colour textures {original / BytesPerMegabyte} MB, HD {hd / BytesPerMegabyte} MB";
    }

    /// <summary>Exports, upscales and saves <paramref name="todo"/> into the cache.</summary>
    private static void Make(RealEsrgan tool, List<HdSource> todo, string work, string folder, HdManifest manifest,
        HdOptions options, HdProgress progress, CancellationToken cancel)
    {
        if (Directory.Exists(work))
        {
            Directory.Delete(work, recursive: true);
        }

        var input = Directory.CreateDirectory(Path.Combine(work, "in")).FullName;
        var output = Path.Combine(work, "out");
        var frames = todo.Sum(s => s.Texture.FrameCount);
        var exported = 0;
        var parallel = new ParallelOptions { CancellationToken = cancel };
        Parallel.ForEach(todo, parallel, source =>
        {
            var t = source.Texture;
            for (var f = 0; f < t.FrameCount; f++)
            {
                var padded = UpscaleImages.Input(TextureExport.Frame(t, source.Palette, f, source.ZeroOpaque),
                    t.Width, t.Height, Margin, source.WrapY);
                var (width, height) = UpscaleImages.Padded(t.Width, t.Height, Margin);
                File.WriteAllBytes(Path.Combine(input, FrameFile(source, f)), Png.Encode(width, height, padded, Png.Channels.Rgb));
                progress.Set(HdProgress.Stage.Export, Interlocked.Increment(ref exported), frames);
            }
        });

        var factor = RealEsrgan.Factor(options.Upscaler, options.Scale);
        progress.Set(HdProgress.Stage.Upscale, 0, frames);
        tool.Run(input, output, options.Upscaler, factor, done => progress.Set(HdProgress.Stage.Upscale, done, frames), cancel);

        var saved = 0;
        var entries = new HdManifest.Entry[todo.Count];
        Parallel.For(0, todo.Count, parallel, i =>
        {
            entries[i] = Save(todo[i], output, folder, factor, options.Scale);
            progress.Set(HdProgress.Stage.Save, Interlocked.Increment(ref saved), todo.Count);
        });

        foreach (var entry in entries)
        {
            manifest.Entries[entry.Key] = entry;
        }
    }

    private static string FrameFile(HdSource source, int frame) => $"{source.Key}_{frame}.png";

    /// <summary>A texture's upscaled frames finished (<see cref="UpscaleImages.Output"/>), stacked, saved.</summary>
    private static HdManifest.Entry Save(HdSource source, string output, string folder, int factor, int scale)
    {
        var t = source.Texture;
        var frameBytes = t.Width * scale * t.Height * scale * Channels;
        var image = new byte[frameBytes * t.FrameCount];
        for (var f = 0; f < t.FrameCount; f++)
        {
            var upscaled = Png.Decode(File.ReadAllBytes(Path.Combine(output, FrameFile(source, f))));
            var (width, height) = UpscaleImages.Padded(t.Width, t.Height, Margin);
            if (upscaled.Width != width * factor || upscaled.Height != height * factor)
            {
                throw new InvalidDataException($"{source.Name}: the upscaler made {upscaled.Width} x {upscaled.Height}");
            }

            var frame = UpscaleImages.Output(upscaled.Rgba, factor, TextureExport.Frame(t, source.Palette, f, source.ZeroOpaque),
                t.Width, t.Height, Margin, scale, source.Alpha);
            frame.CopyTo(image, f * frameBytes);
        }

        var file = FileOf(source.Level, source.Name, source.Key, source.Kind);
        var path = Path.Combine(folder, file);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var opaque = t.Indices.AsSpan().IndexOf((byte)0) < 0;
        File.WriteAllBytes(path, Png.Encode(t.Width * scale, t.Height * scale * t.FrameCount, image, opaque ? Png.Channels.Rgb : Png.Channels.Rgba));
        return new HdManifest.Entry(source.Key, source.Level, source.Name, t.Width, t.Height, t.FrameCount, file);
    }

    /// <summary>A texture's name as a file name (characters a file system refuses become _).</summary>
    private static string SafeName(string name) =>
        string.Concat(name.Select(c => char.IsAsciiLetterOrDigit(c) || c is '_' or '-' ? c : '_'));

    private static long FolderBytes(string folder) =>
        new DirectoryInfo(folder).EnumerateFiles("*", SearchOption.AllDirectories).Sum(f => f.Length);
}
