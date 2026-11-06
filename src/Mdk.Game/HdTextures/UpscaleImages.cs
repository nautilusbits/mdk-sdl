namespace Mdk.Game.HdTextures;

/// <summary>An HD image's cover: hard (0 or 255: surfaces' cut-outs), or soft (the source's, smooth:
/// sprites, whose outline the shader cuts at half cover as smoothly as the original's).</summary>
public enum HdAlpha { Hard, Soft }

/// <summary>A texture frame before and after the upscaler (RGBA8, straight alpha). The upscaler
/// sees colour only: clear texels take their neighbours' colour first (no dark fringe where a
/// cut-out ends), and the frame wraps around by a margin, so a tiling texture's edges are upscaled
/// with what they meet. After: the margin cut, the size brought to the cache's scale (a box filter),
/// and the alpha the source's, upscaled and kept hard (0 or 255: cut-outs stay cut-outs), or soft
/// for sprites (<see cref="HdAlpha"/>).
/// <code>
///   ┌───────────┐            ┌─────────────┐
///   │ m  wrap   │  upscaler  │             │   crop m × factor ─► box ÷ (factor / scale)
///   │  ┌─────┐  │  ───────►  │   × factor  │   alpha: source's, bilinear, ≥ ½ ─► 255
///   │  │frame│  │            │             │
///   │  └─────┘  │            │             │
///   └───────────┘            └─────────────┘
/// </code></summary>
public static class UpscaleImages
{
    private const int Channels = 4;
    private const int Alpha = 3;
    private const byte Opaque = 255;
    private const byte HalfCover = 128;

    /// <summary>The size of a frame with its margin all around.</summary>
    public static (int Width, int Height) Padded(int width, int height, int margin) => (width + margin * 2, height + margin * 2);

    /// <summary>The upscaler's input: clear texels are bled; the margin wraps horizontally and wraps or clamps vertically.</summary>
    /// <param name="wrapY">Whether to wrap vertically; false clamps to the nearest row.</param>
    public static byte[] Input(byte[] rgba, int width, int height, int margin, bool wrapY = true)
    {
        var bled = Bleed(rgba, width, height);
        var (paddedWidth, paddedHeight) = Padded(width, height, margin);
        var padded = new byte[paddedWidth * paddedHeight * Channels];
        for (var y = 0; y < paddedHeight; y++)
        {
            var sy = wrapY ? Wrap(y - margin, height) : Math.Clamp(y - margin, 0, height - 1);
            for (var x = 0; x < paddedWidth; x++)
            {
                var sx = Wrap(x - margin, width);
                bled.AsSpan((sy * width + sx) * Channels, Channels).CopyTo(padded.AsSpan((y * paddedWidth + x) * Channels));
            }
        }

        return padded;
    }

    private static int Wrap(int value, int size) => ((value % size) + size) % size;

    /// <summary>Clear texels take the mean colour of their opaque (or already filled) neighbours, ring
    /// after ring, until none is left (an all clear frame stays black). All end opaque.</summary>
    private static byte[] Bleed(byte[] rgba, int width, int height)
    {
        var result = (byte[])rgba.Clone();
        var filled = new bool[width * height];
        var pending = new List<int>();
        for (var i = 0; i < filled.Length; i++)
        {
            filled[i] = rgba[i * Channels + Alpha] != 0;
            if (!filled[i])
            {
                pending.Add(i);
            }
        }

        var ring = new List<int>();
        while (pending.Count > 0)
        {
            ring.Clear();
            foreach (var i in pending)
            {
                if (Mean(result, filled, i % width, i / width, width, height))
                {
                    ring.Add(i);
                }
            }

            if (ring.Count == 0)
            {
                break;
            }

            foreach (var i in ring)
            {
                filled[i] = true;
            }

            pending.RemoveAll(i => filled[i]);
        }

        for (var i = 0; i < filled.Length; i++)
        {
            result[i * Channels + Alpha] = Opaque;
        }

        return result;
    }

    private static readonly (int X, int Y)[] Neighbours = [(-1, 0), (1, 0), (0, -1), (0, 1)];

    /// <summary>Writes the mean colour of a texel's filled neighbours into it; false when it has none.</summary>
    private static bool Mean(byte[] rgba, bool[] filled, int x, int y, int width, int height)
    {
        Span<int> sum = stackalloc int[Alpha];
        var count = 0;
        foreach (var (dx, dy) in Neighbours)
        {
            var (nx, ny) = (x + dx, y + dy);
            if (nx < 0 || ny < 0 || nx >= width || ny >= height || !filled[ny * width + nx])
            {
                continue;
            }

            for (var c = 0; c < Alpha; c++)
            {
                sum[c] += rgba[(ny * width + nx) * Channels + c];
            }

            count++;
        }

        if (count == 0)
        {
            return false;
        }

        for (var c = 0; c < Alpha; c++)
        {
            rgba[(y * width + x) * Channels + c] = (byte)((sum[c] + count / 2) / count);
        }

        return true;
    }

    /// <summary>The cache's frame (<paramref name="scale"/> × the source's size) from the upscaler's
    /// output of <see cref="Input"/> (<paramref name="factor"/> × its size).</summary>
    public static byte[] Output(byte[] upscaled, int factor, byte[] source, int width, int height, int margin, int scale,
        HdAlpha alpha = HdAlpha.Hard)
    {
        var upscaledWidth = Padded(width, height, margin).Width * factor;
        var shrink = factor / scale;
        var (outWidth, outHeight) = (width * scale, height * scale);
        var result = new byte[outWidth * outHeight * Channels];
        var opaque = AllOpaque(source);
        for (var y = 0; y < outHeight; y++)
        {
            for (var x = 0; x < outWidth; x++)
            {
                var target = (y * outWidth + x) * Channels;
                Box(upscaled, upscaledWidth, margin * factor + x * shrink, margin * factor + y * shrink, shrink, result.AsSpan(target, Alpha));
                result[target + Alpha] = opaque ? Opaque : Cover(source, width, height, (x + 0.5f) / scale - 0.5f, (y + 0.5f) / scale - 0.5f, alpha);
            }
        }

        return result;
    }

    private static bool AllOpaque(byte[] rgba)
    {
        for (var i = Alpha; i < rgba.Length; i += Channels)
        {
            if (rgba[i] != Opaque)
            {
                return false;
            }
        }

        return true;
    }

    /// <summary>The mean colour of a <paramref name="size"/>² block.</summary>
    private static void Box(byte[] rgba, int width, int left, int top, int size, Span<byte> colour)
    {
        Span<int> sum = stackalloc int[Alpha];
        for (var y = top; y < top + size; y++)
        {
            for (var x = left; x < left + size; x++)
            {
                for (var c = 0; c < Alpha; c++)
                {
                    sum[c] += rgba[(y * width + x) * Channels + c];
                }
            }
        }

        var count = size * size;
        for (var c = 0; c < Alpha; c++)
        {
            colour[c] = (byte)((sum[c] + count / 2) / count);
        }
    }

    /// <summary>The source's alpha at a point (texel centres at whole numbers, edges clamped),
    /// bilinear, then hard (opaque from half cover on) or as it is.</summary>
    private static byte Cover(byte[] rgba, int width, int height, float x, float y, HdAlpha alpha)
    {
        x = Math.Clamp(x, 0f, width - 1);
        y = Math.Clamp(y, 0f, height - 1);
        var (x0, y0) = ((int)x, (int)y);
        var (x1, y1) = (Math.Min(x0 + 1, width - 1), Math.Min(y0 + 1, height - 1));
        var (fx, fy) = (x - x0, y - y0);
        float At(int ax, int ay) => rgba[(ay * width + ax) * Channels + Alpha];
        var top = float.Lerp(At(x0, y0), At(x1, y0), fx);
        var bottom = float.Lerp(At(x0, y1), At(x1, y1), fx);
        var cover = float.Lerp(top, bottom, fy);
        if (alpha == HdAlpha.Soft)
        {
            return (byte)MathF.Round(cover);
        }

        return cover >= HalfCover ? Opaque : (byte)0;
    }
}
