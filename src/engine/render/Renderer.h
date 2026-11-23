#pragma once

#include "core/Drawing.h"
#include "core/DotNet.h"
#include "core/Numerics.h"
#include "core/TimeSpan.h"
#include "engine/platform/Display.h"
#include "engine/render/DepthBands.h"
#include "engine/render/GpuBackend.h"
#include "engine/render/PointLights.h"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Mdk::Engine::Platform
{
    class Window;
}

namespace Mdk::Engine::Render
{
    // A corner of a triangle: position (MDK coordinates, Z up), texture coordinates (0-1
    // over one frame) and a colour (RGBA8, R in the low byte; white unless given) multiplying the
    // material's, interpolated across the triangle (Gouraud).
    struct Vertex
    {
        static constexpr uint White = 0xFFFFFFFFu;

        Vector3 Position;
        Vector2 Uv;
        // C#'s default(Vertex) is all zero; the constructors give White.
        uint Colour = 0;

        constexpr Vertex() = default;
        constexpr Vertex(Vector3 position, Vector2 uv) : Position(position), Uv(uv), Colour(White) {}
        constexpr Vertex(Vector3 position, Vector2 uv, uint colour) : Position(position), Uv(uv), Colour(colour) {}

        // A colour packed for Colour.
        static constexpr uint Rgba(byte r, byte g, byte b, byte a)
        {
            return r | static_cast<uint>(g) << GreenShift | static_cast<uint>(b) << BlueShift | static_cast<uint>(a) << AlphaShift;
        }

    private:
        static constexpr int GreenShift = 8;
        static constexpr int BlueShift = 16;
        static constexpr int AlphaShift = 24;
    };

    // How a batch is drawn.
    enum class Pass
    {
        // Opaque, back faces culled (arenas).
        Solid,
        // Opaque, both faces (models).
        DoubleSided,
        // The panorama where the sky behind would be, both faces (mirrors).
        Mirror,
        // Blended by alpha, both faces, after the opaque ones (glass).
        Blended,
        // Opaque, both faces, over the scene without depth (the fall's explosions).
        OnTop,
        // The 2D canvas over the scene (HUD, menus): blended, no depth.
        Overlay,
    };

    // How a blended pass meets what's behind: mixed by alpha, or added (scaled by alpha).
    enum class Blend { Alpha, Add };

    // What a mesh's vertices make: triangles (three each) or lines (two each, one pixel wide).
    enum class Primitive { Triangles, Lines };

    // How a surface is shaded.
    enum class Shading
    {
        // The original: nearest texels, unlit.
        Original,
        // The enhanced look: filtered texels, lit by the sun and the light all around, shadowed,
        // hazy; casts shadows.
        Lit,
        // The enhanced look's sprites: filtered texels, edges cut where half covered, hazy; no
        // light and no shadow.
        Sprite,
    };

    // How images are sampled: nearest pixels or filtered.
    enum class Sampling { Nearest, Linear };

    // Multisample anti-aliasing of the frame.
    enum class AntiAliasing { Off, X2, X4 };

    // Whether the sun casts shadows.
    enum class Shadows { Off, On };

    // How much the enhanced look's passes cost: Full (desktops), or Mobile (Android: a
    // 1024² shadow map, the ambient occlusion at half the frame's sides; the post pass blurs it up).
    enum class EffectQuality { Full, Mobile };

    // The enhanced look's light: the sun (the way its light goes, its linear colour), the
    // light from the sky above and from the ground below (linear colours, blended by a face's
    // up component), the exposure scaling it all, the sun's shadows and how far they reach, the glow,
    // and the haze (its colour, its density per unit).
    struct Lighting
    {
        Vector3 SunDirection;
        Vector3 Sun;
        Vector3 Sky;
        Vector3 Ground;
        float Exposure = 0;
        Render::Shadows Shadows = Render::Shadows::Off;
        float ShadowDistance = 0;
        float Glow = 0;
        Vector4 HazeColour;
        float HazeDensity = 0;

        bool operator==(const Lighting&) const = default;
    };

    // A surface: an index texture through a palette, a flat colour, or a mirror showing the
    // panorama RowShift rows lower or higher; Shading picks the
    // original look or the enhanced one; Blend applies to the blended passes;
    // DepthLayer draws it that many steps nearer in depth (a poster over its wall).
    struct Material
    {
        static constexpr int None = -1;

        int Texture = 0;
        int Palette = 0;
        Vector4 Colour;
        int FrameCount = 0;
        Render::Pass Pass = Render::Pass::Solid;
        float RowShift = 0.0f;
        Render::Shading Shading = Render::Shading::Original;
        Render::Blend Blend = Render::Blend::Alpha;
        int DepthLayer = 0;

        constexpr Material() = default;
        constexpr Material(int texture, int palette, Vector4 colour, int frameCount, Render::Pass pass, float rowShift = 0.0f,
            Render::Shading shading = Render::Shading::Original, Render::Blend blend = Render::Blend::Alpha, int depthLayer = 0)
            : Texture(texture), Palette(palette), Colour(colour), FrameCount(frameCount), Pass(pass), RowShift(rowShift),
              Shading(shading), Blend(blend), DepthLayer(depthLayer) {}

        bool operator==(const Material&) const = default;

        static constexpr Material Flat(Vector4 colour, Render::Pass pass) { return {None, None, colour, 1, pass}; }

        static constexpr Material Mirror(float rowShift) { return {None, None, Vector4::One(), 1, Render::Pass::Mirror, rowShift}; }
    };

    // The level's panorama (index texture and palette ids): WrapWidth
    // columns cover 360 degrees, HorizonRow at eye level; the screen above and below
    // it takes palette colours TopColour and BottomColour. An optional HdSky is the
    // enhanced look's higher-resolution RGBA replacement.
    struct Panorama
    {
        int Sky = 0;
        int MirrorSky = 0;
        int Palette = 0;
        float WrapWidth = 0;
        float HorizonRow = 0;
        float Offset = 0;
        float Height = 0;
        int TopColour = 0;
        int BottomColour = 0;
        Render::Sampling Sampling = Render::Sampling::Nearest;
        std::optional<int> HdSky;

        bool operator==(const Panorama&) const = default;
    };

    // What is behind the scene (the scripts' sky modes, 0x574304): the panorama, the clear
    // colour, or the last frame kept.
    enum class Backdrop { Sky, Clear, Keep };

    // A view's camera: what a stereo eye is built from (the projection is shifted off-centre
    // for convergence).
    struct ViewCamera
    {
        Vector3 Forward;
        Vector3 Up;
        Matrix4x4 Projection;
        float Near = 0;

        bool operator==(const ViewCamera&) const = default;

        // Clip space to world directions for the sky, built from the camera's axes and its
        // projection: (x, y, 1, 1) goes to forward + right·x′ + up·y′, on one plane. sky.hlsl
        // blends its screen triangle's corners across the pixels, which is right only for corners in one
        // plane: inverting rotation × projection in float32 (near 0.5, far 20000) put them off it at clip
        // depth 1, by an error that changed with the camera's angle, and the sky shook by several pixels
        // when turning while the world stayed still.
        Matrix4x4 SkyDirections() const;
    };

    // The camera: its view-projection, the inverse of its rotation and projection (clip space
    // to world directions, for the sky), and its position.
    struct View
    {
        Matrix4x4 ViewProjection;
        Matrix4x4 ClipToDirection;
        Vector3 Position;
        std::optional<ViewCamera> Camera;

        bool operator==(const View&) const = default;

        // The view from an eye side (−1 the left, +1 the right; 0 the view
        // itself): the eyes are separation apart, parallel, and their images converge
        // at convergence (0 or less: at infinity). A view without a camera (the 2D
        // screens) stays as it is.
        View Eye(float side, float separation, float convergence) const;
    };

    // What the last frame drew: its GPU draw calls and triangles (lines count none), and the
    // milliseconds it waited for the GPU (the swapchain: earlier frames, the display's refresh; with
    // GpuSync::Wait, this frame's GPU work).
    struct RenderStats
    {
        int DrawCalls = 0;
        int Triangles = 0;
        float GpuWait = 0.0f;

        bool operator==(const RenderStats&) const = default;
    };

    // Whether a frame returns once submitted, or waits until the GPU has drawn it (measures).
    enum class GpuSync { Async, Wait };

    // Where a hidden window's frames go: off screen only, or also through its swapchain as a
    // shown window's do (tests).
    enum class HiddenFrames { Offscreen, Swapchain };

    // What an inset shows: the frame's scene again through its own camera, or only what was
    // drawn between Renderer::BeginInset and Renderer::EndInset.
    enum class InsetContent { Scene, Own };

    // Whether an inset is drawn under the 2D canvas or over it.
    enum class InsetLayer { UnderCanvas, OverCanvas };

    // How a frame holds the two eyes of a stereo view; Renderer::StereoMode.
    enum class Stereo
    {
        // One view: no stereo.
        Off,
        // Side by side: the left eye's image in the left half of the frame (3D TVs).
        Sbs,
        // Full side by side: each eye's half with its own aspect, seen as it is (VR viewers,
        // PC glasses).
        SbsFull,
        // Full side by side with the halves exchanged, for crossed free viewing (the left eye's
        // image in the right half).
        CrossView,
        // Row-interleaved: even rows the left eye (row-interleaved displays, shutter glasses).
        Interlaced,
        // Row-interleaved the other way: even rows the right eye.
        InterlacedReversed,
        // Top and bottom (over-under): the left eye's image in the top half.
        Tab,
        // Top and bottom with the halves exchanged: the right eye's image in the top half.
        TabReversed,
        // A phone in a VR viewer: full side by side, each half bent against the lens
        // (barrel), the HUD and the menus kept to the middle (StereoModes::CanvasZoom).
        Vr,
    };

    // The stereo modes and their names (the options page, the console, --stereo).
    class StereoModes
    {
    public:
        // The modes' names, by value (the options page's).
        static constexpr std::array<std::string_view, 9> Names{
            "Off", "Side by side (half)", "Side by side (full)", "Cross view", "Interlaced", "Interlaced reverse", "Top and bottom", "Top and bottom rev.", "VR viewer (phone)"};

        // The most the eyes go apart and the farthest their images converge (the sliders).
        static constexpr float MaxSeparation = 2.0f;
        static constexpr float MinConvergence = 1.0f;
        static constexpr float MaxConvergence = 4000.0f;

        // The mode of a name: the page's, or a short one (off, sbs, sbsfull, crossview, int, intr).
        static bool TryParse(const std::string& name, Stereo& stereo);

        // An eye's camera aspect in a frame of aspect: full side by side
        // halves it (16:9 → 8:9 an eye); the others keep it (the TV stretches half side by side back).
        static float EyeAspect(Stereo stereo, float aspect)
        {
            return stereo == Stereo::SbsFull || stereo == Stereo::CrossView || stereo == Stereo::Vr ? aspect / 2.0f : aspect;
        }

        // The canvas' size in an eye's view: smaller in the VR viewer, whose lenses blur the edges.
        static float CanvasZoom(Stereo stereo) { return stereo == Stereo::Vr ? VrCanvas : 1.0f; }

    private:
        // The VR viewer's canvas: the share of an eye's view the HUD and the menus take.
        static constexpr float VrCanvas = 0.7f;
    };

    // Draws paletted triangle meshes with SDL_GPU.
    //  game ──► Renderer ──► offscreen colour + depth ──blit──► swapchain
    //              │                    └─download──► screenshot (BMP)
    //              └ meshes, index textures, palettes (GPU resources by id)
    class Renderer
    {
    public:
        // ---- Renderer.cs ----

        // The 2D canvas: CanvasHeight units high like the original's view, as
        // wide as the window's aspect makes it; quads collected each frame into one dynamic mesh.
        static constexpr float CanvasHeight = 360.0f;

        static constexpr int FullScale = 100;
        static constexpr int AutoScale = 0;
        // The render scales (%): the frame is drawn at this share of the window's size, then
        // stretched to it (below 100: faster, blurrier; above: supersampled); Auto picks one by the
        // window's pixels (AutoPercent).
        static constexpr std::array<int, 5> Scales{AutoScale, 50, 100, 150, 200};

        // The offscreen frame of a hidden window (the shown one's default size).
        static constexpr Platform::Resolution DefaultHiddenSize{1280, 960};

        // The textures and meshes created so far; Release frees those created after.
        struct Scope
        {
            int Textures = 0;
            int Buffers = 0;

            bool operator==(const Scope&) const = default;
        };

        explicit Renderer(Platform::Window& window, GpuBackend backend = GpuBackend::Auto);
        // C#'s Dispose.
        ~Renderer();
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        // A point of the window (in its coordinates) on the canvas.
        Vector2 CanvasPoint(float x, float y) const;

        // The canvas clear of the window's cut-outs and rounded corners (its safe area's
        // insets taken off the whole canvas); all of it on a desktop.
        RectangleF CanvasSafeArea() const;

        Scope Mark() const { return {static_cast<int>(_textures.size()), static_cast<int>(_buffers.size())}; }

        // Frees the textures and meshes created after scope (a screen of
        // the game ends): their ids are reused.
        void Release(Scope scope);

        // The canvas's width for the window's aspect.
        float CanvasWidth() const { return CanvasHeight * AspectRatio(); }

        // Draws part of an index texture (source in its pixels) on the canvas,
        // tinted (alpha fades it); palette index 0 stays transparent.
        void DrawImage(int texture, int palette, Vector2 textureSize, RectangleF source, RectangleF target, Vector4 tint);

        // Fills a rectangle of the canvas with a colour (alpha blends it).
        void FillRect(RectangleF target, Vector4 colour);

        // Adds a colour, scaled by its alpha, to a rectangle of the canvas (clamped at white).
        void AddRect(RectangleF target, Vector4 colour);

        // A rectangle's outline, width units thick, inside it.
        void FrameRect(RectangleF target, float width, Vector4 colour);

        // The sky and mirrors' panorama; without one the screen is cleared to a colour.
        std::optional<Render::Panorama> Panorama;

        // What the scene is drawn over; the sky needs a Panorama.
        Render::Backdrop Backdrop = Render::Backdrop::Sky;

        // The backend asked for at start (Options, Display: a change applies after a restart).
        GpuBackend Requested() const { return _requested; }

        // The device's backend, name and driver.
        const GpuInfo& Gpu() const { return _gpu; }

        // The backends of this OS whose shaders are embedded and whose driver SDL has.
        static std::vector<GpuBackend> Backends();

        // An 8-bit index texture; frames of animated ones stacked vertically.
        int CreateIndexTexture(int width, int height, std::span<const byte> indices);

        // New pixels for a texture of the same size (a video's frame, a fading palette).
        void UpdateTexture(int texture, int width, int height, std::span<const byte> pixels);

        // A 256-colour palette, RGBA8.
        int CreatePalette(std::span<const byte> rgba);

        int CreateRgbaTexture(int width, int height, std::span<const byte> rgba);

        // A vertex buffer of triangles (three vertices each).
        int CreateMesh(std::span<const Vertex> vertices);

        // A vertex buffer of up to capacity vertices, rewritten with
        // UpdateMesh (sprites).
        int CreateDynamicMesh(int capacity);

        // New vertices for a dynamic mesh, uploaded before the next frame is drawn.
        void UpdateMesh(int mesh, std::span<const Vertex> vertices);

        // Room for count vertices of this frame only (posed models), drawn
        // from mesh at first; empty when the frame's stream is full.
        // No buffer is made or filled per draw: the stream is uploaded once, before the frame.
        std::span<Vertex> Stream(int count, int& mesh, int& first);

        // The depth band of the draws queued from now on (front again after each frame).
        DepthBand Band = DepthBand::Front;

        // Queues triangles of a mesh for the next frame; frame picks an animated texture's frame.
        void Draw(int mesh, int firstVertex, int vertexCount, const Material& material, int frame = 0);

        // Queues triangles of a mesh placed in the world by world (models).
        void Draw(int mesh, int firstVertex, int vertexCount, const Material& material, int frame, const Matrix4x4& world);

        // Queues lines of a mesh (two vertices each) in a material's colour (not a mirror).
        void DrawLines(int mesh, int firstVertex, int vertexCount, const Material& material);

        // Draws the queued batches (opaque first, then blended) and shows them. With
        // screenshot, also saves the frame as a BMP.
        void Present(const View& view, Vector4 clearColour, const std::optional<std::string>& screenshot = std::nullopt);

        // How Present meets the GPU.
        GpuSync Sync = GpuSync::Async;

        // Draws on the canvas over every frame, whoever presents it, just before (the
        // developer tools).
        std::function<void()> Overlay;

        // The last presented frame's draws (the debug overlay).
        RenderStats Stats() const { return _stats; }

        // The render scale (%) of a shown window; a hidden one draws HiddenSize.
        int RenderScale = FullScale;

        // The render scale (%) Auto picks for a window: its frame about 1080p's pixels, never
        // above the window's own nor below half its sides.
        //   2880 x 1800 tablet: √(2.07 MP / 5.18 MP) = 63 %   2400 x 1080 phone: 89 %   1920 x 1080: 100 %
        static int AutoPercent(uint width, uint height);

        // VSync as asked for; the mode used falls back to one the window supports.
        Render::VSync VSync() const { return _vsync; }
        void SetVSync(Render::VSync value);

        // The frame's size (the last drawn).
        Platform::Resolution Target() const { return {static_cast<int>(_targetWidth), static_cast<int>(_targetHeight)}; }

        // The swapchain's and the depth buffer's formats, e.g. B8G8R8A8_UNORM, D32_FLOAT.
        const std::string& SwapchainFormat() const { return _swapchainFormat; }
        const std::string& DepthFormat() const { return _depthFormatName; }

        // The samples per pixel (MSAA; 1: off).
        int Samples() const { return static_cast<int>(_samples); }

        // The frame size of a hidden window (--size; tests, videos).
        Platform::Resolution HiddenSize = DefaultHiddenSize;

        // A hidden window's frames through its swapchain too (--swapchain; tests).
        Render::HiddenFrames HiddenFrames = Render::HiddenFrames::Offscreen;

        // The cameras' and the canvas' aspect: the frame's, or an eye's half in full side by side.
        float AspectRatio() const;

        // ---- Renderer.Stereo.cs ----

        // How the frame holds the two eyes; Stereo::Off without stereo.
        Stereo StereoMode = Stereo::Off;

        // The eyes' distance apart (MDK units) and the distance their images converge at
        // (the same point on the screen in both; 0 or less: parallel rays, converging at infinity).
        float StereoSeparation = 0.25f;
        float StereoConvergence = 10.0f;

        // ---- Renderer.Enhanced.cs ----

        // The enhanced look's cost (Android's default: Mobile); a change makes the shadow map
        // and the occlusion's target again.
        EffectQuality Quality() const { return _quality; }
        void SetQuality(EffectQuality value);

        // The occlusion target's side for a frame's: whole texels of step
        // pixels, the last one partly off the frame (1801 / 2 ─► 901).
        static uint OcclusionSide(uint side, uint step) { return (side + step - 1) / step; }

        // The enhanced look's light for the scene; null: the original look.
        std::optional<Render::Lighting> Lighting;

        // The frame's point lights (the enhanced look's): the game fills them each frame.
        PointLights& Lights() { return _lights; }

        // How the canvas samples its images.
        Sampling CanvasSampling = Sampling::Nearest;

        Render::AntiAliasing AntiAliasing = Render::AntiAliasing::Off;

        // ---- Renderer.Gamma.cs ----

        static constexpr float MinGamma = 0.5f;
        static constexpr float MaxGamma = 2.0f;
        static constexpr float DefaultGamma = 1.0f;

        // Above 1 brighter (the darks lifted), below 1 darker.
        float Gamma = DefaultGamma;

        // ---- Renderer.Insets.cs ----

        // Starts an inset in area (canvas units) seen through
        // view; with InsetContent::Own, draws until
        // EndInset go into it.
        void BeginInset(const View& view, RectangleF area, InsetContent content, InsetLayer layer);

        void EndInset() { _open = nullptr; }

        // ---- Renderer.Colours.cs ----

        // Asks for a material's colour texture (the enhanced look's surfaces and sprites; nothing
        // for the others): those of a level's load are all made in one copy pass before its first frame.
        void Prepare(const Material& material);

        // Gives a material's colour texture instead of its index texture through its palette
        // (an HD texture: width x height a frame, any size,
        // premultiplied RGBA8, the material's frames stacked downwards). Kept until its index texture or
        // palette change (then expanded again) or its scope is released.
        void Replace(const Material& material, int width, int height, std::vector<byte> rgba);

        // Gives a canvas image (DrawImage's index texture through a palette) an
        // RGBA image of any size, premultiplied, drawn instead in the enhanced look's canvas (filtered,
        // with mips: sharp at the window's size). Kept through palette changes (fades don't reach it).
        void ReplaceImage(int texture, int palette, int width, int height, std::vector<byte> rgba);

        // Bytes of the colour textures made and asked for, with their mips (GPU memory).
        long long ColourBytes() const;

    private:
        // ---- Renderer.cs ----

        static constexpr SDL_GPUTextureFormat ColourFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        static constexpr int BytesPerPixel = 4;
        static constexpr int PaletteSize = 256;

        // ---- Renderer.Enhanced.cs (constants) ----

        static constexpr uint ShadowSize = 2048;
        static constexpr uint MobileShadowSize = 1024;
        // The ambient occlusion's pixels per side of one of its texels, by quality.
        static constexpr uint FullOcclusion = 1;
        static constexpr uint MobileOcclusion = 2;
        // Shadow acne: the sun's depth is pushed back by the surface's slope, the receiver's
        // point moved off its surface, and compared a little nearer (units).
        static constexpr float ShadowSlopeBias = 2.0f;
        static constexpr float ShadowNormalOffset = 0.5f;
        static constexpr float ShadowDepthBias = 0.5f;
        // Ambient occlusion: the disc's radius (units) and how dark it gets.
        static constexpr float OcclusionRadius = 12.0f;
        static constexpr float OcclusionStrength = 1.5f;
        // The glow: colours above the threshold shine; the mips blurred for it (the first of two).
        static constexpr float GlowThreshold = 0.8f;
        static constexpr float GlowIntensity = 0.8f;
        static constexpr float GlowMip = 3.0f;
        static constexpr uint SceneMips = 6;
        static constexpr float AllMips = 1000.0f;
        static constexpr int EnhancedSamplers = 4;
        static constexpr int DepthSamplers = 1;
        static constexpr int DefaultSamplers = 2;
        static constexpr int PostSamplers = 3;
        static constexpr uint SkySamplers = 3;
        // The enhanced fragment shader's uniform buffers: the draw's, then the frame's point
        // lights (pushed once per frame, kept by the command buffer for every pass).
        static constexpr uint EnhancedUniformBuffers = 2;
        static constexpr uint LightSlot = 1;
        // Per point light: its position and radius, then its colour.
        static constexpr int LightVectors = 2;
        static constexpr SDL_GPUTextureUsageFlags SampledDepthUsage =
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

        // ---- Renderer.Stereo.cs / Gamma.cs / SamplerHeap.cs / Colours.cs (constants) ----

        static constexpr uint StereoEyes = 2;
        static constexpr uint GammaSamplers = 1;
        static constexpr int SamplerHeap = 2048;
        // A draw's most samplers (the enhanced shader's).
        static constexpr int DrawSamplers = EnhancedSamplers;
        // The frame's samplers outside the passes' draw loops: sky, post, gamma, stereo,
        // mipmaps, blits (about 30 with stereo and insets).
        static constexpr int OtherSamplers = 64;
        static constexpr float MaxAnisotropy = 16.0f;
        // The scratch's size for a chunk of textures (a larger texture gets a chunk of its own).
        static constexpr int ChainChunk = 4 << 20;

        struct FragmentUniforms
        {
            Vector4 Colour;
            int Textured = 0;
            int FrameCount = 0;
            int Frame = 0;
            int Unused = 0;
        };

        struct PanoramaUniforms
        {
            float WrapWidth = 0;
            float HorizonRow = 0;
            float Offset = 0;
            float Height = 0;
            int TopColour = 0;
            int BottomColour = 0;
            float RowShift = 0;
            int Sampling = 0;
            Vector4 Camera;
        };

        // Whether a pipeline reads vertex buffers and the depth buffer (the sky does neither;
        // the enhanced look's post-processing has no depth buffer).
        enum class Geometry { Mesh, Screen, Post };

        // What a pipeline writes: colour (and depth), or depth alone: the camera's (the enhanced
        // look's ambient occlusion) or the sun's (its shadows, biased).
        enum class Output { Colour, Depth, Shadow };

        // A vertex's position, texture coordinates and colour.
        static constexpr uint VertexAttributes = 3;

        struct PipelineKey
        {
            std::string Program;
            Render::Pass Pass = Render::Pass::Solid;
            Render::Primitive Primitive = Render::Primitive::Triangles;
            Renderer::Geometry Geometry = Renderer::Geometry::Mesh;
            uint Samples = 1;
            Renderer::Output Output = Renderer::Output::Colour;
            Render::Blend Blend = Render::Blend::Alpha;

            bool operator==(const PipelineKey&) const = default;
        };

        struct PipelineKeyHash
        {
            size_t operator()(const PipelineKey& key) const;
        };

        // A shader format and the extension of its embedded programs (shaders/palette.vs.dxil).
        struct ShaderFormat
        {
            SDL_GPUShaderFormat Format = SDL_GPU_SHADERFORMAT_INVALID;
            std::string_view Extension;
        };

        // The formats in the order tried: Direct3D 12, Metal, Vulkan. The build embeds those its
        // tools could compile (CMakeLists.txt).
        static constexpr std::array<ShaderFormat, 3> ShaderFormats{{
            {SDL_GPU_SHADERFORMAT_DXIL, ".dxil"},
            {SDL_GPU_SHADERFORMAT_MSL, ".msl"},
            {SDL_GPU_SHADERFORMAT_SPIRV, ".spv"},
        }};

        // The program whose resource tells whether a format is embedded.
        static constexpr std::string_view ShaderProbe = "shaders/palette.vs";

        struct DrawCommand
        {
            int Mesh = 0;
            int First = 0;
            int Count = 0;
            Render::Material Material;
            int Frame = 0;
            Matrix4x4 World;
            Render::Primitive Primitive = Render::Primitive::Triangles;
            DepthBand Band = DepthBand::Front;
        };

        // C#'s (int First, int Count, Material Material) tuple.
        struct CanvasCommand
        {
            int First = 0;
            int Count = 0;
            Render::Material Material;
        };

        static constexpr int CanvasQuads = 4096;
        static constexpr int QuadVertices = 6;
        int _canvasMesh = 0;
        // The frame's streamed vertices (Stream), uploaded once before it's drawn.
        static constexpr int StreamVertices = 1 << 18;
        int _streamMesh = 0;
        std::vector<Vertex> _stream;
        std::vector<Vertex> _canvasVertices;
        std::vector<CanvasCommand> _canvasCommands;

        Platform::Window& _window;
        SDL_GPUDevice* _device = nullptr;
        ShaderFormat _shaderFormat;
        SDL_GPUTextureFormat _depthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        std::unordered_map<PipelineKey, SDL_GPUGraphicsPipeline*, PipelineKeyHash> _pipelines;
        SDL_GPUSampler* _repeatSampler = nullptr;
        SDL_GPUSampler* _clampSampler = nullptr;
        std::vector<SDL_GPUTexture*> _textures;
        std::vector<SDL_GPUBuffer*> _buffers;
        std::vector<DrawCommand> _commands;
        // A dynamic mesh's upload buffer and the bytes to copy before the next frame.
        struct DynamicMesh
        {
            SDL_GPUTransferBuffer* Transfer = nullptr;
            uint Pending = 0;
        };

        std::map<int, DynamicMesh> _dynamic;
        // The passes in their drawing order.
        static constexpr std::array<Render::Pass, 6> Passes{
            Render::Pass::Solid, Render::Pass::DoubleSided, Render::Pass::Mirror, Render::Pass::Blended, Render::Pass::OnTop, Render::Pass::Overlay};

        SDL_GPUTexture* _target = nullptr;
        SDL_GPUTexture* _depth = nullptr;
        uint _targetWidth = 0;
        uint _targetHeight = 0;

        GpuBackend _requested = GpuBackend::Auto;
        GpuInfo _gpu;
        std::string _swapchainFormat;
        std::string _depthFormatName;

        void AddQuad(RectangleF target, Vector2 uv0, Vector2 uv1, const Render::Material& material);

        static void Check(bool ok, const std::string& what);

        // The device for the backend asked for, else (failing, or not on this OS: a warning)
        // SDL's choice.
        static SDL_GPUDevice* CreateDevice(GpuBackend backend, ShaderFormat& format);

        // The shader format each backend takes.
        static ShaderFormat FormatOf(GpuBackend backend);

        static bool Embedded(ShaderFormat format);

        // A device of one backend (SDL_CreateGPUDeviceWithProperties with its driver name), or null.
        static SDL_GPUDevice* CreateFor(GpuBackend backend, ShaderFormat& format);

        // SDL's GPU validation (the D3D12 debug layer, Vulkan's validation layers, where
        // installed): on by default in SDL, kept to Debug builds (its cost, its debug breaks).
#if defined(NDEBUG)
        static constexpr bool GpuValidation = false;
#else
        static constexpr bool GpuValidation = true;
#endif

        // A device of an SDL driver (null: SDL's choice) taking a shader format, or null. It asks
        // for neither depth clamping nor clip distances: no pipeline uses them, and some GPUs (Mali-G52)
        // lack them, which would leave them no device at all.
        static SDL_GPUDevice* CreateWith(const std::optional<std::string>& driver, SDL_GPUShaderFormat format);

        GpuInfo ReadGpuInfo();

        // A device for the first embedded shader format a GPU driver takes (SDL_GPU_DRIVER can
        // choose the driver, e.g. vulkan on Windows), or null.
        static SDL_GPUDevice* CreateAuto(ShaderFormat& format);

        SDL_GPUSampler* CreateSampler(SDL_GPUSamplerAddressMode mode, Sampling sampling = Sampling::Nearest);

        SDL_GPUShader* LoadShader(const std::string& name, SDL_GPUShaderStage stage, uint samplers);

        // A pipeline, created the first time it's asked for.
        SDL_GPUGraphicsPipeline* Pipeline(const PipelineKey& key);

        SDL_GPUGraphicsPipeline* CreatePipeline(const PipelineKey& key);

        // The frame's streamed vertices go to their buffer.
        void QueueStream();

        // The canvas's quads become draws in screen space, after the scene.
        void QueueCanvas();

        // Copies the pending dynamic meshes into their buffers.
        void UploadDynamic(SDL_GPUCommandBuffer* commands);

        // What keeps the CPU at most a frame ahead of the GPU: the swapchain (a shown window),
        // or the last frame's fence (a hidden one: frames would pile up, and their memory).
        enum class Pacing { Swapchain, Fence };

        SDL_GPUFence* _lastFrame = nullptr;

        // Submits the frame; with GpuSync::Wait, returns once the GPU has drawn it,
        // hidden, once it has drawn the one before (the time waited).
        TimeSpan Submit(SDL_GPUCommandBuffer* commands, Pacing pacing);

        // Waits until the GPU passes a fence (none: at once) and releases it.
        void WaitFor(SDL_GPUFence* fence);

        RenderStats _stats;

        // The sky and the post pass draw one screen-filling triangle.
        static constexpr int ScreenTriangle = 3;
        static constexpr int TriangleVertices = 3;
        int _drawCalls = 0;
        int _triangles = 0;

        // Draws vertices of the bound buffer, counted in Stats.
        void DrawPrimitives(SDL_GPURenderPass* pass, int count, int first, Render::Primitive primitive);

        // Auto's frame: about a 1920 x 1080's pixels, at least half the window's sides.
        static constexpr float AutoPixels = 1920.0f * 1080.0f;
        static constexpr int MinAutoScale = 50;
        // The largest texture side every backend takes.
        static constexpr uint MaxTargetSide = 16384;

        uint Scaled(uint side, int percent) const;

        // The render scale (%) for a window of this size.
        int PercentFor(uint width, uint height) const { return RenderScale == AutoScale ? AutoPercent(width, height) : RenderScale; }

        // The present mode in use (SDL starts with VSync).
        Render::VSync _vsync = Render::VSync::On;

        static SDL_GPUPresentMode PresentMode(Render::VSync mode);

        static std::string FormatName(SDL_GPUTextureFormat format);

        void RenderScene(SDL_GPUCommandBuffer*& commands, const View& view, Vector4 clearColour);

        // The panorama behind everything: one screen-filling triangle.
        void DrawSky(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const View& view, const Render::Panorama& panorama);

        void BindPanorama(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const Render::Panorama& panorama, int texture,
            float rowShift, Vector3 camera, bool includeHdSky);

        // Draws the commands of one pass, in their order.
        void DrawPass(SDL_GPUCommandBuffer*& commands, SDL_GPURenderPass*& pass, const std::vector<DrawCommand>& source, Render::Pass order, const View& view);

        // The open pass's depth band (null: the whole range, as a pass begins).
        std::optional<DepthBand> _band;

        // Depth tested within band's range: the viewport's area kept (the
        // frame's, or an inset's).
        void UseBand(SDL_GPURenderPass* pass, DepthBand band);

        void DrawOne(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const DrawCommand& command, const View& view);

        void Blit(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* swapchain, uint width, uint height);

        // Downloads the frame and writes it as a 32-bit BMP.
        void Save(SDL_GPUCommandBuffer* commands, const std::string& path);

        void EnsureTargets(uint width, uint height);

        SDL_GPUTexture* CreateTexture(SDL_GPUTextureFormat format, uint width, uint height, SDL_GPUTextureUsageFlags usage,
            uint levels = 1, uint samples = 1);

        SDL_GPUTransferBuffer* CreateTransfer(uint size, SDL_GPUTransferBufferUsage usage);

        // Uploads up to this size go through one kept buffer, grown to the largest (cycled:
        // uploads still pending keep theirs); larger ones (a level's first frame) through a buffer of
        // their own, released by the caller when temporary.
        static constexpr int StagingKeep = 4 << 20;

        // An upload buffer of size bytes, mapped (the caller unmaps it).
        std::span<byte> MapStaging(int size, SDL_GPUTransferBuffer*& transfer, bool& temporary);

        SDL_GPUTransferBuffer* _staging = nullptr;
        uint _stagingSize = 0;

        void Upload(SDL_GPUTexture* texture, uint width, uint height, std::span<const byte> pixels);

        // Texture and mesh uploads share one command buffer and one upload buffer, submitted
        // when it's full and before each frame. A level's thousands of uploads, each submitted on its
        // own (a fence) through a cycled buffer (a copy of it), outran the GPU and ran out of memory
        // (Vulkan on NVIDIA: VK_ERROR_OUT_OF_HOST_MEMORY).
        //   Upload ─► [batch: copy, copy, …] ──full / frame──► submit, wait for the batch before
        static constexpr uint UploadBatchSize = 4 << 20;
        // Uploads start at multiples of this (D3D12's texture placement alignment).
        static constexpr uint UploadAlignment = 512;

        SDL_GPUTransferBuffer* _uploadBuffer = nullptr;
        SDL_GPUCommandBuffer* _uploadCommands = nullptr;
        SDL_GPUCopyPass* _uploadCopy = nullptr;
        uint _uploadUsed = 0;
        SDL_GPUFence* _uploadFence = nullptr;

        // The batch's copy pass, data copied to transfer
        // at offset; a larger upload gets a buffer of its own, freed once done.
        SDL_GPUCopyPass* UploadPass(std::span<const byte> data, SDL_GPUTransferBuffer*& transfer, uint& offset);

        // Submits the batch, then waits for the one before: at most two on the GPU.
        void FlushUploads();

        // ---- Renderer.Stereo.cs ----

        struct StereoUniforms
        {
            // x: 0 side by side, 1 interlaced, 2 top and bottom, 3 the VR viewer; y: 1 the eyes
            // exchanged (crossview, the reversed interlaced and top and bottom); z: an eye's aspect.
            Vector4 Mode;
        };

        // The eyes of a stereo frame, the frame's size (the same as the frame's targets).
        std::array<SDL_GPUTexture*, StereoEyes> _eyes{};

        // Where the scene draws: the frame's target, or the eye being rendered.
        SDL_GPUTexture* _frame = nullptr;

        // The eye being drawn (−1 the left, +1 the right, 0: no stereo; the insets).
        float _eyeSide = 0.0f;

        // The eye targets of the frame's size, made (or freed) when stereo is switched.
        void EnsureEyes();

        void ReleaseEyes();

        // Draws one eye's whole frame through its view into its target.
        void RenderEye(SDL_GPUCommandBuffer*& commands, const View& view, Vector4 clearColour, int eye, float side);

        // The two eyes into the frame: side by side or top and bottom (an eye a half;
        // crossview and the reversed top and bottom: exchanged), or interlaced (an eye the even rows;
        // reversed: the odd ones).
        void CompositeStereo(SDL_GPUCommandBuffer* commands);

        // ---- Renderer.Enhanced.cs ----

        // The enhanced shader's modes (shaders/enhanced.hlsl).
        enum class Mode { Lit = 1, Sprite = 2, Canvas = 3, Image = 4 };

        struct EnhancedVertexUniforms
        {
            Matrix4x4 Transform;
            Matrix4x4 World;
        };

        struct EnhancedUniforms
        {
            Vector4 Colour;
            int Textured = 0;
            int FrameCount = 0;
            int Frame = 0;
            Renderer::Mode Mode = Renderer::Mode::Lit;
            Matrix4x4 SunMatrix;
            Vector4 Sun;
            Vector4 SunColour;
            Vector4 Sky;
            Vector4 Ground;
            Vector4 Camera;
            Vector4 Haze;
            Vector4 Shadow;
        };

        using LightArray = std::array<Vector4, PointLights::Shown * LightVectors>;

        struct LightUniforms
        {
            // x: how many lights.
            Vector4 Count;
            LightArray Lights{};
        };

        struct PostUniforms
        {
            Matrix4x4 ClipToView;
            Matrix4x4 ViewToClip;
            Vector4 Screen;
            Vector4 Occlusion;
            Vector4 Glow;
        };

#if defined(__ANDROID__)
        EffectQuality _quality = EffectQuality::Mobile;
#else
        EffectQuality _quality = EffectQuality::Full;
#endif

        uint ShadowSide() const { return _quality == EffectQuality::Mobile ? MobileShadowSize : ShadowSize; }
        uint OcclusionStep() const { return _quality == EffectQuality::Mobile ? MobileOcclusion : FullOcclusion; }

        SDL_GPUTextureFormat _sampledDepthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        SDL_GPUSampler* _mipSampler = nullptr;
        // Samples per pixel of the scene's targets, and of the render pass being drawn.
        uint _samples = 1;
        uint _passSamples = 1;
        SDL_GPUTexture* _msaaColour = nullptr;
        SDL_GPUTexture* _msaaDepth = nullptr;
        SDL_GPUTexture* _scene = nullptr;
        SDL_GPUTexture* _viewDepth = nullptr;
        // The ambient occlusion before its blur (occlusion.hlsl).
        SDL_GPUTexture* _occlusion = nullptr;
        SDL_GPUTexture* _shadowMap = nullptr;
        // This frame's shadow map matrix, while there is one.
        std::optional<Matrix4x4> _sunMatrix;

        PointLights _lights;

        static uint SamplerCount(const std::string& program);

        // A shader's uniform buffers (its name: program.vs or program.ps).
        static uint UniformBuffers(const std::string& shader) { return shader == "enhanced.ps" ? EnhancedUniformBuffers : 1u; }

        // The point lights nearest the camera into the enhanced shader's light buffer (none
        // in the original look).
        void PushLights(SDL_GPUCommandBuffer* commands, const View& view);

        static SDL_GPUSampleCount SampleCount(uint samples);

        // The samples AntiAliasing asks for, fewer if the GPU can't.
        uint SupportedSamples();

        void CreateMultisampled(uint width, uint height);

        // Frees the targets of the frame's size (multisampled, the enhanced look's).
        void ReleaseSizedTargets();

        void ReleaseShadowMap();

        // The scene's colour target: texture, or the multisampled one
        // resolved into it. Its content is kept for Backdrop::Keep.
        SDL_GPUColorTargetInfo SceneColour(SDL_GPUTexture* texture, Vector4 clearColour) const;

        SDL_GPUDepthStencilTargetInfo SceneDepth() const;

        // The enhanced shader's mode for a draw, or null for the original shaders. Lines have
        // no plane to light; the canvas follows CanvasSampling.
        std::optional<Renderer::Mode> ModeOf(const DrawCommand& command) const;

        // Opaque triangles: in the camera's depth, and in the sun's when lit.
        static bool Opaque(const DrawCommand& command);

        static bool CastsShadow(const DrawCommand& command);

        void EnsureEnhanced();

        void RenderEnhanced(SDL_GPUCommandBuffer*& commands, const View& view, Vector4 clearColour, const Render::Lighting& lighting);

        // A depth pass: the sun's (the shadow casters) or the camera's (the opaque triangles).
        void RenderDepth(SDL_GPUCommandBuffer*& commands, SDL_GPUTexture* target, const Matrix4x4& viewProjection, Renderer::Output output);

        void DrawDepth(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const DrawCommand& command, const Matrix4x4& viewProjection, Renderer::Output output);

        // A draw through the enhanced shader (its pipeline and vertex buffer bound).
        void DrawEnhanced(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const DrawCommand& command, const Matrix4x4& transform, Renderer::Mode mode, const View& view);

        // Ambient occlusion (noisy, into its own target), then blurred with the glow from the
        // scene into the frame.
        void RenderPost(SDL_GPUCommandBuffer* commands, const View& view, const Render::Lighting& lighting);

        // A screen-filling triangle of program into target.
        void ScreenPass(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, const std::string& program,
            const SDL_GPUTextureSamplerBinding* samplers, uint count, const PostUniforms& uniforms);

        // ---- Renderer.Gamma.cs ----

        struct GammaUniforms
        {
            // x: 1 / gamma.
            Vector4 Inverse;
        };

        // The frame after the gamma, the frame's size; made when the gamma isn't 1.
        SDL_GPUTexture* _graded = nullptr;

        // The frame shown and saved: the target, or the graded one.
        SDL_GPUTexture* _shown = nullptr;

        // The frame through the gamma into _graded (at 1: the target as it is).
        void ApplyGamma(SDL_GPUCommandBuffer* commands);

        void ReleaseGraded();

        // ---- Renderer.Insets.cs ----

        struct Inset
        {
            Render::View View;
            RectangleF Area;
            InsetContent Content = InsetContent::Scene;
            InsetLayer Layer = InsetLayer::UnderCanvas;
            std::vector<DrawCommand> Commands;
        };

        // Owns every inset made (C#'s garbage collector); _insets and _spareInsets point into it.
        std::vector<std::unique_ptr<Inset>> _insetStore;
        std::vector<Inset*> _insets;
        // Insets of earlier frames, reused.
        std::stack<Inset*> _spareInsets;
        Inset* _open = nullptr;

        // Where draws go: the open inset's own list, or the scene's.
        std::vector<DrawCommand>& Queue() { return _open != nullptr ? _open->Commands : _commands; }

        void ClearInsets();

        void RenderInsets(SDL_GPUCommandBuffer*& commands);

        void RenderInsets(SDL_GPUCommandBuffer*& commands, InsetLayer layer);

        void RenderInset(SDL_GPUCommandBuffer*& commands, const Inset& inset);

        void RenderCanvas(SDL_GPUCommandBuffer*& commands);

        // A render pass over the frame drawn so far, its depth cleared.
        SDL_GPURenderPass* BeginOver(SDL_GPUCommandBuffer* commands);

        // ---- Renderer.SamplerHeap.cs ----

        // An open pass's targets (loaded) and area: where it goes on in a new command buffer.
        struct OpenPass
        {
            SDL_GPUColorTargetInfo Colour{};
            bool Coloured = false;
            SDL_GPUDepthStencilTargetInfo Depth{};
            SDL_GPUViewport Viewport{};
            SDL_Rect Scissor{};
            bool Clipped = false;
        };

        OpenPass _openPass;
        // The samplers the command buffer's draw loops may have bound (a draw's most each).
        int _samplersBound = 0;
        // The frame's point lights, pushed again into a new command buffer.
        LightUniforms _lightUniforms;
        // The frame's command buffer holding the swapchain texture (null: none): presents at its submit.
        SDL_GPUCommandBuffer* _presenting = nullptr;

        // Only Direct3D 12 has the bug.
        bool HeapBound() const { return _gpu.Backend == GpuBackend::Direct3D12; }

        // Kept for the next command buffer when a pass may go on there (Direct3D 12).
        SDL_GPUStoreOp DepthStore() const { return HeapBound() ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE; }

        // Begins a pass that Reserve can resume.
        SDL_GPURenderPass* BeginPass(SDL_GPUCommandBuffer* commands, const SDL_GPUColorTargetInfo* colour, const SDL_GPUDepthStencilTargetInfo* depth);

        // The open pass's viewport and scissor, set again when it's resumed.
        void ClipPass(SDL_GPURenderPass* pass, SDL_GPUViewport viewport, SDL_Rect scissor);

        // Room for a draw's samplers: when the heap would run out, the pass ends, the command
        // buffer is submitted and the pass goes on in a new one.
        void Reserve(SDL_GPUCommandBuffer*& commands, SDL_GPURenderPass*& pass);

        // ---- Renderer.Colours.cs ----

        struct ColourKey
        {
            int Texture = 0;
            int Palette = 0;
            int Frames = 0;

            auto operator<=>(const ColourKey&) const = default;
        };

        // A texture's size and pixels as last uploaded.
        struct Pixels
        {
            int Width = 0;
            int Height = 0;
            std::vector<byte> Data;
        };

        // C#'s (int Width, int Height) tuple.
        struct Size
        {
            int Width = 0;
            int Height = 0;

            bool operator==(const Size&) const = default;
        };

        std::unordered_map<int, Pixels> _indexPixels;
        std::unordered_map<int, std::vector<byte>> _palettePixels;
        std::map<ColourKey, SDL_GPUTexture*> _colours;
        // Colour textures given whole (HD textures) instead of expanded: premultiplied RGBA8,
        // frames stacked downwards.
        std::map<ColourKey, Pixels> _replaced;
        // The canvas's images given whole (ReplaceImage): kept through palette changes.
        std::set<ColourKey> _images;
        // This frame's missing colour textures (reused).
        std::set<ColourKey> _missing;
        std::vector<ColourKey> _stale;
        SDL_GPUSampler* _colourSampler = nullptr;
        // Bound where a draw has no colour texture (flat colours, the canvas).
        SDL_GPUTexture* _blankColours = nullptr;

        // The trilinear, anisotropic sampler and the blank array.
        void CreateColourSampler();

        // Whether a material samples a colour texture (ModeOf Lit or Sprite).
        static bool NeedsColours(const Render::Material& material);

        // Keeps what ColourMips needs of an index texture or a palette (a copy: C# kept the
        // caller's array, which C++ can't share without changing the API's ownership).
        void KeepIndices(int texture, int width, int height, std::span<const byte> indices);

        void KeepPalette(int palette, std::span<const byte> rgba);

        // New pixels for a texture: its colour textures are written again, in place.
        void Changed(int texture, int width, int height, std::span<const byte> pixels);

        // Whether a colour texture still fits its index texture (the size it was made for).
        bool SameSize(ColourKey key, SDL_GPUTexture* colours) const;

        Size SizeOf(ColourKey key) const;

        std::unordered_map<SDL_GPUTexture*, Size> _colourSizes;

        // Frees the colour textures in _stale.
        void ReleaseStale();

        // Forgets the textures from first on (a scope released).
        void ReleaseColoursFrom(int first);

        static ColourKey KeyOf(const Render::Material& material);

        // Whether the pixels to expand are known: the palette, and indices of whole frames.
        bool CanExpand(ColourKey key) const;

        // Makes the colour textures this frame's enhanced draws need and nobody prepared, in
        // one copy pass.
        void PrepareColours(SDL_GPUCommandBuffer* commands);

        void FindMissing(const std::vector<DrawCommand>& commands);

        // The colour textures of a copy pass: each key and its texture (null: a new one).
        std::vector<std::pair<ColourKey, SDL_GPUTexture*>> _batch;
        // A colour texture's frames and mips on the CPU, grown to the largest (reused).
        std::vector<byte> _chain;

        // Bytes of a colour texture: every frame's mip chain.
        int ColourSize(ColourKey key) const;

        // The _batch's colour textures (new ones made): each frame expanded and
        // its mips, all through one upload buffer, uploaded in copy.
        void UploadBatch(SDL_GPUCopyPass* copy);

        // The batch's textures [first, last) expanded into
        // _chain one after the other; several on every core (a bullet hole's one: no threads).
        void ExpandChunk(int first, int last);

        // A colour texture's frames through the palette, each with its mips.
        void Expand(ColourKey key, std::span<byte> target) const;

        // A given colour texture's frames, each with its mips.
        void Copy(ColourKey key, const Pixels& replaced, std::span<byte> target) const;

        // Uploads a colour texture's layers and mips from transfer at
        // offset; returns the offset after them.
        int UploadLayers(SDL_GPUCopyPass* copy, ColourKey key, SDL_GPUTexture* texture, SDL_GPUTransferBuffer* transfer, int offset);

        SDL_GPUTexture* CreateColours(ColourKey key);

        SDL_GPUTexture* CreateColours(int width, int height, int layers, int levels);

        // A draw's colour texture, or the blank one.
        SDL_GPUTexture* ColoursOf(const Render::Material& material) const;

        void DisposeColours();
    };
}
