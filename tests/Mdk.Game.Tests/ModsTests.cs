using Mdk.Engine.Render;
using Mdk.Formats;
using Mdk.Game.Flow;
using Mdk.Game.HdTextures;
using Mdk.Game.Kurt;
using Mdk.Game.Mods;

namespace Mdk.Game.Tests;

/// <summary>Mods: <c>mod.txt</c>, finding and ordering mods, switching them in the settings, and
/// which mod's image replaces a texture (key, level, name; higher priority first).</summary>
public class ModsTests : IDisposable
{
    private const int Rgba = 4;
    private const byte Opaque = 255;

    private readonly string _folder = Directory.CreateTempSubdirectory("mdk-mods-").FullName;

    public void Dispose() => Directory.Delete(_folder, recursive: true);

    private string Mod(string name, string? info = null)
    {
        var path = Path.Combine(_folder, name);
        Directory.CreateDirectory(path);
        if (info != null)
        {
            File.WriteAllText(Path.Combine(path, ModInfo.FileName), info);
        }

        return path;
    }

    /// <summary>A PNG of one colour (red = <paramref name="red"/>), alpha half where asked.</summary>
    private static void Image(string path, int width, int height, byte red, byte alpha = Opaque)
    {
        var rgba = new byte[width * height * Rgba];
        for (var i = 0; i < width * height; i++)
        {
            rgba[i * Rgba] = red;
            rgba[i * Rgba + 3] = alpha;
        }

        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, Png.Encode(width, height, rgba, Png.Channels.Rgba));
    }

    private static Texture Texture(string name, int frames = 1) =>
        new() { Name = name, Width = 2, Height = 2, FrameCount = frames, Indices = Enumerable.Repeat((byte)1, 4 * frames).ToArray() };

    private static Palette Colours()
    {
        var palette = new Palette();
        Array.Fill(palette.Rgba, (byte)100);
        return palette;
    }

    // --- mod.txt and the catalogue -----------------------------------------------------------

    [Fact]
    public void ParsesModInfo()
    {
        var info = ModInfo.Parse("walls", "# my mod\nname = Clean walls\nauthor=Me\nversion=1.2\ndescription=Walls, cleaner\npriority=5\nunknown=x\n");

        Assert.Equal(new ModInfo("walls", "Clean walls", "Me", "1.2", "Walls, cleaner", 5), info);
        Assert.Equal(info, ModInfo.Parse("walls", info.Format()));
    }

    [Fact]
    public void ModInfoDefaults()
    {
        Assert.Equal(new ModInfo("walls", "walls", "", "", "", 0), ModInfo.Parse("walls", "priority=high\n"));
    }

    /// <summary>Higher priority first, then by folder name; files and hidden folders aren't mods.</summary>
    [Fact]
    public void FindsAndOrdersMods()
    {
        Mod("b-low");
        Mod("a-low", "priority=0");
        Mod("z-high", "name=High\npriority=10");
        File.WriteAllText(Path.Combine(_folder, "readme.txt"), "");
        Mod(".git");

        var catalog = ModCatalog.Scan(_folder);

        Assert.Equal(["z-high", "a-low", "b-low"], catalog.Mods.Select(m => m.Folder));
        Assert.Equal("High", catalog.Mods[0].Name);
        Assert.Empty(ModCatalog.Scan(Path.Combine(_folder, "missing")).Mods);
    }

    [Fact]
    public void SettingsSwitchMods()
    {
        Mod("a");
        Mod("b");
        var catalog = ModCatalog.Scan(_folder);
        var settings = new Settings();

        // A new mod is on.
        Assert.Equal(["a", "b"], catalog.Enabled(settings).Select(m => m.Folder));

        settings.Mods["a"] = ModState.Off;
        Assert.Equal(["b"], catalog.Enabled(settings).Select(m => m.Folder));

        // --mod=a: that one only, for this run.
        catalog.Only(settings, ["A"]);
        Assert.Equal(["a"], catalog.Enabled(settings).Select(m => m.Folder));
    }

    [Fact]
    public void ModSettingsRoundTrip()
    {
        var settings = new Settings();
        settings.Mods["walls"] = ModState.Off;
        settings.Mods["hd-textures"] = ModState.On;

        var read = Settings.Parse(settings.Format());

        Assert.Equal(settings.Format(), read.Format());
        Assert.Equal(ModState.Off, read.StateOf("WALLS"));
        Assert.Equal(ModState.On, read.StateOf("hd-textures"));
        Assert.Equal(ModState.On, read.StateOf("new"));
    }

    /// <summary>Older settings' HD switch becomes the HD textures mod's.</summary>
    [Theory]
    [InlineData("Hd", ModState.On)]
    [InlineData("Original", ModState.Off)]
    public void OldHdSwitchBecomesTheMods(string value, ModState state)
    {
        var read = Settings.Parse($"textures={value}\n");

        Assert.Equal(state, read.StateOf(HdGenerator.ModFolder));
    }

    // --- Texture images ----------------------------------------------------------------------

    private ModImages Open(int? level, params string[] mods) => ModImages.Open([.. mods.Select(m => ModFiles.Folder(Path.Combine(_folder, m)))], level);

    [Fact]
    public void NoModNoImage()
    {
        Assert.Null(Open(3).Texture("WALL", Texture("WALL"), Colours()));
    }

    /// <summary>Any size; premultiplied as the renderer's colour textures.</summary>
    [Fact]
    public void NamedTextureReplaces()
    {
        Image(Path.Combine(Mod("m"), "textures", "WALL.png"), 8, 6, 200, alpha: 128);

        var image = Open(3, "m").Texture("WALL", Texture("WALL"), Colours());

        Assert.NotNull(image);
        Assert.Equal((8, 6, 1), (image.Width, image.Height, image.Frames));
        Assert.Equal(new byte[] { 100, 0, 0, 128 }, image.Rgba.AsSpan(0, Rgba).ToArray());
    }

    /// <summary>Within a mod: the exact view (key), then the level's folder, then all levels; names ignore case.</summary>
    [Fact]
    public void KeyThenLevelThenName()
    {
        var mod = Mod("m");
        var texture = Texture("WALL");
        Image(Path.Combine(mod, "textures", "wall.png"), 2, 2, 1);
        Image(Path.Combine(mod, "textures", "LEVEL3", "WALL.png"), 2, 2, 2);

        Assert.Equal(2, Open(3, "m").Texture("WALL", texture, Colours())!.Rgba[0]);
        Assert.Equal(1, Open(4, "m").Texture("WALL", texture, Colours())!.Rgba[0]);

        Image(Path.Combine(mod, "textures", "LEVEL7", $"WALL@{HdKey.Of(texture, Colours())}.png"), 2, 2, 3);
        Assert.Equal(3, Open(3, "m").Texture("WALL", texture, Colours())!.Rgba[0]);
        Assert.Equal(2, Open(3, "m").Texture("WALL", texture, new Palette())!.Rgba[0]);
    }

    /// <summary>Mods in the catalogue's order: the first that has an image wins, even by name over another's key.</summary>
    [Fact]
    public void FirstModWins()
    {
        var texture = Texture("WALL");
        Image(Path.Combine(Mod("high"), "textures", "WALL.png"), 2, 2, 1);
        Image(Path.Combine(Mod("low"), "textures", "LEVEL3", $"WALL@{HdKey.Of(texture, Colours())}.png"), 2, 2, 2);

        Assert.Equal(1, Open(3, "high", "low").Texture("WALL", texture, Colours())!.Rgba[0]);
        Assert.Equal(2, Open(3, "low", "high").Texture("WALL", texture, Colours())!.Rgba[0]);
    }

    /// <summary>Animated textures: one image per frame (NAME_0, NAME_1...) or a strip of the frames stacked downwards.</summary>
    [Fact]
    public void AnimatedFrames()
    {
        var mod = Mod("m");
        Image(Path.Combine(mod, "textures", "ANIM_0.png"), 4, 4, 1);
        Image(Path.Combine(mod, "textures", "ANIM_1.png"), 4, 4, 2);
        Image(Path.Combine(mod, "textures", "STRIP.png"), 4, 8, 3);
        Image(Path.Combine(mod, "textures", "ODD.png"), 4, 5, 3);
        Image(Path.Combine(mod, "textures", "MIXED_0.png"), 4, 4, 1);
        Image(Path.Combine(mod, "textures", "MIXED_1.png"), 2, 2, 1);
        var images = Open(3, "m");

        var frames = images.Texture("ANIM", Texture("ANIM", 2), Colours());
        Assert.NotNull(frames);
        Assert.Equal((4, 4, 2), (frames.Width, frames.Height, frames.Frames));
        Assert.Equal(2, frames.Rgba[4 * 4 * Rgba]);

        var strip = images.Texture("STRIP", Texture("STRIP", 2), Colours());
        Assert.Equal((4, 4, 2), (strip!.Width, strip.Height, strip.Frames));

        // Frames that don't stack: the original stays.
        Assert.Null(images.Texture("ODD", Texture("ODD", 2), Colours()));
        Assert.Null(images.Texture("MIXED", Texture("MIXED", 2), Colours()));
        Assert.Null(images.Texture("ANIM", Texture("ANIM", 3), Colours()));
    }

    [Fact]
    public void BrokenImageKeepsTheOriginal()
    {
        var path = Path.Combine(Mod("m"), "textures", "WALL.png");
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, "not a png");

        Assert.Null(Open(3, "m").Texture("WALL", Texture("WALL"), Colours()));
    }

    /// <summary>2D images (HUD, menus) come from images/, textures from textures/.</summary>
    [Fact]
    public void ImagesAreTheirOwn()
    {
        var mod = Mod("m");
        Image(Path.Combine(mod, "images", "SC_STAT.png"), 4, 4, 9);
        var images = Open(null, "m");

        Assert.Equal(9, images.Image("SC_STAT", Texture("SC_STAT"), Colours())!.Rgba[0]);
        Assert.Null(images.Texture("SC_STAT", Texture("SC_STAT"), Colours()));
    }

    /// <summary>Kurt's frames (ANIMATION_frame) in the enhanced look only.</summary>
    [Fact]
    public void KurtTakesModFrames()
    {
        Image(Path.Combine(Mod("m"), "textures", "K_RUN_0.png"), 4, 4, 5);
        var images = Open(3, "m");
        var frame = Texture("K_RUN_0");

        Assert.NotNull(KurtSprite.Upscaled(images, Shading.Sprite, "K_RUN_0", frame, Colours()));
        Assert.Null(KurtSprite.Upscaled(images, Shading.Original, "K_RUN_0", frame, Colours()));
        Assert.Null(KurtSprite.Upscaled(null, Shading.Sprite, "K_RUN_0", frame, Colours()));
        Assert.Null(KurtSprite.Upscaled(images, Shading.Sprite, "K_RUN_1", frame, Colours()));
    }

    /// <summary>The overrides found are counted (the console's mods, F3).</summary>
    [Fact]
    public void CountsWhatItFinds()
    {
        Image(Path.Combine(Mod("m"), "textures", "WALL.png"), 2, 2, 1);
        var images = Open(3, "m");

        images.Texture("WALL", Texture("WALL"), Colours());
        images.Texture("OTHER", Texture("OTHER"), Colours());

        Assert.Equal(["WALL"], images.Found);
    }

    /// <summary>Straight alpha becomes premultiplied, rounded: (c × a + 127) / 255; opaque
    /// pixels keep their colour, clear ones lose it.</summary>
    [Fact]
    public void PremultipliesRounded()
    {
        byte[] alphas = [Opaque, 254, 128, 1, 0];
        var rgba = new byte[256 * alphas.Length * Rgba];
        for (var i = 0; i < rgba.Length / Rgba; i++)
        {
            (rgba[i * Rgba], rgba[i * Rgba + 1], rgba[i * Rgba + 2]) = ((byte)i, (byte)~i, (byte)(i * 7));
            rgba[i * Rgba + 3] = alphas[i / 256];
        }

        var path = Path.Combine(Mod("m"), "textures", "WALL.png");
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, Png.Encode(256, alphas.Length, rgba, Png.Channels.Rgba));
        var image = Open(3, "m").Texture("WALL", Texture("WALL"), Colours())!;

        var expected = (byte[])rgba.Clone();
        for (var i = 0; i < expected.Length; i += Rgba)
        {
            for (var c = 0; c < 3; c++)
            {
                expected[i + c] = (byte)((rgba[i + c] * rgba[i + 3] + 127) / 255);
            }
        }

        Assert.Equal(expected, image.Rgba);
    }

    /// <summary>Prefetched images are the ones a lookup would decode (the exact view, by name).</summary>
    [Fact]
    public void PrefetchesTheSameImages()
    {
        var palette = Colours();
        var keyed = Texture("K_RUN_0");
        Image(Path.Combine(Mod("m"), "textures", "LEVEL3", $"K_RUN_0@{HdKey.Of(keyed, palette)}.png"), 4, 4, 5, 128);
        Image(Path.Combine(_folder, "m", "textures", "K_RUN_1.png"), 4, 4, 9, 200);
        var plain = Open(8, "m");
        var prefetched = Open(8, "m");

        prefetched.Prefetch([("K_RUN_0", keyed), ("K_RUN_1", Texture("K_RUN_1")), ("K_NONE", Texture("K_NONE"))], palette);

        foreach (var name in new[] { "K_RUN_0", "K_RUN_1", "K_NONE" })
        {
            Assert.Equal(plain.Texture(name, Texture(name), palette)?.Rgba, prefetched.Texture(name, Texture(name), palette)?.Rgba);
        }

        Assert.NotNull(prefetched.Texture("K_RUN_0", keyed, palette));
    }
}
