using System.Drawing;
using System.Numerics;
using Mdk.Formats;

namespace Mdk.Game.Fall;

/// <summary>The fall's ground (0x41357c): not 3D, the texture <c>LEVELn</c> (1024²) mapped affinely
/// onto the view, zooming in as the camera drops and scrolling down the screen, with the
/// minecrawler's track (<c>PODn</c>) written into it under and behind the crawler.
/// <code>
///   u = 512 + 0.36 cx + S (sx − w/2)      S = cz / 5280 texels per pixel
///   v = A   − 0.36 cy + S (sy − 180)      A = 824 − 624 t / 33 (the crawler's row)
///
///   track rows from R = round(A − 33.75):   ┌──┐  16 + 2j/3 texels each side of 512 (row R + j)
///                                          ┌┘  └┐ 32 from 24 rows on
/// </code></summary>
public sealed class FallGround
{
    public const int Size = 1024;
    private const float CentreColumn = 512f;
    private const float Parallax = 0.36f;
    /// <summary>The camera height at which one texel is one pixel.</summary>
    public const float UnitHeight = 5280f;
    private const float StartRow = 824f;
    private const float RowsOverFall = 624f;
    /// <summary>The track starts this far ahead of the crawler's row (108 × 80 / 256).</summary>
    private const float TrackAhead = 108f * 80f / 256f;
    /// <summary>The track widens over 24 rows from 16 texels each side to 32.</summary>
    private const int TrackRamp = 24;
    private const float TrackNarrow = 16f;
    private const float TrackWiden = 2f / 3f;
    private const int TrackWide = 32;
    private const float ViewCentreY = 180f;

    private readonly byte[] _ground;
    private readonly Texture _pod;
    /// <summary>The first track row written so far (none yet: the bottom).</summary>
    private int _trackRow = Size;

    /// <summary>The ground with the track as written so far.</summary>
    public byte[] Indices { get; }

    public FallGround(Texture ground, Texture pod)
    {
        _ground = ground.Indices;
        _pod = pod;
        Indices = (byte[])_ground.Clone();
    }

    /// <summary>The texel row at the view's centre after <paramref name="time"/> seconds of fall.</summary>
    public static float CentreRow(float time) => StartRow - RowsOverFall * time / FallSim.EndTime;

    /// <summary>Half the track's width at row <paramref name="j"/> from its start.</summary>
    public static int TrackWidth(int j) => j < TrackRamp ? (int)MathF.Round(TrackNarrow + TrackWiden * j) : TrackWide;

    /// <summary>Writes the track from the crawler's row down; false when nothing changed.</summary>
    public bool UpdateTrack(float centreRow)
    {
        var row = Math.Clamp((int)MathF.Round(centreRow - TrackAhead), 0, Size);
        if (row >= _trackRow)
        {
            return false;
        }

        // Rows within 24 of the new start, and the ones that were within 24 of the old one.
        var end = Math.Min(_trackRow + TrackRamp, Size);
        for (var y = row; y < end; y++)
        {
            var w = TrackWidth(y - row);
            var podX = _pod.Width / 2 - w;
            Array.Copy(_pod.Indices, y * _pod.Width + podX, Indices, y * Size + (int)CentreColumn - w, 2 * w);
        }

        _trackRow = row;
        return true;
    }

    /// <summary>The part of the <paramref name="view"/> (view pixels, centred across, 0-360 at 4:3,
    /// taller on a narrower screen) the texture covers, and its texels; empty when the view is off
    /// the texture.</summary>
    public static (RectangleF Screen, RectangleF Texels) Visible(Vector3 camera, float centreRow, RectangleF view)
    {
        var s = camera.Z / UnitHeight;
        var u0 = CentreColumn + Parallax * camera.X - s * view.Width / 2f;
        var v0 = centreRow - Parallax * camera.Y + s * (view.Y - ViewCentreY);
        var texels = RectangleF.Intersect(new RectangleF(u0, v0, s * view.Width, s * view.Height), new RectangleF(0f, 0f, Size, Size));
        if (texels.Width <= 0f || texels.Height <= 0f || s <= 0f)
        {
            return (RectangleF.Empty, RectangleF.Empty);
        }

        var screen = new RectangleF(view.X + (texels.X - u0) / s, view.Y + (texels.Y - v0) / s, texels.Width / s, texels.Height / s);
        return (screen, texels);
    }

    /// <summary>The minecrawler on the screen: its centre (at texel (512, A)) and its scale.</summary>
    public static (Vector2 Centre, float Scale) Crawler(Vector3 camera, float width)
    {
        var s = camera.Z / UnitHeight;
        const float CrawlerScale = 0.75f;
        return (new Vector2(width / 2f - Parallax * camera.X / s, ViewCentreY + Parallax * camera.Y / s), CrawlerScale / s);
    }
}
