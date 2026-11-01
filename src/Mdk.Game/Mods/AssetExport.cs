using Mdk.Formats;
using Mdk.Game.Audio;
using Mdk.Game.HdTextures;
using Mdk.Game.Kurt;
using Mdk.Game.Level;

namespace Mdk.Game.Mods;

/// <summary>The game's assets as a mod's files, for modders to start from (<c>--export-assets</c>):
/// read from the user's game files, written where asked, in the layout and names the loader reads
/// (copy what you change into <c>mods/&lt;name&gt;/</c>). Each level's own, through its palettes (the
/// first arena's that shows it); the menus' 2D images once.
/// <code>
///   textures/LEVELn/NAME.png   (animated: NAME_0.png, NAME_1.png...; Kurt: K_RUN_0.png...)
///   images/LEVELn/NAME.png     the level's HUD        images/NAME.png   menus, fonts
///   models/LEVELn/NAME.glb     parts as named nodes, rest pose, textures embedded
/// </code></summary>
public static class AssetExport
{
    public static string Run(MdkData data, string folder)
    {
        var (textures, images, models) = (0, 0, 0);
        var sprites = Bni.Load(data.PathOf("TRAVERSE/TRAVSPRT.BNI"));
        foreach (var number in HdOptions.AllLevels)
        {
            var level = new LevelData(data, number);
            var cmi = Cmi.Load(data.PathOf($"TRAVERSE/LEVEL{number}/LEVEL{number}.CMI"));
            var levelFolder = ModImages.LevelFolder(number);

            // Each name once: the first arena's palette.
            var sources = TextureExport.Collect(level, cmi)
                .Concat(TextureExport.Kurt(level, sprites, SoundBank.ForLevel(data, number).Animation))
                .DistinctBy(s => s.Name);
            var textureFolder = Path.Combine(folder, ModImages.FolderOf(ModImages.Kind.Textures), levelFolder);
            foreach (var source in sources)
            {
                WriteTexture(textureFolder, source.Name, source.Texture, source.Palette);
                textures++;
            }

            WriteTexture(textureFolder, level.Dti.Sky.Name, level.Dti.Sky, level.Dti.Palette);
            textures++;

            var imageFolder = Path.Combine(folder, ModImages.FolderOf(ModImages.Kind.Images), levelFolder);
            foreach (var source in CanvasExport.Level(level, sprites).DistinctBy(s => s.Name))
            {
                WriteTexture(imageFolder, source.Name, source.Texture, source.Palette);
                images++;
            }

            var modelFolder = Path.Combine(folder, ModModels.FolderName, levelFolder);
            models += WriteModels(modelFolder, level, cmi);
            Console.WriteLine($"LEVEL{number}: {textures} textures, {images} images, {models} models so far");
        }

        foreach (var source in CanvasExport.Menus(data, CanvasExport.Fonts.Listed).DistinctBy(s => s.Name))
        {
            WriteTexture(Path.Combine(folder, ModImages.FolderOf(ModImages.Kind.Images)), source.Name, source.Texture, source.Palette);
            images++;
        }

        return $"{textures} textures, {images} images, {models} models";
    }

    /// <summary>A texture through a palette as PNG: one file, or one per frame (index 0 clear).</summary>
    public static void WriteTexture(string folder, string name, Texture texture, Palette palette)
    {
        Directory.CreateDirectory(folder);
        for (var f = 0; f < texture.FrameCount; f++)
        {
            var file = texture.FrameCount == 1 ? $"{name}.png" : $"{name}_{f}.png";
            var rgba = TextureExport.Frame(texture, palette, f);
            File.WriteAllBytes(Path.Combine(folder, file), Png.Encode(texture.Width, texture.Height, rgba, Png.Channels.Rgba));
        }
    }

    /// <summary>The level's models (the scripts' and the arenas'), each name once, through the first
    /// arena's look (the scripts' through the level's palette).</summary>
    private static int WriteModels(string folder, LevelData level, Cmi cmi)
    {
        Directory.CreateDirectory(folder);
        var written = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var levelModels = cmi.ModelOffsets.Keys.Select(cmi.GetModel).OfType<Model>()
            .Select(m => (Model: m, Palette: level.Dti.Palette, Archives: (IReadOnlyList<TextureArchive>)[level.LevelTextures]));
        var arenaModels = level.Arenas.SelectMany(a => a.Models.Values
            .Select(m => (Model: m, Palette: level.PaletteOf(a), Archives: (IReadOnlyList<TextureArchive>)level.ArchivesOf(a))));
        foreach (var (model, palette, archives) in levelModels.Concat(arenaModels))
        {
            if (!written.Add(model.Name))
            {
                continue;
            }

            var bytes = Glb.Write(ModelExport.Scene(model, palette, archives));
            File.WriteAllBytes(Path.Combine(folder, $"{model.Name}.glb"), bytes);
        }

        return written.Count;
    }
}
