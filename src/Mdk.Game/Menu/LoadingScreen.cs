using System.Drawing;
using System.Numerics;
using Mdk.Formats;
using Mdk.Game.Hud;

namespace Mdk.Game.Menu;

/// <summary>The loading screen (level_load 0x41b0c0, drawn by 0x422a10) on the 600 x 360 screen:
/// <c>MISC/LOAD_n.LBB</c> (768-byte palette, <c>u16 width, height</c>, pixels; 200 x 200) at
/// (200, 25), <c>LOAD_MSG</c> ("Loading") centred at y 260 and a progress bar from x 10 to 590,
/// y 290 to 310 (colour 3, framed by 4).</summary>
public sealed class LoadingScreen
{
    private static readonly Vector2 ImagePosition = new(200f, 25f);
    private const float TextY = 260f;
    private static readonly RectangleF Bar = new(10f, 290f, 580f, 20f);
    private const int PaletteSize = 768;
    private const int BarFill = 3;
    private const int BarFrame = 4;

    private readonly Ui _ui;
    /// <summary>The image, the font and the bar's colours; null without the screen's file.</summary>
    private readonly (PalettedImage Image, FontView Font, Vector4 Fill, Vector4 Frame)? _look;

    /// <summary>The loading screen of LEVELn, made once: it's redrawn as the level loads.</summary>
    public LoadingScreen(Ui ui, int level)
    {
        _ui = ui;
        var path = ui.Data.PathOf($"MISC/LOAD_{level}.LBB");
        var bytes = File.Exists(path) ? File.ReadAllBytes(path) : [];

        // The 1996 demo's screens have the same layout.
        if (BetaDemo.IsBeta(level) && ui.Beta != null)
        {
            bytes = ui.Beta.LoadingScreen(BetaDemo.LevelOf(level));
        }

        if (bytes.Length <= PaletteSize)
        {
            return;
        }

        var rgb = bytes[..PaletteSize];
        var palette = Palette.FromRgb(rgb);
        var image = PalettedImage.Of(ui.Renderer, Texture.Parse($"LOAD_{level}", bytes, PaletteSize), rgb, ui.CanvasMods());
        var font = new Fonts(ui.Renderer, ui.Fti, palette, ui.CanvasMods()).Big;
        _look = (image, font, Colour(palette, BarFill), Colour(palette, BarFrame));
    }

    /// <summary>Draws and shows one frame with the bar <paramref name="progress"/> (0 to 1) full.</summary>
    public void Show(float progress)
    {
        var view = _ui.View;
        view.Layout(ScreenView.Fit.Height);
        if (_look is var (image, font, fill, frame))
        {
            image.Draw(view, ImagePosition);
            var text = _ui.Fti.GetTextBytes("LOAD_MSG");
            view.Text(font, text, (ScreenView.Size.X - font.Width(text)) / 2f, TextY);
            view.Fill(new RectangleF(Bar.X, Bar.Y, Bar.Width * Math.Clamp(progress, 0f, 1f), Bar.Height), fill);
            _ui.Renderer.FrameRect(view.ToCanvas(Bar), view.Scale, frame);
        }

        // The level being loaded has set its sky and light already: the loading screen shows neither.
        var renderer = _ui.Renderer;
        var (sky, light) = (renderer.Panorama, renderer.Lighting);
        (renderer.Panorama, renderer.Lighting) = (null, null);
        _ui.Present(null);
        (renderer.Panorama, renderer.Lighting) = (sky, light);
    }

    private static Vector4 Colour(Palette palette, int index) =>
        new Vector4(palette.Rgba[index * 4], palette.Rgba[index * 4 + 1], palette.Rgba[index * 4 + 2], byte.MaxValue) / byte.MaxValue;
}
