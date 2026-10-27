using System.Buffers.Binary;
using System.IO.Compression;
using System.Runtime.Intrinsics;
using System.Text;

namespace Mdk.Formats;

/// <summary>PNG images of 8-bit channels, not interlaced: written as RGB or RGBA (rows unfiltered),
/// read as grey, grey and alpha, RGB or RGBA (every row filter), always into RGBA8.
/// <code>
///   signature, IHDR (width, height, depth 8, colour type), IDAT... (zlib: per row a filter byte, then
///   the row), IEND; each chunk: u32 length, type, data, CRC-32 (big-endian)
/// </code></summary>
public static class Png
{
    /// <summary>The channels written.</summary>
    public enum Channels { Rgb, Rgba }

    public sealed record Image(int Width, int Height, byte[] Rgba);

    private static readonly byte[] Signature = [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A];
    private const int HeaderSize = 13;
    private const int Depth = 8;
    private const byte Opaque = 255;
    private const int Rgba = 4;
    /// <summary>The widest and tallest read (256 MB as RGBA8; mods' HD images are far smaller):
    /// larger sizes would overflow the buffers or exhaust memory.</summary>
    private const int MaxSide = 8192;

    /// <summary>Colour types (IHDR byte 9) and their bytes per pixel.</summary>
    private const byte Grey = 0;
    private const byte Rgb = 2;
    private const byte GreyAlpha = 4;
    private const byte RgbAlpha = 6;

    /// <summary>Row filters: none, left, up, mean of left and up, Paeth.</summary>
    private const byte FilterNone = 0;
    private const byte FilterSub = 1;
    private const byte FilterUp = 2;
    private const byte FilterAverage = 3;
    private const byte FilterPaeth = 4;

    private static readonly uint[] CrcTable = MakeCrcTable();

    /// <summary>An RGBA8 image as a PNG of <paramref name="channels"/> (RGB drops the alpha).</summary>
    public static byte[] Encode(int width, int height, ReadOnlySpan<byte> rgba, Channels channels)
    {
        var bpp = channels == Channels.Rgb ? 3 : Rgba;
        var stride = width * bpp;
        var raw = new byte[(stride + 1) * height];
        for (var y = 0; y < height; y++)
        {
            var row = raw.AsSpan(y * (stride + 1) + 1, stride);
            for (var x = 0; x < width; x++)
            {
                rgba.Slice((y * width + x) * Rgba, bpp).CopyTo(row[(x * bpp)..]);
            }
        }

        using var compressed = new MemoryStream();
        using (var zlib = new ZLibStream(compressed, CompressionLevel.Optimal, leaveOpen: true))
        {
            zlib.Write(raw);
        }

        var header = new byte[HeaderSize];
        BinaryPrimitives.WriteInt32BigEndian(header, width);
        BinaryPrimitives.WriteInt32BigEndian(header.AsSpan(4), height);
        header[8] = Depth;
        header[9] = channels == Channels.Rgb ? Rgb : RgbAlpha;

        using var file = new MemoryStream();
        file.Write(Signature);
        WriteChunk(file, "IHDR", header);
        WriteChunk(file, "IDAT", compressed.ToArray());
        WriteChunk(file, "IEND", []);
        return file.ToArray();
    }

    /// <summary>A PNG's pixels as RGBA8 (opaque without alpha).</summary>
    public static Image Decode(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length < Signature.Length || !bytes[..Signature.Length].SequenceEqual(Signature))
        {
            throw new InvalidDataException("Not a PNG");
        }

        var (width, height, type) = (0, 0, (byte)0);
        using var data = new MemoryStream();
        var at = Signature.Length;
        while (at + 8 <= bytes.Length)
        {
            var length = BinaryPrimitives.ReadInt32BigEndian(bytes[at..]);
            var name = Encoding.ASCII.GetString(bytes.Slice(at + 4, 4));
            if (length < 0 || length > bytes.Length - at - 12)
            {
                throw new InvalidDataException("PNG chunk past the end");
            }

            var chunk = bytes.Slice(at + 8, length);
            at += 12 + length;
            if (name == "IHDR")
            {
                (width, height, type) = ReadHeader(chunk);
            }
            else if (name == "IDAT")
            {
                data.Write(chunk);
            }
            else if (name == "IEND")
            {
                break;
            }
        }

        if (width <= 0 || height <= 0)
        {
            throw new InvalidDataException("PNG without a header");
        }

        if (width > MaxSide || height > MaxSide)
        {
            throw new InvalidDataException($"PNG of {width}x{height}, over {MaxSide}x{MaxSide}");
        }

        var bpp = BytesPerPixel(type);
        var raw = Inflate(data, (width * bpp + 1) * height);
        Unfilter(raw, width * bpp, bpp, height);
        return new Image(width, height, ToRgba(raw, width, height, bpp));
    }

    private static (int Width, int Height, byte Type) ReadHeader(ReadOnlySpan<byte> header)
    {
        const int interlace = 12;
        if (header.Length < HeaderSize || header[8] != Depth || header[interlace] != 0)
        {
            throw new InvalidDataException("PNG not of 8-bit channels, or interlaced");
        }

        return (BinaryPrimitives.ReadInt32BigEndian(header), BinaryPrimitives.ReadInt32BigEndian(header[4..]), header[9]);
    }

    private static int BytesPerPixel(byte type) => type switch
    {
        Grey => 1,
        GreyAlpha => 2,
        Rgb => 3,
        RgbAlpha => Rgba,
        _ => throw new InvalidDataException($"PNG colour type {type} not read"),
    };

    private static byte[] Inflate(MemoryStream data, int size)
    {
        data.Position = 0;
        using var zlib = new ZLibStream(data, CompressionMode.Decompress);
        var raw = new byte[size];
        zlib.ReadExactly(raw);
        return raw;
    }

    /// <summary>Undoes each row's filter in place (the filter byte stays before the row). The first
    /// row's "up" is a row of zeros.</summary>
    private static void Unfilter(byte[] raw, int stride, int bpp, int height)
    {
        ReadOnlySpan<byte> previous = new byte[stride];
        for (var y = 0; y < height; y++)
        {
            var start = y * (stride + 1) + 1;
            var row = raw.AsSpan(start, stride);
            var filter = raw[start - 1];
            switch (filter)
            {
                case FilterNone:
                    break;
                case FilterSub:
                    UnfilterSub(row, bpp);
                    break;
                case FilterUp:
                    UnfilterUp(row, previous);
                    break;
                case FilterAverage:
                    UnfilterAverage(row, previous, bpp);
                    break;
                case FilterPaeth:
                    UnfilterPaeth(row, previous, bpp);
                    break;
                default:
                    throw new InvalidDataException($"PNG row filter {filter}");
            }

            previous = row;
        }
    }

    private static void UnfilterSub(Span<byte> row, int bpp)
    {
        for (var x = bpp; x < row.Length; x++)
        {
            row[x] += row[x - bpp];
        }
    }

    private static void UnfilterUp(Span<byte> row, ReadOnlySpan<byte> previous)
    {
        // Bytes wrap, so whole vectors add as the bytes do.
        var x = 0;
        for (; x <= row.Length - Vector128<byte>.Count; x += Vector128<byte>.Count)
        {
            (Vector128.Create(row[x..]) + Vector128.Create(previous[x..])).CopyTo(row[x..]);
        }

        for (; x < row.Length; x++)
        {
            row[x] += previous[x];
        }
    }

    private static void UnfilterAverage(Span<byte> row, ReadOnlySpan<byte> previous, int bpp)
    {
        for (var x = 0; x < bpp; x++)
        {
            row[x] += (byte)(previous[x] / 2);
        }

        for (var x = bpp; x < row.Length; x++)
        {
            row[x] += (byte)((row[x - bpp] + previous[x]) / 2);
        }
    }

    private static void UnfilterPaeth(Span<byte> row, ReadOnlySpan<byte> previous, int bpp)
    {
        // Left of the first pixel is zero: Paeth(0, b, 0) is b.
        for (var x = 0; x < bpp; x++)
        {
            row[x] += previous[x];
        }

        for (var x = bpp; x < row.Length; x++)
        {
            row[x] += (byte)Paeth(row[x - bpp], previous[x], previous[x - bpp]);
        }
    }

    private static int Paeth(int a, int b, int c)
    {
        var p = a + b - c;
        var (pa, pb, pc) = (Math.Abs(p - a), Math.Abs(p - b), Math.Abs(p - c));
        return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
    }

    /// <summary>Unfiltered rows to RGBA8: grey to all three, no alpha to opaque.</summary>
    private static byte[] ToRgba(byte[] raw, int width, int height, int bpp)
    {
        var rgba = new byte[width * height * Rgba];
        for (var y = 0; y < height; y++)
        {
            var source = raw.AsSpan(y * (width * bpp + 1) + 1, width * bpp);
            var target = rgba.AsSpan(y * width * Rgba, width * Rgba);
            if (bpp == Rgba)
            {
                source.CopyTo(target);
                continue;
            }

            for (var x = 0; x < width; x++)
            {
                var pixel = source.Slice(x * bpp, bpp);
                var colour = bpp >= 3;
                target[x * Rgba] = pixel[0];
                target[x * Rgba + 1] = colour ? pixel[1] : pixel[0];
                target[x * Rgba + 2] = colour ? pixel[2] : pixel[0];
                target[x * Rgba + 3] = bpp == 2 ? pixel[1] : Opaque;
            }
        }

        return rgba;
    }

    private static void WriteChunk(Stream stream, string type, byte[] data)
    {
        Span<byte> word = stackalloc byte[4];
        BinaryPrimitives.WriteInt32BigEndian(word, data.Length);
        stream.Write(word);
        var name = Encoding.ASCII.GetBytes(type);
        stream.Write(name);
        stream.Write(data);
        BinaryPrimitives.WriteUInt32BigEndian(word, Crc(name, data));
        stream.Write(word);
    }

    /// <summary>CRC-32 (ISO 3309) of a chunk's type and data.</summary>
    private static uint Crc(byte[] type, byte[] data)
    {
        var crc = Crc(uint.MaxValue, type);
        return Crc(crc, data) ^ uint.MaxValue;
    }

    private static uint Crc(uint crc, byte[] bytes)
    {
        foreach (var b in bytes)
        {
            crc = CrcTable[(crc ^ b) & 0xFF] ^ (crc >> 8);
        }

        return crc;
    }

    private static uint[] MakeCrcTable()
    {
        const uint polynomial = 0xEDB88320;
        var table = new uint[256];
        for (uint n = 0; n < table.Length; n++)
        {
            var c = n;
            for (var k = 0; k < 8; k++)
            {
                c = (c & 1) != 0 ? polynomial ^ (c >> 1) : c >> 1;
            }

            table[n] = c;
        }

        return table;
    }
}
