using System.Drawing;
using System.Numerics;
using Mdk.Engine.Audio;
using Mdk.Engine.Platform;
using Mdk.Engine.Render;
using Mdk.Formats;
using Mdk.Game.DevTools;
using Mdk.Game.Flow;
using Mdk.Game.Hud;
using Mdk.Game.Mods;

namespace Mdk.Game.Menu;

/// <summary>What the game's screens share: the data, the engine, the settings and <c>MDKFONT.FTI</c>.
/// GPU resources are not kept here: each screen creates its own (see <see cref="Renderer.Release"/>).</summary>
public sealed class Ui(MdkData data, Window window, Renderer renderer, AudioDevice audio, Input input, Settings settings, string userFolder)
{
    public MdkData Data => data;
    public Window Window => window;
    public Renderer Renderer => renderer;
    public AudioDevice Audio => audio;
    public Input Input => input;
    public Settings Settings => settings;
    /// <summary>Where the settings and the saved games are kept.</summary>
    public string UserFolder => userFolder;
    public Fti Fti { get; } = Fti.Load(data.PathOf("MISC/MDKFONT.FTI"));
    public ScreenView View { get; } = new(renderer);
    /// <summary>The 1996 demo, when it's found (its levels are extras).</summary>
    public BetaDemo? Beta { get; } = BetaDemo.Find(data);
    /// <summary>The console's and the debug overlay's session: the log, timings, history.</summary>
    public DevSession Dev { get; } = new();
    /// <summary>Imports the game files again (Android); null elsewhere.</summary>
    public Action? Import { get; init; }
    /// <summary>The touch layout editor without a touch screen (--touch-layout; tests).</summary>
    public bool TouchEditor { get; init; }
    /// <summary>The mods found in the user folder (<see cref="ModCatalog"/>); the Mods page scans again.</summary>
    public ModCatalog Mods { get; private set; } = ModCatalog.Scan(ModCatalog.FolderIn(userFolder));

    public void ScanMods()
    {
        Mods.Close();
        Mods = ModCatalog.Scan(ModCatalog.FolderIn(userFolder));
        _menuImages = null;
    }

    /// <summary>The enabled mods' images for a level (null: the menus).</summary>
    public ModImages ModImages(int? level) => Mods.ModImages(settings, level);

    /// <summary>The menus' mod images (2D: drawn by the enhanced look only), kept while the same mods are on.</summary>
    public ModImages MenuImages
    {
        get
        {
            var folders = string.Join(FolderSeparator, Mods.Files(settings).Select(f => f.Where("")));
            if (_menuImages == null || folders != _menuFolders)
            {
                (_menuImages, _menuFolders) = (ModImages(null), folders);
            }

            return _menuImages;
        }
    }

    /// <summary>Mods' 2D images for a screen (null: the menus'): none in the original look, which draws the originals.</summary>
    public ModImages? CanvasMods(int? level = null) =>
        settings.Graphics != Graphics.Enhanced ? null : level is { } number ? ModImages(number) : MenuImages;

    private const char FolderSeparator = '|';
    private ModImages? _menuImages;
    private string _menuFolders = "";

    /// <summary>The settings changed: applied, and saved.</summary>
    public void ApplySettings()
    {
        settings.Apply(audio, window, renderer, input);
        settings.Save(Settings.PathIn(userFolder));
    }

    /// <summary>A 2D frame (no 3D scene) on black.</summary>
    public void Present(string? screenshot) => renderer.Present(default, Black, screenshot);

    public static readonly Vector4 Black = new(0f, 0f, 0f, 1f);
    public static readonly Vector4 White = Vector4.One;

    /// <summary>A sound of a WAV file, or null.</summary>
    public static Sound? SoundOf(byte[] wav, Looping looping = Looping.Once)
    {
        var parsed = Wav.Parse(wav);
        return parsed == null ? null : Sound.FromPcm(parsed.Data, parsed.Channels, parsed.SampleRate, parsed.BitsPerSample, looping);
    }

    /// <summary>A BNI entry's WAV as a sound, or null.</summary>
    public static Sound? SoundOf(Bni bni, string name, Looping looping = Looping.Once) =>
        bni.Has(name) ? SoundOf(BytesOf(bni, name), looping) : null;

    /// <summary>A BNI entry's bytes.</summary>
    public static byte[] BytesOf(Bni bni, string name)
    {
        var entry = bni.Entries[name];
        return bni.Bytes.AsSpan(entry.Offset, entry.Size).ToArray();
    }

    /// <summary>Plays a sound (at full volume of its bus); returns the voice.</summary>
    public int Play(Sound? sound, Bus bus = Bus.Effects, float gain = 1f) => sound == null ? 0 : audio.Play(sound, gain, 1f, 0f, bus);
}

/// <summary>The original's 600 x 360 screen on the canvas's safe area (clear of cut-outs): scaled
/// to fit, centred, black bars around it, never cut (menus, statistics, videos). The loading
/// screen fills a 4:3 canvas's height as the original's, its sides cut; other screens fit it too.
/// <code>
///   canvas ┌──────────────────────────┐
///          │   ┌──────────────────┐   │  origin + view point × scale
///          │   │  600 x 360 view  │   │
///          │   └──────────────────┘   │
///          └──────────────────────────┘
/// </code></summary>
public sealed class ScreenView(Renderer renderer)
{
    public static readonly Vector2 Size = new(600f, 360f);

    public enum Fit { Inside, Height }

    /// <summary>The 4:3 canvas the loading screen's height fills (within a pixel's rounding).</summary>
    private const float FourByThree = 4f / 3f;
    private const float AspectTolerance = 0.01f;

    public float Scale { get; private set; } = 1f;
    public Vector2 Origin { get; private set; }

    /// <summary>Places a <paramref name="size"/> view (the 600 x 360 screen, or a movie's) on the canvas.</summary>
    public void Layout(Fit fit, Vector2? size = null)
    {
        var safe = Centred(renderer.CanvasSafeArea, new Vector2(renderer.CanvasWidth, Renderer.CanvasHeight));
        (Scale, Origin) = Place(fit, size ?? Size, new Vector2(safe.Width, safe.Height));
        Origin += new Vector2(safe.X, safe.Y);
    }

    /// <summary>The safe area made even about the canvas's centre (each side's inset the larger
    /// of its and the opposite side's): a cut-out on one side doesn't push the screen off centre.
    /// <code>
    ///   canvas 800 wide, safe 69 ─ 774 (cut-out left) ─► 69 ─ 731
    /// </code></summary>
    public static RectangleF Centred(RectangleF safe, Vector2 canvas)
    {
        var x = MathF.Max(safe.Left, canvas.X - safe.Right);
        var y = MathF.Max(safe.Top, canvas.Y - safe.Bottom);
        return RectangleF.FromLTRB(x, y, canvas.X - x, canvas.Y - y);
    }

    /// <summary>A view's scale and origin on a canvas: inside it, or (a 4:3 canvas) to its height.
    /// <code>
    ///   600 x 360 on 480 x 360 (4:3), Height: × 1,    x from -60 (sides cut)
    ///   600 x 360 on 576 x 360 (16:10):       × 0.96, y from 7.2 (bars)
    /// </code></summary>
    public static (float Scale, Vector2 Origin) Place(Fit fit, Vector2 view, Vector2 canvas)
    {
        var height = fit == Fit.Height && MathF.Abs(canvas.X / canvas.Y - FourByThree) < AspectTolerance;
        var scale = height ? canvas.Y / view.Y : MathF.Min(canvas.X / view.X, canvas.Y / view.Y);
        return (scale, (canvas - view * scale) / 2f);
    }

    public Vector2 ToCanvas(Vector2 point) => Origin + point * Scale;

    public RectangleF ToCanvas(RectangleF area) =>
        new(Origin.X + area.X * Scale, Origin.Y + area.Y * Scale, area.Width * Scale, area.Height * Scale);

    /// <summary>The pointer (window coordinates) on the view.</summary>
    public Vector2 Pointer(Input input) => (renderer.CanvasPoint(input.PointerX, input.PointerY) - Origin) / Scale;

    public void Fill(RectangleF area, Vector4 colour) => renderer.FillRect(ToCanvas(area), colour);

    /// <summary>The whole canvas.</summary>
    public void FillCanvas(Vector4 colour) =>
        renderer.FillRect(new RectangleF(0f, 0f, renderer.CanvasWidth, Renderer.CanvasHeight), colour);

    /// <summary>A text from x along the baseline y (view coordinates), scaled about the baseline.</summary>
    public void Text(FontView font, ReadOnlySpan<byte> text, float x, float y, float scale = 1f, float alpha = 1f)
    {
        var at = ToCanvas(new Vector2(x, y));
        font.Draw(text, at.X, at.Y, scale * Scale, alpha);
    }
}

/// <summary>The interface fonts of <c>MDKFONT.FTI</c> in its <c>SYS_PAL</c> colours.</summary>
public sealed class Fonts
{
    private const int BigSpace = 6;
    private const int SmallSpace = 4;

    public FontView Big { get; }
    public FontView Small { get; }

    /// <summary>The fonts; <paramref name="mods"/>' images of their atlases replace them.</summary>
    public Fonts(Renderer renderer, Fti fti, Palette? palette = null, ModImages? mods = null)
    {
        var colours = palette ?? Palette.FromRgb(fti.GetBytes("SYS_PAL"));
        Big = new FontView(renderer, Font.Parse(fti.GetBytes(FontView.Big), BigSpace), colours, mods, FontView.Big);
        Small = new FontView(renderer, Font.Parse(fti.GetBytes(FontView.Small), SmallSpace), colours, mods, FontView.Small);
    }
}

/// <summary>An image of palette indices with its palette on the GPU; both can change (videos,
/// fades).</summary>
public sealed class PalettedImage
{
    private const int PaletteBytes = 256 * 4;

    private readonly Renderer _renderer;
    private readonly int _texture;
    private readonly int _palette;

    public int Width { get; }
    public int Height { get; }
    public Vector2 Size => new(Width, Height);

    public PalettedImage(Renderer renderer, int width, int height, byte[] indices, byte[] paletteRgb)
    {
        _renderer = renderer;
        Width = width;
        Height = height;
        _texture = renderer.CreateIndexTexture(width, height, indices);
        _palette = renderer.CreatePalette(Palette.FromRgb(paletteRgb).Rgba);
    }

    /// <summary>An image of a texture (BNI, LBB) with a palette of 768 RGB bytes; a mod's image of
    /// its name replaces it (<see cref="CanvasImages"/>).</summary>
    public static PalettedImage Of(Renderer renderer, Texture texture, byte[] paletteRgb, ModImages? mods = null)
    {
        var image = new PalettedImage(renderer, texture.Width, texture.Height, texture.Indices, paletteRgb);
        CanvasImages.Replace(renderer, mods, texture.Name, texture, Palette.FromRgb(paletteRgb), image._texture, image._palette);
        return image;
    }

    public void SetIndices(byte[] indices) => _renderer.UpdateTexture(_texture, Width, Height, indices);

    public void SetPalette(byte[] paletteRgb)
    {
        var rgba = Palette.FromRgb(paletteRgb).Rgba;
        if (rgba.Length == PaletteBytes)
        {
            _renderer.UpdateTexture(_palette, rgba.Length / 4, 1, rgba);
        }
    }

    /// <summary>Draws it at <paramref name="at"/> of the view, at its size.</summary>
    public void Draw(ScreenView view, Vector2 at) =>
        _renderer.DrawImage(_texture, _palette, Size, new RectangleF(0f, 0f, Width, Height),
            view.ToCanvas(new RectangleF(at.X, at.Y, Width, Height)), Vector4.One);
}
