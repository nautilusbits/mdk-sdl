using Mdk.Engine.Render;
using Mdk.Game.Audio;
using Mdk.Formats;
using Mdk.Game.HdTextures;
using Mdk.Game.Kurt;
using Mdk.Game.Level;
using Mdk.Game.Mods;

namespace Mdk.Game.Tests;

/// <summary>HD textures without the upscaler: the export (keys, colours, alpha, padding), the
/// finishing of the upscaler's images, the cache's manifest and the loader's choice.</summary>
public class HdTexturesTests : IDisposable
{
    private const int Rgba = 4;
    private const byte Opaque = 255;
    private const int Scale = 2;

    private static readonly Lazy<MdkData> Data = new(() => MdkData.Find() ?? throw new InvalidOperationException("MDK data not found"));

    private readonly string _folder = Directory.CreateTempSubdirectory("mdk-hd-").FullName;

    public void Dispose() => Directory.Delete(_folder, recursive: true);

    /// <summary>Index 1 red, 2 blue, 3 green, the rest grey.</summary>
    private static Palette Colours()
    {
        var palette = new Palette();
        Array.Fill(palette.Rgba, (byte)128);
        new byte[] { 200, 0, 0, Opaque }.CopyTo(palette.Rgba, 1 * Rgba);
        new byte[] { 0, 0, 100, Opaque }.CopyTo(palette.Rgba, 2 * Rgba);
        new byte[] { 0, 150, 0, Opaque }.CopyTo(palette.Rgba, 3 * Rgba);
        return palette;
    }

    private static Texture Texture(string name, int width, int height, byte[] indices, int frames = 1) =>
        new() { Name = name, Width = width, Height = height, FrameCount = frames, Indices = indices };

    private static Palette With(Palette palette, int index, byte r)
    {
        var copy = new Palette();
        palette.Rgba.CopyTo(copy.Rgba, 0);
        copy.Rgba[index * Rgba] = r;
        return copy;
    }

    // --- Keys --------------------------------------------------------------------------------

    [Fact]
    public void KeyFollowsTheColoursUsed()
    {
        var texture = Texture("WALL", 2, 1, [1, 2]);
        var palette = Colours();
        var key = HdKey.Of(texture, palette);

        Assert.Equal(key, HdKey.Of(Texture("WALL", 2, 1, [1, 2]), Colours()));
        // A colour the texture doesn't use (3), or index 0 (always clear), changes nothing.
        Assert.Equal(key, HdKey.Of(texture, With(palette, 3, 7)));
        Assert.Equal(key, HdKey.Of(texture, With(palette, 0, 7)));
        // A colour it uses, or its indices, make another image.
        Assert.NotEqual(key, HdKey.Of(texture, With(palette, 2, 7)));
        Assert.NotEqual(key, HdKey.Of(Texture("WALL", 2, 1, [2, 1]), palette));
        Assert.NotEqual(key, HdKey.Of(Texture("WALL", 1, 2, [1, 2]), palette));
    }

    // --- Export ------------------------------------------------------------------------------

    [Fact]
    public void FramesTakeThePaletteIndexZeroClear()
    {
        var texture = Texture("ANIM", 2, 1, [1, 0, 2, 3], frames: 2);

        var first = TextureExport.Frame(texture, Colours(), 0);
        var second = TextureExport.Frame(texture, Colours(), 1);

        Assert.Equal(new byte[] { 200, 0, 0, Opaque, 0, 0, 0, 0 }, first);
        Assert.Equal(new byte[] { 0, 0, 100, Opaque, 0, 150, 0, Opaque }, second);
    }

    /// <summary>The upscaler's input: clear texels take their neighbours' colour (no black fringe
    /// on cut-outs), and the image wraps around by the margin (tiling textures meet without seams).</summary>
    [Fact]
    public void InputBleedsAndWraps()
    {
        var rgba = TextureExport.Frame(Texture("CUT", 3, 1, [1, 0, 2]), Colours(), 0);
        const int margin = 2;

        var padded = UpscaleImages.Input(rgba, 3, 1, margin);

        var (width, height) = UpscaleImages.Padded(3, 1, margin);
        Assert.Equal((7, 5), (width, height));
        Assert.Equal(width * height * Rgba, padded.Length);
        // Row 2 is the image's: (wrapped) 0 2 | 1 [between] 2 | 1 0 ...
        var row = padded.AsSpan(2 * width * Rgba, width * Rgba);
        Assert.Equal(new byte[] { 200, 0, 0, Opaque }, row.Slice(2 * Rgba, Rgba).ToArray());
        Assert.Equal(new byte[] { 0, 0, 100, Opaque }, row.Slice(4 * Rgba, Rgba).ToArray());
        Assert.Equal(new byte[] { 0, 0, 100, Opaque }, row.Slice(1 * Rgba, Rgba).ToArray());
        // The clear texel: the mean of red and blue, opaque.
        Assert.Equal(new byte[] { 100, 0, 50, Opaque }, row.Slice(3 * Rgba, Rgba).ToArray());
    }

    /// <summary>The upscaler's output: the margin cut, brought to the cache's scale, the alpha the
    /// source's (hard edges: 0 or 255).</summary>
    [Fact]
    public void OutputCropsScalesAndKeysAlpha()
    {
        var source = TextureExport.Frame(Texture("CUT", 2, 2, [1, 0, 1, 1]), Colours(), 0);
        const int margin = 1;
        const int factor = 4;
        var (paddedWidth, paddedHeight) = UpscaleImages.Padded(2, 2, margin);
        // A uniform grey output from a 4x upscaler.
        var upscaled = new byte[paddedWidth * factor * paddedHeight * factor * Rgba];
        Array.Fill(upscaled, (byte)90);

        var hd = UpscaleImages.Output(upscaled, factor, source, 2, 2, margin, Scale);

        Assert.Equal(4 * 4 * Rgba, hd.Length);
        for (var i = 0; i < 16; i++)
        {
            var (x, y) = (i % 4, i / 4);
            var clear = x >= 2 && y < 2;
            Assert.Equal(clear ? 0 : Opaque, hd[i * Rgba + 3]);
            Assert.Equal(90, hd[i * Rgba]);
        }
    }

    /// <summary>Sprites (Kurt) keep their cover soft: the shader's half-cover cut then follows the
    /// source's outline as smoothly as the original's, not in steps.</summary>
    [Fact]
    public void SpritesKeepSoftEdges()
    {
        var source = TextureExport.Frame(Texture("K_RUN_0", 2, 1, [1, 0]), Colours(), 0);
        const int margin = 1;
        const int factor = 4;
        var (paddedWidth, paddedHeight) = UpscaleImages.Padded(2, 1, margin);
        var upscaled = new byte[paddedWidth * factor * paddedHeight * factor * Rgba];

        var hard = UpscaleImages.Output(upscaled, factor, source, 2, 1, margin, Scale);
        var soft = UpscaleImages.Output(upscaled, factor, source, 2, 1, margin, Scale, HdAlpha.Soft);

        // Row 0: x = 0..3 at source x 0 (clamped), 0.25, 0.75, 1: cover 1, 0.75, 0.25, 0.
        Assert.Equal([Opaque, Opaque, 0, 0], Enumerable.Range(0, 4).Select(x => hard[x * Rgba + 3]));
        Assert.Equal([Opaque, 191, 64, 0], Enumerable.Range(0, 4).Select(x => soft[x * Rgba + 3]));
    }

    // --- Cache -------------------------------------------------------------------------------

    [Fact]
    public void ManifestRoundTrips()
    {
        var manifest = new HdManifest("realesrgan-x4plus", Scale);
        manifest.Entries["abc"] = new HdManifest.Entry("abc", 7, "B_WALL", 64, 32, 2, "LEVEL7/B_WALL_abc.png");
        manifest.Save(_folder);

        var loaded = HdManifest.Load(_folder);

        Assert.NotNull(loaded);
        Assert.Equal("realesrgan-x4plus", loaded.Model);
        Assert.Equal(Scale, loaded.Scale);
        Assert.Equal(manifest.Entries["abc"], loaded.Entries["abc"]);
    }

    /// <summary>Older builds' textures-hd/ moves into the HD textures mod, where the loader finds
    /// each image by its key (the texture through that palette).</summary>
    [Fact]
    public void OldCacheBecomesTheMod()
    {
        var texture = Texture("WALL", 2, 2, [1, 2, 3, 1]);
        var key = HdKey.Of(texture, Colours());
        var old = Path.Combine(_folder, HdGenerator.OldFolder);
        Directory.CreateDirectory(Path.Combine(old, "LEVEL3"));
        var rgba = Enumerable.Repeat((byte)77, 4 * 4 * Rgba).ToArray();
        File.WriteAllBytes(Path.Combine(old, "LEVEL3", $"WALL_{key}.png"), Png.Encode(4, 4, rgba, Png.Channels.Rgba));
        var manifest = new HdManifest("model", Scale);
        manifest.Entries[key] = new HdManifest.Entry(key, 3, "WALL", 2, 2, 1, $"LEVEL3/WALL_{key}.png");
        manifest.Save(old);

        Assert.Equal(1, HdGenerator.Migrate(_folder));

        var mod = HdGenerator.FolderIn(_folder);
        Assert.False(Directory.Exists(old));
        Assert.True(File.Exists(Path.Combine(mod, "textures", "LEVEL3", $"WALL@{key}.png")));
        Assert.Equal($"textures/LEVEL3/WALL@{key}.png", HdManifest.Load(mod)!.Entries[key].File);
        var catalog = ModCatalog.Scan(ModCatalog.FolderIn(_folder));
        Assert.Equal(-100, Assert.Single(catalog.Mods).Priority);
        var image = ModImages.Open([ModFiles.Folder(mod)], 3).Texture("WALL", texture, Colours());
        Assert.Equal((4, 4, 1), (image!.Width, image.Height, image.Frames));

        // Once: nothing left to move.
        Assert.Equal(0, HdGenerator.Migrate(_folder));
    }

    /// <summary>Making the HD textures (menu or --upscale-textures) first moves an older build's
    /// cache into the mod, so its images aren't made again.</summary>
    [Fact]
    public void RunMovesTheOldCacheFirst()
    {
        var old = Path.Combine(_folder, HdGenerator.OldFolder);
        Directory.CreateDirectory(old);
        new HdManifest("model", Scale).Save(old);
        using var cancel = new CancellationTokenSource();
        cancel.Cancel();

        // Cancelled at the upscaler's download, before any game data is read.
        Assert.ThrowsAny<OperationCanceledException>(() =>
            HdGenerator.Run(null!, _folder, HdOptions.Default, new HdProgress(), cancel.Token));

        Assert.False(Directory.Exists(old));
        Assert.NotNull(HdManifest.Load(HdGenerator.FolderIn(_folder)));
    }

    // --- The game's textures --------------------------------------------------------------------

    /// <summary>Level 3's arenas and models: each texture once per distinct look, all in palettes of
    /// the level's arenas.</summary>
    [DataFact]
    public void LevelExportsItsTextures()
    {
        var level = new LevelData(Data.Value, 3);
        var cmi = Cmi.Load(Data.Value.PathOf("TRAVERSE/LEVEL3/LEVEL3.CMI"));

        var sources = TextureExport.Collect(level, cmi);

        Assert.True(sources.Count > 50, $"{sources.Count} textures");
        Assert.Equal(sources.Count, sources.Select(s => s.Key).Distinct().Count());
        Assert.All(sources, s => Assert.Equal(HdKey.Of(s.Texture, s.Palette), s.Key));
    }

    /// <summary>Kurt's frames: TRAVSPRT.BNI's he's drawn with, through the level's palette, and the
    /// level's own (LEVEL4's snowboard).</summary>
    [DataFact]
    public void LevelExportsKurt()
    {
        var level = new LevelData(Data.Value, 4);
        var sprites = Bni.Load(Data.Value.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        var run = sprites.GetAnimation("K_RUN");

        var sources = TextureExport.Kurt(level, sprites, SoundBank.ForLevel(Data.Value, 4).Animation);

        var keys = sources.Select(s => s.Key).ToHashSet();
        Assert.Contains(HdKey.Of(run.GetFrame(0).Image, level.Dti.Palette), keys);
        Assert.Contains(sources, s => s.Name == "K_SURF_0");
        Assert.Contains(sources, s => s.Name == "K_CHUTEC_0");
        Assert.All(sources, s => Assert.Same(level.Dti.Palette, s.Palette));
        Assert.All(sources, s => Assert.Equal(HdAlpha.Soft, s.Alpha));
        Assert.Equal(sources.Count, keys.Count);
    }
}
