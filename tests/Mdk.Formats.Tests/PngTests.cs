using System.IO.Compression;

namespace Mdk.Formats.Tests;

/// <summary>PNG: what <see cref="Png.Encode"/> writes decodes back, and the upscaler's files
/// (RGB, every row filter) decode.</summary>
public class PngTests
{
    [Fact]
    public void RoundTrips()
    {
        const int width = 5;
        const int height = 3;
        var rgba = Enumerable.Range(0, width * height * 4).Select(i => (byte)(i * 7)).ToArray();

        var image = Png.Decode(Png.Encode(width, height, rgba, Png.Channels.Rgba));

        Assert.Equal(width, image.Width);
        Assert.Equal(height, image.Height);
        Assert.Equal(rgba, image.Rgba);
    }

    [Fact]
    public void RgbIsOpaque()
    {
        var rgba = new byte[] { 10, 20, 30, 0, 40, 50, 60, 128 };

        var image = Png.Decode(Png.Encode(2, 1, rgba, Png.Channels.Rgb));

        Assert.Equal(new byte[] { 10, 20, 30, 255, 40, 50, 60, 255 }, image.Rgba);
    }

    /// <summary>Rows filtered None, Sub, Up, Average and Paeth (as other encoders write them).</summary>
    [Fact]
    public void DecodesEveryFilter()
    {
        const int width = 3;
        const int height = 5;
        var rgb = Enumerable.Range(0, width * height * 3).Select(i => (byte)(i * 13 + 5)).ToArray();
        var filtered = Filter(rgb, width * 3, 3, height);

        var image = Png.Decode(Build(width, height, filtered));

        for (var i = 0; i < width * height; i++)
        {
            Assert.Equal(rgb.AsSpan(i * 3, 3).ToArray(), image.Rgba.AsSpan(i * 4, 3).ToArray());
            Assert.Equal(255, image.Rgba[i * 4 + 3]);
        }
    }

    [Fact]
    public void RejectsOtherFiles()
    {
        Assert.Throws<InvalidDataException>(() => Png.Decode([1, 2, 3, 4, 5, 6, 7, 8, 9]));
    }

    /// <summary>Crafted files (a mod from the internet) are refused, never a crash: sizes whose
    /// buffers overflow or exhaust memory, a chunk's length near the int's end.</summary>
    [Theory]
    [InlineData(0x40000000, 1)]
    [InlineData(0x7FFFFFFF, 0x7FFFFFFF)]
    [InlineData(40000, 40000)]
    public void RefusesHugeSizes(int width, int height)
    {
        Assert.Throws<InvalidDataException>(() => Png.Decode(Build(width, height, [0, 1, 2, 3])));
    }

    [Fact]
    public void RefusesAChunkPastTheEnd()
    {
        var bytes = Build(1, 1, [0, 1, 2, 3]);
        System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(bytes.AsSpan(8), int.MaxValue - 4);

        Assert.Throws<InvalidDataException>(() => Png.Decode(bytes));
    }

    /// <summary>Row y filtered with filter y % 5 (RGB, 3 bytes a pixel).</summary>
    private static byte[] Filter(byte[] raw, int stride, int bpp, int height)
    {
        var output = new List<byte>();
        for (var y = 0; y < height; y++)
        {
            var filter = y % 5;
            output.Add((byte)filter);
            for (var x = 0; x < stride; x++)
            {
                int a = x >= bpp ? raw[y * stride + x - bpp] : 0;
                int b = y > 0 ? raw[(y - 1) * stride + x] : 0;
                int c = x >= bpp && y > 0 ? raw[(y - 1) * stride + x - bpp] : 0;
                var predicted = filter switch
                {
                    1 => a,
                    2 => b,
                    3 => (a + b) / 2,
                    4 => Paeth(a, b, c),
                    _ => 0,
                };
                output.Add((byte)(raw[y * stride + x] - predicted));
            }
        }

        return output.ToArray();
    }

    private static int Paeth(int a, int b, int c)
    {
        var p = a + b - c;
        var (pa, pb, pc) = (Math.Abs(p - a), Math.Abs(p - b), Math.Abs(p - c));
        return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
    }

    /// <summary>A PNG of 8-bit RGB with these filtered rows (CRCs left zero: the decoder ignores them).</summary>
    private static byte[] Build(int width, int height, byte[] filtered)
    {
        using var stream = new MemoryStream();
        stream.Write([0x89, (byte)'P', (byte)'N', (byte)'G', 0x0D, 0x0A, 0x1A, 0x0A]);
        var header = new byte[13];
        System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(header, width);
        System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(header.AsSpan(4), height);
        header[8] = 8;
        header[9] = 2;
        Chunk(stream, "IHDR", header);
        using var compressed = new MemoryStream();
        using (var zlib = new ZLibStream(compressed, CompressionLevel.Fastest, leaveOpen: true))
        {
            zlib.Write(filtered);
        }

        Chunk(stream, "IDAT", compressed.ToArray());
        Chunk(stream, "IEND", []);
        return stream.ToArray();
    }

    private static void Chunk(Stream stream, string type, byte[] data)
    {
        var length = new byte[4];
        System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(length, data.Length);
        stream.Write(length);
        stream.Write(System.Text.Encoding.ASCII.GetBytes(type));
        stream.Write(data);
        stream.Write(new byte[4]);
    }
}
