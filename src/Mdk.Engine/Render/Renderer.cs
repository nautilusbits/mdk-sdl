using System.Diagnostics;
using System.Drawing;
using System.Numerics;
using System.Runtime.InteropServices;
using Mdk.Engine.Platform;
using SDL;
using static SDL.SDL3;

namespace Mdk.Engine.Render;

/// <summary>A corner of a triangle: position (MDK coordinates, Z up), texture coordinates (0-1
/// over one frame) and a colour (RGBA8, R in the low byte; white unless given) multiplying the
/// material's, interpolated across the triangle (Gouraud).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vertex(Vector3 position, Vector2 uv)
{
    public const uint White = uint.MaxValue;
    private const int GreenShift = 8;
    private const int BlueShift = 16;
    private const int AlphaShift = 24;

    public Vector3 Position = position;
    public Vector2 Uv = uv;
    public uint Colour = White;

    public Vertex(Vector3 position, Vector2 uv, uint colour) : this(position, uv) => Colour = colour;

    /// <summary>A colour packed for <see cref="Colour"/>.</summary>
    public static uint Rgba(byte r, byte g, byte b, byte a) =>
        r | (uint)g << GreenShift | (uint)b << BlueShift | (uint)a << AlphaShift;
}

/// <summary>How a batch is drawn.</summary>
public enum Pass
{
    /// <summary>Opaque, back faces culled (arenas).</summary>
    Solid,
    /// <summary>Opaque, both faces (models).</summary>
    DoubleSided,
    /// <summary>The panorama where the sky behind would be, both faces (mirrors).</summary>
    Mirror,
    /// <summary>Blended by alpha, both faces, after the opaque ones (glass).</summary>
    Blended,
    /// <summary>Opaque, both faces, over the scene without depth (the fall's explosions).</summary>
    OnTop,
    /// <summary>The 2D canvas over the scene (HUD, menus): blended, no depth.</summary>
    Overlay,
}

/// <summary>How a blended pass meets what's behind: mixed by alpha, or added (scaled by alpha).</summary>
public enum Blend { Alpha, Add }

/// <summary>What a mesh's vertices make: triangles (three each) or lines (two each, one pixel wide).</summary>
public enum Primitive { Triangles, Lines }

/// <summary>A surface: an index texture through a palette, a flat colour, or a mirror showing the
/// panorama <paramref name="RowShift"/> rows lower or higher; <paramref name="Shading"/> picks the
/// original look or the enhanced one; <paramref name="Blend"/> applies to the blended passes;
/// <paramref name="DepthLayer"/> draws it that many steps nearer in depth (a poster over its wall).</summary>
public readonly record struct Material(int Texture, int Palette, Vector4 Colour, int FrameCount, Pass Pass, float RowShift = 0f,
    Shading Shading = Shading.Original, Blend Blend = Blend.Alpha, int DepthLayer = 0)
{
    public const int None = -1;

    public static Material Flat(Vector4 colour, Pass pass) => new(None, None, colour, 1, pass);

    public static Material Mirror(float rowShift) => new(None, None, Vector4.One, 1, Pass.Mirror, rowShift);
}

/// <summary>The level's panorama (index texture and palette ids): <paramref name="WrapWidth"/>
/// columns cover 360 degrees, <paramref name="HorizonRow"/> at eye level; the screen above and below
/// it takes palette colours <paramref name="TopColour"/> and <paramref name="BottomColour"/>.</summary>
public sealed record Panorama(int Sky, int MirrorSky, int Palette, float WrapWidth, float HorizonRow, float Offset,
    float Height, int TopColour, int BottomColour, Sampling Sampling = Sampling.Nearest);

/// <summary>What is behind the scene (the scripts' sky modes, 0x574304): the panorama, the clear
/// colour, or the last frame kept.</summary>
public enum Backdrop { Sky, Clear, Keep }

/// <summary>A view's camera: what a stereo eye is built from (the projection is shifted off-centre
/// for convergence).</summary>
public readonly record struct ViewCamera(Vector3 Forward, Vector3 Up, Matrix4x4 Projection, float Near)
{
    /// <summary>Clip space to world directions for the sky, built from the camera's axes and its
    /// projection: <c>(x, y, 1, 1)</c> goes to <c>forward + right·x′ + up·y′</c>, on one plane. sky.hlsl
    /// blends its screen triangle's corners across the pixels, which is right only for corners in one
    /// plane: inverting rotation × projection in float32 (near 0.5, far 20000) put them off it at clip
    /// depth 1, by an error that changed with the camera's angle, and the sky shook by several pixels
    /// when turning while the world stayed still.</summary>
    public Matrix4x4 SkyDirections()
    {
        var forward = Vector3.Normalize(Forward);
        var right = Vector3.Normalize(Vector3.Cross(forward, Up));
        var up = Vector3.Cross(right, forward);
        var across = right / Projection.M11;
        var upwards = up / Projection.M22;
        var centre = forward + right * (Projection.M31 / Projection.M11) + up * (Projection.M32 / Projection.M22);
        return new Matrix4x4(
            across.X, across.Y, across.Z, 0f,
            upwards.X, upwards.Y, upwards.Z, 0f,
            centre.X, centre.Y, centre.Z, 0f,
            0f, 0f, 0f, 1f);
    }
}

/// <summary>The camera: its view-projection, the inverse of its rotation and projection (clip space
/// to world directions, for the sky), and its position.</summary>
public readonly record struct View(Matrix4x4 ViewProjection, Matrix4x4 ClipToDirection, Vector3 Position, ViewCamera? Camera = null)
{
    /// <summary>The view from an eye <paramref name="side"/> (−1 the left, +1 the right; 0 the view
    /// itself): the eyes are <paramref name="separation"/> apart, parallel, and their images converge
    /// at <paramref name="convergence"/> (0 or less: at infinity). A view without a camera (the 2D
    /// screens) stays as it is.</summary>
    public View Eye(float side, float separation, float convergence)
    {
        if (Camera is not { } camera || side == 0f)
        {
            return this;
        }

        var right = Vector3.Normalize(Vector3.Cross(camera.Forward, camera.Up));
        var position = Position + right * (side * separation * 0.5f);
        var view = Matrix4x4.CreateLookAt(position, position + camera.Forward, camera.Up);
        var projection = camera.Projection;
        if (convergence > 0f)
        {
            // Shifts the frustum sideways so the eyes' rays meet at the convergence distance
            // (the same off-centre projection the scope uses).
            var shift = -side * separation * camera.Near / (2f * convergence);
            projection.M31 += shift * projection.M11 / camera.Near;
        }

        // The sky's rays follow the eye's frustum too: its panorama stays at infinity (the eyes'
        // directions at a pixel differ as a camera axis' would, not as a finite surface's).
        var rotation = Matrix4x4.CreateLookAt(Vector3.Zero, camera.Forward, camera.Up);
        Matrix4x4.Invert(rotation * projection, out var clipToDirection);
        return new View(view * projection, clipToDirection, position, camera);
    }
}

/// <summary>What the last frame drew: its GPU draw calls and triangles (lines count none), and the
/// milliseconds it waited for the GPU (the swapchain: earlier frames, the display's refresh; with
/// <see cref="GpuSync.Wait"/>, this frame's GPU work).</summary>
public readonly record struct RenderStats(int DrawCalls, int Triangles, float GpuWait = 0f);

/// <summary>Whether a frame returns once submitted, or waits until the GPU has drawn it (measures).</summary>
public enum GpuSync { Async, Wait }

/// <summary>Where a hidden window's frames go: off screen only, or also through its swapchain as a
/// shown window's do (tests).</summary>
public enum HiddenFrames { Offscreen, Swapchain }

/// <summary>Draws paletted triangle meshes with SDL_GPU.
/// <code>
///  game ──► Renderer ──► offscreen colour + depth ──blit──► swapchain
///              │                    └─download──► screenshot (BMP)
///              └ meshes, index textures, palettes (GPU resources by id)
/// </code></summary>
public sealed unsafe partial class Renderer : IDisposable
{
    private const SDL_GPUTextureFormat ColourFormat = SDL_GPUTextureFormat.SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    private const int BytesPerPixel = 4;
    private const int PaletteSize = 256;

    [StructLayout(LayoutKind.Sequential)]
    private struct FragmentUniforms
    {
        public Vector4 Colour;
        public int Textured;
        public int FrameCount;
        public int Frame;
        public int Unused;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct PanoramaUniforms
    {
        public float WrapWidth;
        public float HorizonRow;
        public float Offset;
        public float Height;
        public int TopColour;
        public int BottomColour;
        public float RowShift;
        public Sampling Sampling;
        public Vector4 Camera;
    }

    /// <summary>Whether a pipeline reads vertex buffers and the depth buffer (the sky does neither;
    /// the enhanced look's post-processing has no depth buffer).</summary>
    private enum Geometry { Mesh, Screen, Post }

    /// <summary>What a pipeline writes: colour (and depth), or depth alone: the camera's (the enhanced
    /// look's ambient occlusion) or the sun's (its shadows, biased).</summary>
    private enum Output { Colour, Depth, Shadow }

    /// <summary>A vertex's position, texture coordinates and colour.</summary>
    private const uint VertexAttributes = 3;

    private readonly record struct PipelineKey(string Program, Pass Pass, Primitive Primitive, Geometry Geometry, uint Samples, Output Output,
        Blend Blend = Blend.Alpha);

    /// <summary>A shader format and the extension of its embedded programs (shaders/palette.vs.dxil).</summary>
    private readonly record struct ShaderFormat(SDL_GPUShaderFormat Format, string Extension);

    /// <summary>The formats in the order tried: Direct3D 12, Metal, Vulkan. The build embeds those its
    /// tools could compile (Mdk.Engine.csproj).</summary>
    private static readonly ShaderFormat[] ShaderFormats =
    [
        new(SDL_GPUShaderFormat.SDL_GPU_SHADERFORMAT_DXIL, ".dxil"),
        new(SDL_GPUShaderFormat.SDL_GPU_SHADERFORMAT_MSL, ".msl"),
        new(SDL_GPUShaderFormat.SDL_GPU_SHADERFORMAT_SPIRV, ".spv"),
    ];

    /// <summary>The program whose resource tells whether a format is embedded.</summary>
    private const string ShaderProbe = "shaders/palette.vs";

    private readonly record struct DrawCommand(int Mesh, int First, int Count, Material Material, int Frame, Matrix4x4 World,
        Primitive Primitive = Primitive.Triangles, DepthBand Band = DepthBand.Front);

    /// <summary>The 2D canvas: <see cref="CanvasHeight"/> units high like the original's view, as
    /// wide as the window's aspect makes it; quads collected each frame into one dynamic mesh.</summary>
    public const float CanvasHeight = 360f;
    private const int CanvasQuads = 4096;
    private const int QuadVertices = 6;
    private readonly int _canvasMesh;
    /// <summary>The frame's streamed vertices (<see cref="Stream"/>), uploaded once before it's drawn.</summary>
    private const int StreamVertices = 1 << 18;
    private readonly int _streamMesh;
    private readonly List<Vertex> _stream = [];
    private readonly List<Vertex> _canvasVertices = [];
    private readonly List<(int First, int Count, Material Material)> _canvasCommands = [];

    private readonly Window _window;
    private readonly SDL_GPUDevice* _device;
    private readonly ShaderFormat _shaderFormat;
    private readonly SDL_GPUTextureFormat _depthFormat;
    private readonly Dictionary<PipelineKey, IntPtr> _pipelines = [];
    private readonly SDL_GPUSampler* _repeatSampler;
    private readonly SDL_GPUSampler* _clampSampler;
    private readonly List<IntPtr> _textures = [];
    private readonly List<IntPtr> _buffers = [];
    private readonly List<DrawCommand> _commands = [];
    /// <summary>A dynamic mesh's upload buffer and the bytes to copy before the next frame.</summary>
    private sealed class DynamicMesh(IntPtr transfer)
    {
        public readonly IntPtr Transfer = transfer;
        public uint Pending;
    }

    private readonly Dictionary<int, DynamicMesh> _dynamic = [];
    /// <summary>The passes in their drawing order.</summary>
    private static readonly Pass[] Passes = Enum.GetValues<Pass>();

    private SDL_GPUTexture* _target;
    private SDL_GPUTexture* _depth;
    private uint _targetWidth;
    private uint _targetHeight;

    public Renderer(Window window, GpuBackend backend = GpuBackend.Auto)
    {
        _window = window;
        Requested = backend;
        _device = CreateDevice(backend, out _shaderFormat);
        Check(_device != null, "SDL_CreateGPUDevice");
        Check(SDL_ClaimWindowForGPUDevice(_device, window.Handle), "SDL_ClaimWindowForGPUDevice");
        Gpu = ReadGpuInfo();
        SwapchainFormat = FormatName(SDL_GetGPUSwapchainTextureFormat(_device, window.Handle));

        var depthUsage = SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        _depthFormat = SDL_GPUTextureSupportsFormat(_device, SDL_GPUTextureFormat.SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPUTextureType.SDL_GPU_TEXTURETYPE_2D, depthUsage)
            ? SDL_GPUTextureFormat.SDL_GPU_TEXTUREFORMAT_D32_FLOAT
            : SDL_GPUTextureFormat.SDL_GPU_TEXTUREFORMAT_D24_UNORM;

        _repeatSampler = CreateSampler(SDL_GPUSamplerAddressMode.SDL_GPU_SAMPLERADDRESSMODE_REPEAT);
        _clampSampler = CreateSampler(SDL_GPUSamplerAddressMode.SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE);

        // The enhanced look samples its depth buffers.
        var sampledDepth = depthUsage | SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_SAMPLER;
        _sampledDepthFormat = SDL_GPUTextureSupportsFormat(_device, SDL_GPUTextureFormat.SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPUTextureType.SDL_GPU_TEXTURETYPE_2D, sampledDepth)
            ? SDL_GPUTextureFormat.SDL_GPU_TEXTUREFORMAT_D32_FLOAT
            : SDL_GPUTextureFormat.SDL_GPU_TEXTUREFORMAT_D16_UNORM;
        _mipSampler = CreateSampler(SDL_GPUSamplerAddressMode.SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE, Sampling.Linear);
        DepthFormat = FormatName(_depthFormat);

        _canvasMesh = CreateDynamicMesh(CanvasQuads * QuadVertices);
        _streamMesh = CreateDynamicMesh(StreamVertices);
        CreateColourSampler();

        // Texture 0 is bound for flat colours, so it must outlive every scope (Release).
        CreateRgbaTexture(1, 1, new byte[BytesPerPixel]);
    }

    /// <summary>A point of the window (in its coordinates) on the canvas.</summary>
    public Vector2 CanvasPoint(float x, float y)
    {
        int width, height;
        SDL_GetWindowSize(_window.Handle, &width, &height);
        return height <= 0 ? Vector2.Zero : new Vector2(x, y) * (CanvasHeight / height);
    }

    /// <summary>The canvas clear of the window's cut-outs and rounded corners (its safe area's
    /// insets taken off the whole canvas); all of it on a desktop.</summary>
    public RectangleF CanvasSafeArea
    {
        get
        {
            var canvas = new RectangleF(0f, 0f, CanvasWidth, CanvasHeight);
            var (width, height) = _window.Size;
            var safe = _window.SafeArea;
            if (height <= 0 || safe == new RectangleF(0f, 0f, width, height))
            {
                return canvas;
            }

            var scale = CanvasHeight / height;
            return RectangleF.FromLTRB(safe.Left * scale, safe.Top * scale,
                canvas.Right - (width - safe.Right) * scale, canvas.Bottom - (height - safe.Bottom) * scale);
        }
    }

    /// <summary>The textures and meshes created so far; <see cref="Release"/> frees those created after.</summary>
    public readonly record struct Scope(int Textures, int Buffers);

    public Scope Mark() => new(_textures.Count, _buffers.Count);

    /// <summary>Frees the textures and meshes created after <paramref name="scope"/> (a screen of
    /// the game ends): their ids are reused.</summary>
    public void Release(Scope scope)
    {
        FlushUploads();
        SDL_WaitForGPUIdle(_device);
        ReleaseColoursFrom(scope.Textures);
        for (var i = _textures.Count - 1; i >= scope.Textures; i--)
        {
            SDL_ReleaseGPUTexture(_device, (SDL_GPUTexture*)_textures[i]);
            _textures.RemoveAt(i);
        }

        for (var i = _buffers.Count - 1; i >= scope.Buffers; i--)
        {
            SDL_ReleaseGPUBuffer(_device, (SDL_GPUBuffer*)_buffers[i]);
            if (_dynamic.Remove(i, out var dynamic))
            {
                SDL_ReleaseGPUTransferBuffer(_device, (SDL_GPUTransferBuffer*)dynamic.Transfer);
            }

            _buffers.RemoveAt(i);
        }

        Panorama = null;
        Backdrop = Backdrop.Sky;
        Lighting = null;
        Lights.Clear();
    }

    /// <summary>The canvas's width for the window's aspect.</summary>
    public float CanvasWidth => CanvasHeight * AspectRatio;

    /// <summary>Draws part of an index texture (<paramref name="source"/> in its pixels) on the canvas,
    /// tinted (alpha fades it); palette index 0 stays transparent.</summary>
    public void DrawImage(int texture, int palette, Vector2 textureSize, RectangleF source, RectangleF target, Vector4 tint)
    {
        var uv0 = new Vector2(source.Left, source.Top) / textureSize;
        var uv1 = new Vector2(source.Right, source.Bottom) / textureSize;
        AddQuad(target, uv0, uv1, new Material(texture, palette, tint, 1, Pass.Overlay));
    }

    /// <summary>Fills a rectangle of the canvas with a colour (alpha blends it).</summary>
    public void FillRect(RectangleF target, Vector4 colour) =>
        AddQuad(target, Vector2.Zero, Vector2.Zero, Material.Flat(colour, Pass.Overlay));

    /// <summary>Adds a colour, scaled by its alpha, to a rectangle of the canvas (clamped at white).</summary>
    public void AddRect(RectangleF target, Vector4 colour) =>
        AddQuad(target, Vector2.Zero, Vector2.Zero, Material.Flat(colour, Pass.Overlay) with { Blend = Blend.Add });

    /// <summary>A rectangle's outline, <paramref name="width"/> units thick, inside it.</summary>
    public void FrameRect(RectangleF target, float width, Vector4 colour)
    {
        FillRect(new RectangleF(target.Left, target.Top, target.Width, width), colour);
        FillRect(new RectangleF(target.Left, target.Bottom - width, target.Width, width), colour);
        FillRect(new RectangleF(target.Left, target.Top + width, width, target.Height - 2 * width), colour);
        FillRect(new RectangleF(target.Right - width, target.Top + width, width, target.Height - 2 * width), colour);
    }

    private void AddQuad(RectangleF target, Vector2 uv0, Vector2 uv1, Material material)
    {
        if (_canvasVertices.Count >= CanvasQuads * QuadVertices)
        {
            return;
        }

        var first = _canvasVertices.Count;
        Vertex Corner(float x, float y, float u, float v) => new(new Vector3(x, y, 0f), new Vector2(u, v));
        _canvasVertices.Add(Corner(target.Left, target.Top, uv0.X, uv0.Y));
        _canvasVertices.Add(Corner(target.Right, target.Top, uv1.X, uv0.Y));
        _canvasVertices.Add(Corner(target.Right, target.Bottom, uv1.X, uv1.Y));
        _canvasVertices.Add(Corner(target.Left, target.Top, uv0.X, uv0.Y));
        _canvasVertices.Add(Corner(target.Right, target.Bottom, uv1.X, uv1.Y));
        _canvasVertices.Add(Corner(target.Left, target.Bottom, uv0.X, uv1.Y));

        // Neighbouring quads of one material draw together.
        if (_canvasCommands.Count > 0 && _canvasCommands[^1].Material == material)
        {
            var last = _canvasCommands[^1];
            _canvasCommands[^1] = (last.First, last.Count + QuadVertices, material);
            return;
        }

        _canvasCommands.Add((first, QuadVertices, material));
    }

    /// <summary>The sky and mirrors' panorama; without one the screen is cleared to a colour.</summary>
    public Panorama? Panorama { get; set; }

    /// <summary>What the scene is drawn over; the sky needs a <see cref="Panorama"/>.</summary>
    public Backdrop Backdrop { get; set; }

    private static void Check(bool ok, string what)
    {
        if (!ok)
        {
            throw new InvalidOperationException($"{what}: {SDL_GetError()}");
        }
    }

    /// <summary>The backend asked for at start (Options, Display: a change applies after a restart).</summary>
    public GpuBackend Requested { get; }

    /// <summary>The device's backend, name and driver.</summary>
    public GpuInfo Gpu { get; }

    /// <summary>The device for the backend asked for, else (failing, or not on this OS: a warning)
    /// SDL's choice.</summary>
    private static SDL_GPUDevice* CreateDevice(GpuBackend backend, out ShaderFormat format)
    {
        if (backend != GpuBackend.Auto && GpuBackends.Resolve(backend, OsInfo.Current) != backend)
        {
            Console.Error.WriteLine($"GPU backend {GpuBackends.Name(backend)} not on this OS: using SDL's choice");
        }

        foreach (var attempt in GpuBackends.Attempts(backend, OsInfo.Current))
        {
            var device = attempt == GpuBackend.Auto ? CreateAuto(out format) : CreateFor(attempt, out format);
            if (device != null)
            {
                return device;
            }
        }

        format = default;
        return null;
    }

    /// <summary>The shader format each backend takes.</summary>
    private static ShaderFormat FormatOf(GpuBackend backend) => backend switch
    {
        GpuBackend.Direct3D12 => ShaderFormats[0],
        GpuBackend.Metal => ShaderFormats[1],
        _ => ShaderFormats[2],
    };

    private static bool Embedded(ShaderFormat format) =>
        typeof(Renderer).Assembly.GetManifestResourceNames().Contains(ShaderProbe + format.Extension);

    /// <summary>A device of one backend (SDL_CreateGPUDeviceWithProperties with its driver name), or null.</summary>
    private static SDL_GPUDevice* CreateFor(GpuBackend backend, out ShaderFormat format)
    {
        format = FormatOf(backend);
        var name = GpuBackends.Name(backend);
        if (!Embedded(format))
        {
            Console.Error.WriteLine($"GPU backend {name}: no {format.Extension} shaders in this build, using SDL's choice");
            return null;
        }

        var device = CreateWith(GpuBackends.Driver(backend), format.Format);
        if (device == null)
        {
            Console.Error.WriteLine($"GPU backend {name} failed ({SDL_GetError()}), using SDL's choice");
        }

        return device;
    }

    /// <summary>SDL's GPU validation (the D3D12 debug layer, Vulkan's validation layers, where
    /// installed): on by default in SDL, kept to Debug builds (its cost, its debug breaks).</summary>
#if DEBUG
    private const bool GpuValidation = true;
#else
    private const bool GpuValidation = false;
#endif

    /// <summary>A device of an SDL driver (null: SDL's choice) taking a shader format, or null. It asks
    /// for neither depth clamping nor clip distances: no pipeline uses them, and some GPUs (Mali-G52)
    /// lack them, which would leave them no device at all.</summary>
    private static SDL_GPUDevice* CreateWith(string? driver, SDL_GPUShaderFormat format)
    {
        var properties = SDL_CreateProperties();
        if (driver != null)
        {
            SDL_SetStringProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, driver);
        }

        if (format == SDL_GPUShaderFormat.SDL_GPU_SHADERFORMAT_DXIL)
        {
            SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
        }
        else if (format == SDL_GPUShaderFormat.SDL_GPU_SHADERFORMAT_MSL)
        {
            SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
        }
        else
        {
            SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        }

        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, GpuValidation);
        var device = SDL_CreateGPUDeviceWithProperties(properties);
        SDL_DestroyProperties(properties);
        return device;
    }

    /// <summary>The backends of this OS whose shaders are embedded and whose driver SDL has.</summary>
    public static IReadOnlyList<GpuBackend> Backends() =>
        GpuBackends.On(OsInfo.Current)
            .Where(b => b == GpuBackend.Auto || (Embedded(FormatOf(b)) && SDL_GPUSupportsShaderFormats(FormatOf(b).Format, GpuBackends.Driver(b))))
            .ToList();

    private GpuInfo ReadGpuInfo()
    {
        var properties = SDL_GetGPUDeviceProperties(_device);
        return new GpuInfo(GpuBackends.Of(SDL_GetGPUDeviceDriver(_device) ?? ""),
            SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "") ?? "",
            SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "") ?? "");
    }

    /// <summary>A device for the first embedded shader format a GPU driver takes (SDL_GPU_DRIVER can
    /// choose the driver, e.g. vulkan on Windows), or null.</summary>
    private static SDL_GPUDevice* CreateAuto(out ShaderFormat format)
    {
        var embedded = typeof(Renderer).Assembly.GetManifestResourceNames();
        foreach (var candidate in ShaderFormats)
        {
            if (!embedded.Contains(ShaderProbe + candidate.Extension))
            {
                continue;
            }

            var device = CreateWith(null, candidate.Format);
            if (device == null)
            {
                continue;
            }

            format = candidate;
            return device;
        }

        format = default;
        return null;
    }

    private SDL_GPUSampler* CreateSampler(SDL_GPUSamplerAddressMode mode, Sampling sampling = Sampling.Nearest)
    {
        var linear = sampling == Sampling.Linear;
        var info = new SDL_GPUSamplerCreateInfo
        {
            min_filter = linear ? SDL_GPUFilter.SDL_GPU_FILTER_LINEAR : SDL_GPUFilter.SDL_GPU_FILTER_NEAREST,
            mag_filter = linear ? SDL_GPUFilter.SDL_GPU_FILTER_LINEAR : SDL_GPUFilter.SDL_GPU_FILTER_NEAREST,
            mipmap_mode = linear ? SDL_GPUSamplerMipmapMode.SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPUSamplerMipmapMode.SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
            address_mode_u = mode,
            address_mode_v = mode,
            address_mode_w = mode,
            max_lod = linear ? AllMips : 0f,
        };
        return SDL_CreateGPUSampler(_device, &info);
    }

    private SDL_GPUShader* LoadShader(string name, SDL_GPUShaderStage stage, uint samplers)
    {
        using var stream = typeof(Renderer).Assembly.GetManifestResourceStream("shaders/" + name + _shaderFormat.Extension)
            ?? throw new InvalidOperationException($"Shader {name} not embedded: {string.Join(", ", typeof(Renderer).Assembly.GetManifestResourceNames())}");
        var code = new byte[stream.Length];
        stream.ReadExactly(code);
        var entry = stage == SDL_GPUShaderStage.SDL_GPU_SHADERSTAGE_VERTEX ? "vs_main"u8 : "ps_main"u8;
        fixed (byte* codePtr = code)
        fixed (byte* entryPtr = entry)
        {
            var info = new SDL_GPUShaderCreateInfo
            {
                code = codePtr,
                code_size = (nuint)code.Length,
                entrypoint = entryPtr,
                format = _shaderFormat.Format,
                stage = stage,
                num_samplers = samplers,
                num_uniform_buffers = UniformBuffers(name),
            };
            var shader = SDL_CreateGPUShader(_device, &info);
            Check(shader != null, $"SDL_CreateGPUShader {name}");
            return shader;
        }
    }

    /// <summary>A pipeline, created the first time it's asked for.</summary>
    private SDL_GPUGraphicsPipeline* Pipeline(PipelineKey key)
    {
        if (!_pipelines.TryGetValue(key, out var pipeline))
        {
            pipeline = _pipelines[key] = (IntPtr)CreatePipeline(key);
        }

        return (SDL_GPUGraphicsPipeline*)pipeline;
    }

    private SDL_GPUGraphicsPipeline* CreatePipeline(PipelineKey key)
    {
        var (program, pass, primitive, geometry, samples, output, blend) = key;
        var vertexShader = LoadShader(program + ".vs", SDL_GPUShaderStage.SDL_GPU_SHADERSTAGE_VERTEX, 0);
        var fragmentShader = LoadShader(program + ".ps", SDL_GPUShaderStage.SDL_GPU_SHADERSTAGE_FRAGMENT, SamplerCount(program));
        var bufferDescription = new SDL_GPUVertexBufferDescription
        {
            slot = 0,
            pitch = (uint)sizeof(Vertex),
            input_rate = SDL_GPUVertexInputRate.SDL_GPU_VERTEXINPUTRATE_VERTEX,
        };
        var attributes = stackalloc SDL_GPUVertexAttribute[(int)VertexAttributes];
        attributes[0] = new SDL_GPUVertexAttribute { location = 0, format = SDL_GPUVertexElementFormat.SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offset = 0 };
        attributes[1] = new SDL_GPUVertexAttribute { location = 1, format = SDL_GPUVertexElementFormat.SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offset = (uint)sizeof(Vector3) };
        attributes[2] = new SDL_GPUVertexAttribute { location = 2, format = SDL_GPUVertexElementFormat.SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offset = (uint)(sizeof(Vector3) + sizeof(Vector2)) };

        var blended = pass is Pass.Blended or Pass.Overlay;
        var colourTarget = new SDL_GPUColorTargetDescription
        {
            format = ColourFormat,
            blend_state = new SDL_GPUColorTargetBlendState
            {
                enable_blend = blended,
                src_color_blendfactor = SDL_GPUBlendFactor.SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                dst_color_blendfactor = blend == Blend.Add ? SDL_GPUBlendFactor.SDL_GPU_BLENDFACTOR_ONE : SDL_GPUBlendFactor.SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                color_blend_op = SDL_GPUBlendOp.SDL_GPU_BLENDOP_ADD,
                src_alpha_blendfactor = SDL_GPUBlendFactor.SDL_GPU_BLENDFACTOR_ONE,
                dst_alpha_blendfactor = SDL_GPUBlendFactor.SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                alpha_blend_op = SDL_GPUBlendOp.SDL_GPU_BLENDOP_ADD,
            },
        };

        var info = new SDL_GPUGraphicsPipelineCreateInfo
        {
            vertex_shader = vertexShader,
            fragment_shader = fragmentShader,
            vertex_input_state = new SDL_GPUVertexInputState
            {
                vertex_buffer_descriptions = &bufferDescription,
                num_vertex_buffers = geometry == Geometry.Mesh ? 1u : 0u,
                vertex_attributes = attributes,
                num_vertex_attributes = geometry == Geometry.Mesh ? VertexAttributes : 0u,
            },
            primitive_type = primitive == Primitive.Lines ? SDL_GPUPrimitiveType.SDL_GPU_PRIMITIVETYPE_LINELIST : SDL_GPUPrimitiveType.SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            rasterizer_state = new SDL_GPURasterizerState
            {
                fill_mode = SDL_GPUFillMode.SDL_GPU_FILLMODE_FILL,
                cull_mode = pass == Pass.Solid && geometry == Geometry.Mesh && output != Output.Shadow ? SDL_GPUCullMode.SDL_GPU_CULLMODE_BACK : SDL_GPUCullMode.SDL_GPU_CULLMODE_NONE,
                // MDK's front faces wind clockwise on screen.
                front_face = SDL_GPUFrontFace.SDL_GPU_FRONTFACE_CLOCKWISE,
                // Clips at the near and far planes: the device has no depth clamping (CreateWith).
                enable_depth_clip = true,
                enable_depth_bias = output == Output.Shadow,
                depth_bias_slope_factor = output == Output.Shadow ? ShadowSlopeBias : 0f,
            },
            multisample_state = new SDL_GPUMultisampleState { sample_count = SampleCount(samples) },
            depth_stencil_state = new SDL_GPUDepthStencilState
            {
                compare_op = SDL_GPUCompareOp.SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
                enable_depth_test = geometry == Geometry.Mesh && pass is not (Pass.Overlay or Pass.OnTop),
                enable_depth_write = geometry == Geometry.Mesh && !blended && pass != Pass.OnTop,
            },
            target_info = new SDL_GPUGraphicsPipelineTargetInfo
            {
                color_target_descriptions = &colourTarget,
                num_color_targets = output == Output.Colour ? 1u : 0u,
                depth_stencil_format = output == Output.Colour ? _depthFormat : _sampledDepthFormat,
                has_depth_stencil_target = geometry != Geometry.Post,
            },
        };
        var pipeline = SDL_CreateGPUGraphicsPipeline(_device, &info);
        Check(pipeline != null, $"SDL_CreateGPUGraphicsPipeline {program} {pass}");
        SDL_ReleaseGPUShader(_device, vertexShader);
        SDL_ReleaseGPUShader(_device, fragmentShader);
        return pipeline;
    }

    /// <summary>An 8-bit index texture; frames of animated ones stacked vertically.</summary>
    public int CreateIndexTexture(int width, int height, byte[] indices)
    {
        var texture = CreateTexture(SDL_GPUTextureFormat.SDL_GPU_TEXTUREFORMAT_R8_UNORM, (uint)width, (uint)height, SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_SAMPLER);
        Upload(texture, (uint)width, (uint)height, indices);
        _textures.Add((IntPtr)texture);
        KeepIndices(_textures.Count - 1, width, height, indices);
        return _textures.Count - 1;
    }

    /// <summary>New pixels for a texture of the same size (a video's frame, a fading palette).</summary>
    public void UpdateTexture(int texture, int width, int height, byte[] pixels)
    {
        Upload((SDL_GPUTexture*)_textures[texture], (uint)width, (uint)height, pixels);
        Changed(texture, width, height, pixels);
    }

    /// <summary>A 256-colour palette, RGBA8.</summary>
    public int CreatePalette(byte[] rgba)
    {
        var palette = CreateRgbaTexture(PaletteSize, 1, rgba);
        KeepPalette(palette, rgba);
        return palette;
    }

    public int CreateRgbaTexture(int width, int height, byte[] rgba)
    {
        var texture = CreateTexture(ColourFormat, (uint)width, (uint)height, SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_SAMPLER);
        Upload(texture, (uint)width, (uint)height, rgba);
        _textures.Add((IntPtr)texture);
        return _textures.Count - 1;
    }

    /// <summary>A vertex buffer of triangles (three vertices each).</summary>
    public int CreateMesh(Vertex[] vertices)
    {
        var size = (uint)(vertices.Length * sizeof(Vertex));
        var bufferInfo = new SDL_GPUBufferCreateInfo { usage = SDL_GPUBufferUsageFlags.SDL_GPU_BUFFERUSAGE_VERTEX, size = size };
        var buffer = SDL_CreateGPUBuffer(_device, &bufferInfo);
        Check(buffer != null, "SDL_CreateGPUBuffer");

        var copy = UploadPass(MemoryMarshal.AsBytes(vertices.AsSpan()), out var transfer, out var offset);
        var source = new SDL_GPUTransferBufferLocation { transfer_buffer = transfer, offset = offset };
        var destination = new SDL_GPUBufferRegion { buffer = buffer, size = size };
        SDL_UploadToGPUBuffer(copy, &source, &destination, false);

        _buffers.Add((IntPtr)buffer);
        return _buffers.Count - 1;
    }

    /// <summary>A vertex buffer of up to <paramref name="capacity"/> vertices, rewritten with
    /// <see cref="UpdateMesh"/> (sprites).</summary>
    public int CreateDynamicMesh(int capacity)
    {
        var size = (uint)(capacity * sizeof(Vertex));
        var bufferInfo = new SDL_GPUBufferCreateInfo { usage = SDL_GPUBufferUsageFlags.SDL_GPU_BUFFERUSAGE_VERTEX, size = size };
        var buffer = SDL_CreateGPUBuffer(_device, &bufferInfo);
        Check(buffer != null, "SDL_CreateGPUBuffer");
        _buffers.Add((IntPtr)buffer);
        var id = _buffers.Count - 1;
        _dynamic[id] = new DynamicMesh((IntPtr)CreateTransfer(size, SDL_GPUTransferBufferUsage.SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD));
        return id;
    }

    /// <summary>New vertices for a dynamic mesh, uploaded before the next frame is drawn.</summary>
    public void UpdateMesh(int mesh, ReadOnlySpan<Vertex> vertices)
    {
        var dynamic = _dynamic[mesh];
        var bytes = MemoryMarshal.AsBytes(vertices);
        var mapped = SDL_MapGPUTransferBuffer(_device, (SDL_GPUTransferBuffer*)dynamic.Transfer, true);
        bytes.CopyTo(new Span<byte>((void*)mapped, bytes.Length));
        SDL_UnmapGPUTransferBuffer(_device, (SDL_GPUTransferBuffer*)dynamic.Transfer);
        dynamic.Pending = (uint)bytes.Length;
    }

    /// <summary>Room for <paramref name="count"/> vertices of this frame only (posed models), drawn
    /// from <paramref name="mesh"/> at <paramref name="first"/>; empty when the frame's stream is full.
    /// No buffer is made or filled per draw: the stream is uploaded once, before the frame.</summary>
    public Span<Vertex> Stream(int count, out int mesh, out int first)
    {
        mesh = _streamMesh;
        first = _stream.Count;
        if (first + count > StreamVertices)
        {
            return [];
        }

        CollectionsMarshal.SetCount(_stream, first + count);
        return CollectionsMarshal.AsSpan(_stream).Slice(first, count);
    }

    /// <summary>The frame's streamed vertices go to their buffer.</summary>
    private void QueueStream()
    {
        if (_stream.Count > 0)
        {
            UpdateMesh(_streamMesh, CollectionsMarshal.AsSpan(_stream));
        }

        _stream.Clear();
    }

    /// <summary>The canvas's quads become draws in screen space, after the scene.</summary>
    private void QueueCanvas()
    {
        if (_canvasVertices.Count > 0)
        {
            UpdateMesh(_canvasMesh, CollectionsMarshal.AsSpan(_canvasVertices));
        }

        // The canvas fills the view, or keeps to its middle (the VR viewer: a margin around it).
        var margin = new Vector2(CanvasWidth, CanvasHeight) * ((1f / StereoModes.CanvasZoom(StereoMode) - 1f) / 2f);
        var screen = Matrix4x4.CreateOrthographicOffCenter(-margin.X, CanvasWidth + margin.X, CanvasHeight + margin.Y, -margin.Y, 0f, 1f);
        foreach (var (first, count, material) in _canvasCommands)
        {
            _commands.Add(new DrawCommand(_canvasMesh, first, count, material, 0, screen));
        }

        _canvasVertices.Clear();
        _canvasCommands.Clear();
    }

    /// <summary>Copies the pending dynamic meshes into their buffers.</summary>
    private void UploadDynamic(SDL_GPUCommandBuffer* commands)
    {
        SDL_GPUCopyPass* copy = null;
        foreach (var (mesh, dynamic) in _dynamic)
        {
            if (dynamic.Pending == 0)
            {
                continue;
            }

            if (copy == null)
            {
                copy = SDL_BeginGPUCopyPass(commands);
            }

            var source = new SDL_GPUTransferBufferLocation { transfer_buffer = (SDL_GPUTransferBuffer*)dynamic.Transfer };
            var destination = new SDL_GPUBufferRegion { buffer = (SDL_GPUBuffer*)_buffers[mesh], size = dynamic.Pending };
            SDL_UploadToGPUBuffer(copy, &source, &destination, true);
            dynamic.Pending = 0;
        }

        if (copy != null)
        {
            SDL_EndGPUCopyPass(copy);
        }
    }

    /// <summary>The depth band of the draws queued from now on (front again after each frame).</summary>
    public DepthBand Band { get; set; }

    /// <summary>Queues triangles of a mesh for the next frame; <paramref name="frame"/> picks an animated texture's frame.</summary>
    public void Draw(int mesh, int firstVertex, int vertexCount, Material material, int frame = 0) =>
        Queue.Add(new DrawCommand(mesh, firstVertex, vertexCount, material, frame, Matrix4x4.Identity, Band: Band));

    /// <summary>Queues triangles of a mesh placed in the world by <paramref name="world"/> (models).</summary>
    public void Draw(int mesh, int firstVertex, int vertexCount, Material material, int frame, Matrix4x4 world) =>
        Queue.Add(new DrawCommand(mesh, firstVertex, vertexCount, material, frame, world, Band: Band));

    /// <summary>Queues lines of a mesh (two vertices each) in a material's colour (not a mirror).</summary>
    public void DrawLines(int mesh, int firstVertex, int vertexCount, Material material) =>
        Queue.Add(new DrawCommand(mesh, firstVertex, vertexCount, material, 0, Matrix4x4.Identity, Primitive.Lines, Band));

    /// <summary>Draws the queued batches (opaque first, then blended) and shows them. With
    /// <paramref name="screenshot"/>, also saves the frame as a BMP.</summary>
    public void Present(View view, Vector4 clearColour, string? screenshot = null)
    {
        Overlay?.Invoke();
        FlushUploads();
        var commands = SDL_AcquireGPUCommandBuffer(_device);
        SDL_GPUTexture* swapchain = null;
        uint width = (uint)HiddenSize.Width, height = (uint)HiddenSize.Height;

        // A hidden window has no swapchain to show (unless tests ask): the frame stays in the offscreen target.
        var shown = _window.Visibility == Visibility.Shown || HiddenFrames == HiddenFrames.Swapchain;
        var waitStart = Stopwatch.GetTimestamp();
        if (shown && (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, _window.Handle, &swapchain, &width, &height) || swapchain == null))
        {
            SDL_SubmitGPUCommandBuffer(commands);
            _commands.Clear();
            _stream.Clear();
            ClearInsets();
            return;
        }

        var wait = Stopwatch.GetElapsedTime(waitStart);
        // The swapchain's command buffer presents when submitted: on Direct3D 12, where the draws may
        // submit theirs mid-frame (Reserve), they get their own, and the swapchain's only blits at the end.
        var presenting = commands;
        if (swapchain != null && HeapBound)
        {
            commands = SDL_AcquireGPUCommandBuffer(_device);
        }

        _presenting = swapchain != null ? presenting : null;
        var percent = PercentFor(width, height);
        EnsureTargets(shown ? Scaled(width, percent) : width, shown ? Scaled(height, percent) : height);
        QueueCanvas();
        QueueStream();
        UploadDynamic(commands);
        PrepareColours(commands);
        (_drawCalls, _triangles, _samplersBound) = (0, 0, 0);
        PushLights(commands, view);
        _eyeSide = 0f;
        if (StereoMode != Stereo.Off && _eyes[0] != null)
        {
            // Each eye's whole frame, then the two into the frame (side by side or interlaced).
            RenderEye(ref commands, view, clearColour, 0, -1f);
            RenderEye(ref commands, view, clearColour, 1, +1f);
            CompositeStereo(commands);
            _frame = _target;
        }
        else
        {
            _frame = _target;
            RenderScene(ref commands, view, clearColour);
        }

        // The insets belong to the frame, not to one eye: both eyes draw them (their views offset
        // per eye), so they're cleared only once the frame is drawn.
        ClearInsets();
        ApplyGamma(commands);
        if (swapchain != null)
        {
            // The draws first, then the swapchain's blit and present.
            if (commands != presenting)
            {
                SDL_SubmitGPUCommandBuffer(commands);
                commands = presenting;
            }

            Blit(commands, swapchain, width, height);
        }

        if (screenshot != null)
        {
            Save(commands, screenshot);
        }
        else
        {
            wait += Submit(commands, swapchain != null ? Pacing.Swapchain : Pacing.Fence);
        }

        Stats = new RenderStats(_drawCalls, _triangles, (float)wait.TotalMilliseconds);
        _commands.Clear();
        Band = DepthBand.Front;
    }

    /// <summary>How <see cref="Present"/> meets the GPU.</summary>
    public GpuSync Sync { get; set; }

    /// <summary>What keeps the CPU at most a frame ahead of the GPU: the swapchain (a shown window),
    /// or the last frame's fence (a hidden one: frames would pile up, and their memory).</summary>
    private enum Pacing { Swapchain, Fence }

    private SDL_GPUFence* _lastFrame;

    /// <summary>Submits the frame; with <see cref="GpuSync.Wait"/>, returns once the GPU has drawn it,
    /// hidden, once it has drawn the one before (the time waited).</summary>
    private TimeSpan Submit(SDL_GPUCommandBuffer* commands, Pacing pacing)
    {
        if (Sync == GpuSync.Async && pacing == Pacing.Swapchain)
        {
            SDL_SubmitGPUCommandBuffer(commands);
            return TimeSpan.Zero;
        }

        var fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        var start = Stopwatch.GetTimestamp();
        if (Sync == GpuSync.Wait)
        {
            WaitFor(fence);
            return Stopwatch.GetElapsedTime(start);
        }

        WaitFor(_lastFrame);
        _lastFrame = fence;
        return Stopwatch.GetElapsedTime(start);
    }

    /// <summary>Waits until the GPU passes a fence (none: at once) and releases it.</summary>
    private void WaitFor(SDL_GPUFence* fence)
    {
        if (fence == null)
        {
            return;
        }

        SDL_WaitForGPUFences(_device, true, &fence, 1);
        SDL_ReleaseGPUFence(_device, fence);
    }

    /// <summary>Draws on the canvas over every frame, whoever presents it, just before (the
    /// developer tools).</summary>
    public Action? Overlay { get; set; }

    /// <summary>The last presented frame's draws (the debug overlay).</summary>
    public RenderStats Stats { get; private set; }

    /// <summary>The sky and the post pass draw one screen-filling triangle.</summary>
    private const int ScreenTriangle = 3;
    private const int TriangleVertices = 3;
    private int _drawCalls;
    private int _triangles;

    /// <summary>Draws vertices of the bound buffer, counted in <see cref="Stats"/>.</summary>
    private void DrawPrimitives(SDL_GPURenderPass* pass, int count, int first, Primitive primitive)
    {
        _drawCalls++;
        _triangles += primitive == Primitive.Triangles ? count / TriangleVertices : 0;
        SDL_DrawGPUPrimitives(pass, (uint)count, 1, (uint)first, 0);
    }

    /// <summary>The render scales (%): the frame is drawn at this share of the window's size, then
    /// stretched to it (below 100: faster, blurrier; above: supersampled); Auto picks one by the
    /// window's pixels (<see cref="AutoPercent"/>).</summary>
    public static readonly int[] Scales = [AutoScale, 50, 100, 150, 200];
    public const int FullScale = 100;
    public const int AutoScale = 0;
    /// <summary>Auto's frame: about a 1920 x 1080's pixels, at least half the window's sides.</summary>
    private const float AutoPixels = 1920f * 1080f;
    private const int MinAutoScale = 50;
    /// <summary>The largest texture side every backend takes.</summary>
    private const uint MaxTargetSide = 16384;

    /// <summary>The render scale (%) of a shown window; a hidden one draws <see cref="HiddenSize"/>.</summary>
    public int RenderScale { get; set; } = FullScale;

    private uint Scaled(uint side, int percent) => Math.Clamp(side * (uint)percent / FullScale, 1u, MaxTargetSide);

    /// <summary>The render scale (%) Auto picks for a window: its frame about 1080p's pixels, never
    /// above the window's own nor below half its sides.
    /// <code>
    ///   2880 x 1800 tablet: √(2.07 MP / 5.18 MP) = 63 %   2400 x 1080 phone: 89 %   1920 x 1080: 100 %
    /// </code></summary>
    public static int AutoPercent(uint width, uint height)
    {
        var pixels = (float)width * height;
        if (pixels <= 0f)
        {
            return FullScale;
        }

        return Math.Clamp((int)(FullScale * MathF.Sqrt(AutoPixels / pixels)), MinAutoScale, FullScale);
    }

    /// <summary>The render scale (%) for a window of this size.</summary>
    private int PercentFor(uint width, uint height) => RenderScale == AutoScale ? AutoPercent(width, height) : RenderScale;

    /// <summary>The present mode in use (SDL starts with VSync).</summary>
    private VSync _vsync = VSync.On;

    /// <summary>VSync as asked for; the mode used falls back to one the window supports.</summary>
    public VSync VSync
    {
        get => _vsync;
        set
        {
            var mode = PresentModes.Choose(value, m => SDL_WindowSupportsGPUPresentMode(_device, _window.Handle, PresentMode(m)));
            if (mode == _vsync)
            {
                return;
            }

            if (!SDL_SetGPUSwapchainParameters(_device, _window.Handle, SDL_GPUSwapchainComposition.SDL_GPU_SWAPCHAINCOMPOSITION_SDR, PresentMode(mode)))
            {
                Console.Error.WriteLine($"VSync {mode}: {SDL_GetError()}");
                return;
            }

            _vsync = mode;
        }
    }

    private static SDL_GPUPresentMode PresentMode(VSync mode) => mode switch
    {
        VSync.Off => SDL_GPUPresentMode.SDL_GPU_PRESENTMODE_IMMEDIATE,
        VSync.Adaptive => SDL_GPUPresentMode.SDL_GPU_PRESENTMODE_MAILBOX,
        _ => SDL_GPUPresentMode.SDL_GPU_PRESENTMODE_VSYNC,
    };

    /// <summary>The frame's size (the last drawn).</summary>
    public Resolution Target => new((int)_targetWidth, (int)_targetHeight);

    /// <summary>The swapchain's and the depth buffer's formats, e.g. B8G8R8A8_UNORM, D32_FLOAT.</summary>
    public string SwapchainFormat { get; }
    public string DepthFormat { get; }

    /// <summary>The samples per pixel (MSAA; 1: off).</summary>
    public int Samples => (int)_samples;

    private static string FormatName(SDL_GPUTextureFormat format) =>
        format.ToString().Replace("SDL_GPU_TEXTUREFORMAT_", "", StringComparison.Ordinal);

    /// <summary>The offscreen frame of a hidden window (the shown one's default size).</summary>
    public static readonly Resolution DefaultHiddenSize = new(1280, 960);

    /// <summary>The frame size of a hidden window (--size; tests, videos).</summary>
    public Resolution HiddenSize { get; set; } = DefaultHiddenSize;

    /// <summary>A hidden window's frames through its swapchain too (--swapchain; tests).</summary>
    public HiddenFrames HiddenFrames { get; set; }

    /// <summary>The cameras' and the canvas' aspect: the frame's, or an eye's half in full side by side.</summary>
    public float AspectRatio => StereoModes.EyeAspect(StereoMode, _targetHeight == 0 ? 4f / 3f : (float)_targetWidth / _targetHeight);

    private void RenderScene(ref SDL_GPUCommandBuffer* commands, View view, Vector4 clearColour)
    {
        if (Lighting is { } lighting)
        {
            RenderEnhanced(ref commands, view, clearColour, lighting);
            return;
        }

        var colourTarget = SceneColour(_frame, clearColour);
        var depthTarget = SceneDepth();
        var pass = BeginPass(commands, &colourTarget, &depthTarget);
        _passSamples = _samples;
        if (Panorama != null && Backdrop == Backdrop.Sky)
        {
            DrawSky(commands, pass, view, Panorama);
        }

        // With insets, the canvas waits for them (Renderer.Insets.cs).
        var canvasNow = _insets.Count == 0;
        foreach (var order in Passes)
        {
            if (order != Pass.Overlay || canvasNow)
            {
                DrawPass(ref commands, ref pass, _commands, order, view);
            }
        }

        SDL_EndGPURenderPass(pass);
        _passSamples = 1;
        if (!canvasNow)
        {
            RenderInsets(ref commands);
        }
    }

    /// <summary>The panorama behind everything: one screen-filling triangle.</summary>
    private void DrawSky(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, View view, Panorama panorama)
    {
        SDL_BindGPUGraphicsPipeline(pass, Pipeline(new PipelineKey("sky", Pass.DoubleSided, Primitive.Triangles, Geometry.Screen, _passSamples, Output.Colour)));
        // The camera's own axes give steady directions (ViewCamera.SkyDirections); a stereo eye's
        // shifted frustum keeps the inverse.
        var clipToDirection = view.Camera is { } camera && _eyeSide == 0f ? camera.SkyDirections() : view.ClipToDirection;
        SDL_PushGPUVertexUniformData(commands, 0, (IntPtr)(&clipToDirection), (uint)sizeof(Matrix4x4));
        BindPanorama(commands, pass, panorama, panorama.Sky, 0f, view.Position);
        DrawPrimitives(pass, ScreenTriangle, 0, Primitive.Triangles);
    }

    private void BindPanorama(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, Panorama panorama, int texture, float rowShift, Vector3 camera)
    {
        var samplers = stackalloc SDL_GPUTextureSamplerBinding[2];
        samplers[0] = new SDL_GPUTextureSamplerBinding { texture = (SDL_GPUTexture*)_textures[texture], sampler = _clampSampler };
        samplers[1] = new SDL_GPUTextureSamplerBinding { texture = (SDL_GPUTexture*)_textures[panorama.Palette], sampler = _clampSampler };
        SDL_BindGPUFragmentSamplers(pass, 0, samplers, 2);

        var uniforms = new PanoramaUniforms
        {
            WrapWidth = panorama.WrapWidth,
            HorizonRow = panorama.HorizonRow,
            Offset = panorama.Offset,
            Height = panorama.Height,
            TopColour = panorama.TopColour,
            BottomColour = panorama.BottomColour,
            RowShift = rowShift,
            Sampling = panorama.Sampling,
            Camera = new Vector4(camera, 1f),
        };
        SDL_PushGPUFragmentUniformData(commands, 0, (IntPtr)(&uniforms), (uint)sizeof(PanoramaUniforms));
    }

    /// <summary>Draws the commands of one pass, in their order.</summary>
    private void DrawPass(ref SDL_GPUCommandBuffer* commands, ref SDL_GPURenderPass* pass, List<DrawCommand> source, Pass order, View view)
    {
        foreach (ref readonly var command in CollectionsMarshal.AsSpan(source))
        {
            if (command.Material.Pass == order)
            {
                Reserve(ref commands, ref pass);
                UseBand(pass, command.Band);
                DrawOne(commands, pass, command, view);
            }
        }
    }

    /// <summary>The open pass's depth band (null: the whole range, as a pass begins).</summary>
    private DepthBand? _band;

    /// <summary>Depth tested within <paramref name="band"/>'s range: the viewport's area kept (the
    /// frame's, or an inset's).</summary>
    private void UseBand(SDL_GPURenderPass* pass, DepthBand band)
    {
        if (_band == band)
        {
            return;
        }

        var viewport = _openPass.Clipped ? _openPass.Viewport : new SDL_GPUViewport { w = _targetWidth, h = _targetHeight };
        (viewport.min_depth, viewport.max_depth) = DepthBands.Of(band);
        SDL_SetGPUViewport(pass, &viewport);
        _band = band;
    }

    private void DrawOne(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, in DrawCommand command, View view)
    {
        var material = command.Material;
        var mode = ModeOf(command);
        var program = material.Pass == Pass.Mirror ? "mirror" : mode != null ? "enhanced" : "palette";
        SDL_BindGPUGraphicsPipeline(pass, Pipeline(new PipelineKey(program, material.Pass, command.Primitive, Geometry.Mesh, _passSamples, Output.Colour, material.Blend)));

        var binding = new SDL_GPUBufferBinding { buffer = (SDL_GPUBuffer*)_buffers[command.Mesh] };
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);

        // The canvas is already in clip space; the rest goes through the camera.
        // Layers and lines are drawn nearer than the surfaces they lie on.
        var nearer = command.World * DepthPull.Of(view.Position, command.Primitive, material.DepthLayer);
        var transform = material.Pass == Pass.Overlay ? command.World : nearer * view.ViewProjection;
        if (mode is { } enhanced)
        {
            DrawEnhanced(commands, pass, command, transform, enhanced, view);
            return;
        }

        SDL_PushGPUVertexUniformData(commands, 0, (IntPtr)(&transform), (uint)sizeof(Matrix4x4));

        // Mirrors show nothing without a panorama.
        if (material.Pass == Pass.Mirror)
        {
            if (Panorama == null)
            {
                return;
            }

            BindPanorama(commands, pass, Panorama, Panorama.MirrorSky, material.RowShift, view.Position);
            DrawPrimitives(pass, command.Count, command.First, command.Primitive);
            return;
        }

        // Flat colours still bind a texture pair: any will do.
        var textured = material.Texture != Material.None;
        var samplers = stackalloc SDL_GPUTextureSamplerBinding[2];
        samplers[0] = new SDL_GPUTextureSamplerBinding { texture = (SDL_GPUTexture*)_textures[textured ? material.Texture : 0], sampler = _repeatSampler };
        samplers[1] = new SDL_GPUTextureSamplerBinding { texture = (SDL_GPUTexture*)_textures[textured ? material.Palette : 0], sampler = _clampSampler };
        SDL_BindGPUFragmentSamplers(pass, 0, samplers, 2);

        var uniforms = new FragmentUniforms
        {
            Colour = material.Colour,
            Textured = textured ? 1 : 0,
            FrameCount = material.FrameCount,
            Frame = command.Frame,
        };
        SDL_PushGPUFragmentUniformData(commands, 0, (IntPtr)(&uniforms), (uint)sizeof(FragmentUniforms));
        DrawPrimitives(pass, command.Count, command.First, command.Primitive);
    }

    private void Blit(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* swapchain, uint width, uint height)
    {
        var blit = new SDL_GPUBlitInfo
        {
            source = new SDL_GPUBlitRegion { texture = _shown, w = _targetWidth, h = _targetHeight },
            destination = new SDL_GPUBlitRegion { texture = swapchain, w = width, h = height },
            load_op = SDL_GPULoadOp.SDL_GPU_LOADOP_DONT_CARE,
            // A scaled frame is smoothed; at 100 % pixels copy as they are.
            filter = _targetWidth == width && _targetHeight == height ? SDL_GPUFilter.SDL_GPU_FILTER_NEAREST : SDL_GPUFilter.SDL_GPU_FILTER_LINEAR,
        };
        SDL_BlitGPUTexture(commands, &blit);
    }

    /// <summary>Downloads the frame and writes it as a 32-bit BMP.</summary>
    private void Save(SDL_GPUCommandBuffer* commands, string path)
    {
        var size = _targetWidth * _targetHeight * BytesPerPixel;
        var transfer = CreateTransfer(size, SDL_GPUTransferBufferUsage.SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD);
        var copy = SDL_BeginGPUCopyPass(commands);
        var source = new SDL_GPUTextureRegion { texture = _shown, w = _targetWidth, h = _targetHeight, d = 1 };
        var destination = new SDL_GPUTextureTransferInfo { transfer_buffer = transfer };
        SDL_DownloadFromGPUTexture(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);
        var fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        SDL_WaitForGPUFences(_device, true, &fence, 1);
        SDL_ReleaseGPUFence(_device, fence);

        var mapped = SDL_MapGPUTransferBuffer(_device, transfer, false);
        var rgba = new ReadOnlySpan<byte>((void*)mapped, (int)size).ToArray();
        SDL_UnmapGPUTransferBuffer(_device, transfer);
        SDL_ReleaseGPUTransferBuffer(_device, transfer);
        Bmp.Write(path, (int)_targetWidth, (int)_targetHeight, rgba);
    }

    private void EnsureTargets(uint width, uint height)
    {
        var samples = SupportedSamples();
        if (_target != null && width == _targetWidth && height == _targetHeight && samples == _samples
            && (StereoMode != Stereo.Off) == (_eyes[0] != null))
        {
            return;
        }

        if (_target != null)
        {
            SDL_ReleaseGPUTexture(_device, _target);
            SDL_ReleaseGPUTexture(_device, _depth);
        }

        ReleaseSizedTargets();
        _target = CreateTexture(ColourFormat, width, height, SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_SAMPLER);
        _depth = CreateTexture(_depthFormat, width, height, SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
        _targetWidth = width;
        _targetHeight = height;
        _samples = samples;
        CreateMultisampled(width, height);
        EnsureEyes();
    }

    private SDL_GPUTexture* CreateTexture(SDL_GPUTextureFormat format, uint width, uint height, SDL_GPUTextureUsageFlags usage,
        uint levels = 1, uint samples = 1)
    {
        var info = new SDL_GPUTextureCreateInfo
        {
            type = SDL_GPUTextureType.SDL_GPU_TEXTURETYPE_2D,
            format = format,
            usage = usage,
            width = width,
            height = height,
            layer_count_or_depth = 1,
            num_levels = levels,
            sample_count = SampleCount(samples),
        };
        var texture = SDL_CreateGPUTexture(_device, &info);
        Check(texture != null, "SDL_CreateGPUTexture");
        return texture;
    }

    private SDL_GPUTransferBuffer* CreateTransfer(uint size, SDL_GPUTransferBufferUsage usage)
    {
        var info = new SDL_GPUTransferBufferCreateInfo { usage = usage, size = size };
        return SDL_CreateGPUTransferBuffer(_device, &info);
    }

    /// <summary>Uploads up to this size go through one kept buffer, grown to the largest (cycled:
    /// uploads still pending keep theirs); larger ones (a level's first frame) through a buffer of
    /// their own, released by the caller when temporary.</summary>
    private const int StagingKeep = 4 << 20;

    /// <summary>An upload buffer of <paramref name="size"/> bytes, mapped (the caller unmaps it).</summary>
    private Span<byte> MapStaging(int size, out SDL_GPUTransferBuffer* transfer, out bool temporary)
    {
        temporary = size > StagingKeep;
        if (temporary)
        {
            transfer = CreateTransfer((uint)size, SDL_GPUTransferBufferUsage.SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
            return new Span<byte>((void*)SDL_MapGPUTransferBuffer(_device, transfer, false), size);
        }

        if (_stagingSize < size)
        {
            if (_staging != null)
            {
                SDL_ReleaseGPUTransferBuffer(_device, _staging);
            }

            _stagingSize = Math.Max((uint)size, _stagingSize * 2);
            _staging = CreateTransfer(_stagingSize, SDL_GPUTransferBufferUsage.SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
        }

        transfer = _staging;
        return new Span<byte>((void*)SDL_MapGPUTransferBuffer(_device, transfer, true), size);
    }

    private SDL_GPUTransferBuffer* _staging;
    private uint _stagingSize;

    private void Upload(SDL_GPUTexture* texture, uint width, uint height, byte[] pixels)
    {
        var copy = UploadPass(pixels, out var transfer, out var offset);
        var source = new SDL_GPUTextureTransferInfo { transfer_buffer = transfer, offset = offset, pixels_per_row = width, rows_per_layer = height };
        var destination = new SDL_GPUTextureRegion { texture = texture, w = width, h = height, d = 1 };
        SDL_UploadToGPUTexture(copy, &source, &destination, false);
    }

    /// <summary>Texture and mesh uploads share one command buffer and one upload buffer, submitted
    /// when it's full and before each frame. A level's thousands of uploads, each submitted on its
    /// own (a fence) through a cycled buffer (a copy of it), outran the GPU and ran out of memory
    /// (Vulkan on NVIDIA: VK_ERROR_OUT_OF_HOST_MEMORY).
    /// <code>
    ///   Upload ─► [batch: copy, copy, …] ──full / frame──► submit, wait for the batch before
    /// </code></summary>
    private const uint UploadBatchSize = 4 << 20;
    /// <summary>Uploads start at multiples of this (D3D12's texture placement alignment).</summary>
    private const uint UploadAlignment = 512;

    private SDL_GPUTransferBuffer* _uploadBuffer;
    private SDL_GPUCommandBuffer* _uploadCommands;
    private SDL_GPUCopyPass* _uploadCopy;
    private uint _uploadUsed;
    private SDL_GPUFence* _uploadFence;

    /// <summary>The batch's copy pass, <paramref name="data"/> copied to <paramref name="transfer"/>
    /// at <paramref name="offset"/>; a larger upload gets a buffer of its own, freed once done.</summary>
    private SDL_GPUCopyPass* UploadPass(ReadOnlySpan<byte> data, out SDL_GPUTransferBuffer* transfer, out uint offset)
    {
        var size = (uint)data.Length;
        offset = (_uploadUsed + UploadAlignment - 1) / UploadAlignment * UploadAlignment;
        if (size <= UploadBatchSize && offset + size > UploadBatchSize)
        {
            FlushUploads();
            offset = 0;
        }

        if (_uploadCommands == null)
        {
            _uploadCommands = SDL_AcquireGPUCommandBuffer(_device);
            _uploadCopy = SDL_BeginGPUCopyPass(_uploadCommands);
        }

        if (size > UploadBatchSize)
        {
            offset = 0;
            transfer = CreateTransfer(size, SDL_GPUTransferBufferUsage.SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
            data.CopyTo(new Span<byte>((void*)SDL_MapGPUTransferBuffer(_device, transfer, false), data.Length));
            SDL_UnmapGPUTransferBuffer(_device, transfer);
            SDL_ReleaseGPUTransferBuffer(_device, transfer);
            return _uploadCopy;
        }

        if (_uploadBuffer == null)
        {
            _uploadBuffer = CreateTransfer(UploadBatchSize, SDL_GPUTransferBufferUsage.SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
        }

        // A new batch cycles off the buffer the GPU may still read; later uploads add to it.
        transfer = _uploadBuffer;
        var mapped = (byte*)SDL_MapGPUTransferBuffer(_device, transfer, offset == 0);
        data.CopyTo(new Span<byte>(mapped + offset, data.Length));
        SDL_UnmapGPUTransferBuffer(_device, transfer);
        _uploadUsed = offset + size;
        return _uploadCopy;
    }

    /// <summary>Submits the batch, then waits for the one before: at most two on the GPU.</summary>
    private void FlushUploads()
    {
        if (_uploadCommands == null)
        {
            return;
        }

        SDL_EndGPUCopyPass(_uploadCopy);
        var fence = SDL_SubmitGPUCommandBufferAndAcquireFence(_uploadCommands);
        WaitFor(_uploadFence);
        _uploadFence = fence;
        _uploadCommands = null;
        _uploadCopy = null;
        _uploadUsed = 0;
    }

    public void Dispose()
    {
        FlushUploads();
        WaitFor(_uploadFence);
        WaitFor(_lastFrame);
        SDL_WaitForGPUIdle(_device);
        foreach (var texture in _textures)
        {
            SDL_ReleaseGPUTexture(_device, (SDL_GPUTexture*)texture);
        }

        foreach (var buffer in _buffers)
        {
            SDL_ReleaseGPUBuffer(_device, (SDL_GPUBuffer*)buffer);
        }

        foreach (var dynamic in _dynamic.Values)
        {
            SDL_ReleaseGPUTransferBuffer(_device, (SDL_GPUTransferBuffer*)dynamic.Transfer);
        }

        foreach (var pipeline in _pipelines.Values)
        {
            SDL_ReleaseGPUGraphicsPipeline(_device, (SDL_GPUGraphicsPipeline*)pipeline);
        }

        if (_target != null)
        {
            SDL_ReleaseGPUTexture(_device, _target);
            SDL_ReleaseGPUTexture(_device, _depth);
        }

        ReleaseSizedTargets();
        ReleaseShadowMap();
        if (_staging != null)
        {
            SDL_ReleaseGPUTransferBuffer(_device, _staging);
        }

        if (_uploadBuffer != null)
        {
            SDL_ReleaseGPUTransferBuffer(_device, _uploadBuffer);
        }

        SDL_ReleaseGPUSampler(_device, _repeatSampler);
        SDL_ReleaseGPUSampler(_device, _clampSampler);
        SDL_ReleaseGPUSampler(_device, _mipSampler);
        DisposeColours();
        SDL_ReleaseWindowFromGPUDevice(_device, _window.Handle);
        SDL_DestroyGPUDevice(_device);
    }
}
