using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using SDL;
using static SDL.SDL3;

namespace Mdk.Engine.Render;

/// <summary>How a surface is shaded.</summary>
public enum Shading
{
    /// <summary>The original: nearest texels, unlit.</summary>
    Original,
    /// <summary>The enhanced look: filtered texels, lit by the sun and the light all around, shadowed,
    /// hazy; casts shadows.</summary>
    Lit,
    /// <summary>The enhanced look's sprites: filtered texels, edges cut where half covered, hazy; no
    /// light and no shadow.</summary>
    Sprite,
}

/// <summary>How images are sampled: nearest pixels or filtered.</summary>
public enum Sampling { Nearest, Linear }

/// <summary>Multisample anti-aliasing of the frame.</summary>
public enum AntiAliasing { Off, X2, X4 }

/// <summary>Whether the sun casts shadows.</summary>
public enum Shadows { Off, On }

/// <summary>The enhanced look's light: the sun (the way its light goes, its linear colour), the
/// light from the sky above and from the ground below (linear colours, blended by a face's
/// up component), the exposure scaling it all, the sun's shadows and how far they reach, the glow,
/// and the haze (its colour, its density per unit).</summary>
/// <summary>How much the enhanced look's passes cost: Full (desktops), or Mobile (Android: a
/// 1024² shadow map, the ambient occlusion at half the frame's sides; the post pass blurs it up).</summary>
public enum EffectQuality { Full, Mobile }

public sealed record Lighting(Vector3 SunDirection, Vector3 Sun, Vector3 Sky, Vector3 Ground, float Exposure,
    Shadows Shadows, float ShadowDistance, float Glow, Vector4 HazeColour, float HazeDensity);

/// <summary>The enhanced look's frame (with <see cref="Renderer.Lighting"/>).
/// <code>
///   sun's depth ──► shadow map ───────┐
///   camera's depth (opaque) ──────────┼─────────────┐
///   scene: sky, lit surfaces (MSAA) ──┴─► scene ─mips─► post: occlusion, glow ─► frame ─► canvas, insets
/// </code></summary>
public sealed unsafe partial class Renderer
{
    private const uint ShadowSize = 2048;
    private const uint MobileShadowSize = 1024;
    /// <summary>The ambient occlusion's pixels per side of one of its texels, by quality.</summary>
    private const uint FullOcclusion = 1;
    private const uint MobileOcclusion = 2;

    private EffectQuality _quality = OperatingSystem.IsAndroid() ? EffectQuality.Mobile : EffectQuality.Full;

    /// <summary>The enhanced look's cost (Android's default: Mobile); a change makes the shadow map
    /// and the occlusion's target again.</summary>
    public EffectQuality Quality
    {
        get => _quality;
        set
        {
            if (value == _quality)
            {
                return;
            }

            _quality = value;
            ReleaseShadowMap();
            _shadowMap = null;
            if (_occlusion != null)
            {
                SDL_ReleaseGPUTexture(_device, _occlusion);
                _occlusion = null;
            }
        }
    }

    private uint ShadowSide => _quality == EffectQuality.Mobile ? MobileShadowSize : ShadowSize;
    private uint OcclusionStep => _quality == EffectQuality.Mobile ? MobileOcclusion : FullOcclusion;

    /// <summary>The occlusion target's side for a frame's: whole texels of <paramref name="step"/>
    /// pixels, the last one partly off the frame (1801 / 2 ─► 901).</summary>
    internal static uint OcclusionSide(uint side, uint step) => (side + step - 1) / step;
    /// <summary>Shadow acne: the sun's depth is pushed back by the surface's slope, the receiver's
    /// point moved off its surface, and compared a little nearer (units).</summary>
    private const float ShadowSlopeBias = 2f;
    private const float ShadowNormalOffset = 0.5f;
    private const float ShadowDepthBias = 0.5f;
    /// <summary>Ambient occlusion: the disc's radius (units) and how dark it gets.</summary>
    private const float OcclusionRadius = 12f;
    private const float OcclusionStrength = 1.5f;
    /// <summary>The glow: colours above the threshold shine; the mips blurred for it (the first of two).</summary>
    private const float GlowThreshold = 0.8f;
    private const float GlowIntensity = 0.8f;
    private const float GlowMip = 3f;
    private const uint SceneMips = 6;
    private const float AllMips = 1000f;
    private const int EnhancedSamplers = 4;
    private const int DepthSamplers = 1;
    private const int DefaultSamplers = 2;
    private const int PostSamplers = 3;
    /// <summary>The enhanced fragment shader's uniform buffers: the draw's, then the frame's point
    /// lights (pushed once per frame, kept by the command buffer for every pass).</summary>
    private const uint EnhancedUniformBuffers = 2;
    private const uint LightSlot = 1;
    /// <summary>Per point light: its position and radius, then its colour.</summary>
    private const int LightVectors = 2;

    /// <summary>The enhanced shader's modes (shaders/enhanced.hlsl).</summary>
    private enum Mode { Lit = 1, Sprite = 2, Canvas = 3, Image = 4 }

    [StructLayout(LayoutKind.Sequential)]
    private struct EnhancedVertexUniforms
    {
        public Matrix4x4 Transform;
        public Matrix4x4 World;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct EnhancedUniforms
    {
        public Vector4 Colour;
        public int Textured;
        public int FrameCount;
        public int Frame;
        public Mode Mode;
        public Matrix4x4 SunMatrix;
        public Vector4 Sun;
        public Vector4 SunColour;
        public Vector4 Sky;
        public Vector4 Ground;
        public Vector4 Camera;
        public Vector4 Haze;
        public Vector4 Shadow;
    }

    [InlineArray(PointLights.Shown * LightVectors)]
    private struct LightArray
    {
        private Vector4 _first;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct LightUniforms
    {
        /// <summary>x: how many lights.</summary>
        public Vector4 Count;
        public LightArray Lights;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct PostUniforms
    {
        public Matrix4x4 ClipToView;
        public Matrix4x4 ViewToClip;
        public Vector4 Screen;
        public Vector4 Occlusion;
        public Vector4 Glow;
    }

    private readonly SDL_GPUTextureFormat _sampledDepthFormat;
    private readonly SDL_GPUSampler* _mipSampler;
    /// <summary>Samples per pixel of the scene's targets, and of the render pass being drawn.</summary>
    private uint _samples = 1;
    private uint _passSamples = 1;
    private SDL_GPUTexture* _msaaColour;
    private SDL_GPUTexture* _msaaDepth;
    private SDL_GPUTexture* _scene;
    private SDL_GPUTexture* _viewDepth;
    /// <summary>The ambient occlusion before its blur (occlusion.hlsl).</summary>
    private SDL_GPUTexture* _occlusion;
    private SDL_GPUTexture* _shadowMap;
    /// <summary>This frame's shadow map matrix, while there is one.</summary>
    private Matrix4x4? _sunMatrix;

    /// <summary>The enhanced look's light for the scene; null: the original look.</summary>
    public Lighting? Lighting { get; set; }

    /// <summary>The frame's point lights (the enhanced look's): the game fills them each frame.</summary>
    public PointLights Lights { get; } = new();

    /// <summary>How the canvas samples its images.</summary>
    public Sampling CanvasSampling { get; set; }

    public AntiAliasing AntiAliasing { get; set; }

    private static uint SamplerCount(string program) => program switch
    {
        "enhanced" => EnhancedSamplers,
        "depth" => DepthSamplers,
        "post" => PostSamplers,
        "stereo" => StereoEyes,
        "gamma" => GammaSamplers,
        _ => DefaultSamplers,
    };

    /// <summary>A shader's uniform buffers (its name: program.vs or program.ps).</summary>
    private static uint UniformBuffers(string shader) => shader == "enhanced.ps" ? EnhancedUniformBuffers : 1u;

    /// <summary>The point lights nearest the camera into the enhanced shader's light buffer (none
    /// in the original look).</summary>
    private void PushLights(SDL_GPUCommandBuffer* commands, View view)
    {
        Span<PointLight> packed = stackalloc PointLight[PointLights.Shown];
        var count = Lighting == null ? 0 : Lights.Pack(view.Position, packed);
        var uniforms = new LightUniforms { Count = new Vector4(count, 0f, 0f, 0f) };
        for (var i = 0; i < count; i++)
        {
            var light = packed[i];
            uniforms.Lights[i * LightVectors] = new Vector4(light.Position, light.Radius);
            uniforms.Lights[i * LightVectors + 1] = new Vector4(light.Colour, 0f);
        }

        _lightUniforms = uniforms;
        SDL_PushGPUFragmentUniformData(commands, LightSlot, (IntPtr)(&uniforms), (uint)sizeof(LightUniforms));
    }

    private static SDL_GPUSampleCount SampleCount(uint samples) => samples switch
    {
        4 => SDL_GPUSampleCount.SDL_GPU_SAMPLECOUNT_4,
        2 => SDL_GPUSampleCount.SDL_GPU_SAMPLECOUNT_2,
        _ => SDL_GPUSampleCount.SDL_GPU_SAMPLECOUNT_1,
    };

    /// <summary>The samples <see cref="AntiAliasing"/> asks for, fewer if the GPU can't.</summary>
    private uint SupportedSamples()
    {
        var samples = AntiAliasing switch { AntiAliasing.X4 => 4u, AntiAliasing.X2 => 2u, _ => 1u };
        while (samples > 1 && !(SDL_GPUTextureSupportsSampleCount(_device, ColourFormat, SampleCount(samples))
            && SDL_GPUTextureSupportsSampleCount(_device, _depthFormat, SampleCount(samples))))
        {
            samples /= 2;
        }

        return samples;
    }

    private void CreateMultisampled(uint width, uint height)
    {
        if (_samples == 1)
        {
            return;
        }

        _msaaColour = CreateTexture(ColourFormat, width, height, SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, 1, _samples);
        _msaaDepth = CreateTexture(_depthFormat, width, height, SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, 1, _samples);
    }

    /// <summary>Frees the targets of the frame's size (multisampled, the enhanced look's).</summary>
    private void ReleaseSizedTargets()
    {
        foreach (var texture in new[] { _msaaColour, _msaaDepth, _scene, _viewDepth, _occlusion })
        {
            if (texture != null)
            {
                SDL_ReleaseGPUTexture(_device, texture);
            }
        }

        _msaaColour = _msaaDepth = _scene = _viewDepth = _occlusion = null;
        ReleaseEyes();
        ReleaseGraded();
    }

    private void ReleaseShadowMap()
    {
        if (_shadowMap != null)
        {
            SDL_ReleaseGPUTexture(_device, _shadowMap);
        }
    }

    /// <summary>The scene's colour target: <paramref name="texture"/>, or the multisampled one
    /// resolved into it. Its content is kept for <see cref="Backdrop.Keep"/>.</summary>
    private SDL_GPUColorTargetInfo SceneColour(SDL_GPUTexture* texture, Vector4 clearColour)
    {
        var info = new SDL_GPUColorTargetInfo
        {
            texture = texture,
            clear_color = new SDL_FColor { r = clearColour.X, g = clearColour.Y, b = clearColour.Z, a = clearColour.W },
            load_op = Backdrop == Backdrop.Keep ? SDL_GPULoadOp.SDL_GPU_LOADOP_LOAD : SDL_GPULoadOp.SDL_GPU_LOADOP_CLEAR,
            store_op = SDL_GPUStoreOp.SDL_GPU_STOREOP_STORE,
        };
        if (_msaaColour == null)
        {
            return info;
        }

        info.texture = _msaaColour;
        info.resolve_texture = texture;
        info.store_op = SDL_GPUStoreOp.SDL_GPU_STOREOP_RESOLVE_AND_STORE;
        return info;
    }

    private SDL_GPUDepthStencilTargetInfo SceneDepth() => new()
    {
        texture = _msaaDepth != null ? _msaaDepth : _depth,
        clear_depth = 1f,
        load_op = SDL_GPULoadOp.SDL_GPU_LOADOP_CLEAR,
        store_op = DepthStore,
        stencil_load_op = SDL_GPULoadOp.SDL_GPU_LOADOP_DONT_CARE,
        stencil_store_op = SDL_GPUStoreOp.SDL_GPU_STOREOP_DONT_CARE,
    };

    /// <summary>The enhanced shader's mode for a draw, or null for the original shaders. Lines have
    /// no plane to light; the canvas follows <see cref="CanvasSampling"/>.</summary>
    private Mode? ModeOf(in DrawCommand command)
    {
        var material = command.Material;
        if (material.Pass == Pass.Overlay)
        {
            // The original look's canvas keeps its index textures, replaced or not.
            if (CanvasSampling != Sampling.Linear)
            {
                return null;
            }

            return _images.Contains(KeyOf(material)) ? Mode.Image : Mode.Canvas;
        }

        if (material.Pass == Pass.Mirror)
        {
            return null;
        }

        return material.Shading switch
        {
            Shading.Lit when command.Primitive == Primitive.Triangles => Mode.Lit,
            Shading.Original => null,
            _ => Mode.Sprite,
        };
    }

    /// <summary>Opaque triangles: in the camera's depth, and in the sun's when lit.</summary>
    private static bool Opaque(in DrawCommand command) =>
        command.Primitive == Primitive.Triangles && command.Material.Pass is Pass.Solid or Pass.DoubleSided or Pass.Mirror;

    private static bool CastsShadow(in DrawCommand command) =>
        Opaque(command) && command.Material.Shading == Shading.Lit && command.Material.Pass != Pass.Mirror;

    private void EnsureEnhanced()
    {
        if (_shadowMap == null)
        {
            _shadowMap = CreateTexture(_sampledDepthFormat, ShadowSide, ShadowSide, SampledDepthUsage);
        }

        if (_occlusion == null)
        {
            _occlusion = CreateTexture(ColourFormat, OcclusionSide(_targetWidth, OcclusionStep), OcclusionSide(_targetHeight, OcclusionStep),
                SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_SAMPLER);
        }

        if (_scene != null)
        {
            return;
        }

        // Mips down to a pixel at most (a tiny window).
        var mips = Math.Min(SceneMips, (uint)Math.Log2(Math.Max(1u, Math.Min(_targetWidth, _targetHeight))) + 1);
        _scene = CreateTexture(ColourFormat, _targetWidth, _targetHeight,
            SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_SAMPLER, mips);
        _viewDepth = CreateTexture(_sampledDepthFormat, _targetWidth, _targetHeight, SampledDepthUsage);
    }

    private const SDL_GPUTextureUsageFlags SampledDepthUsage =
        SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPUTextureUsageFlags.SDL_GPU_TEXTUREUSAGE_SAMPLER;

    private void RenderEnhanced(ref SDL_GPUCommandBuffer* commands, View view, Vector4 clearColour, Lighting lighting)
    {
        EnsureEnhanced();
        if (lighting.Shadows == Shadows.On)
        {
            _sunMatrix = SunShadow.Matrix(lighting.SunDirection, view.Position, lighting.ShadowDistance, ShadowSide);
            RenderDepth(ref commands, _shadowMap, _sunMatrix.Value, Output.Shadow);
        }

        RenderDepth(ref commands, _viewDepth, view.ViewProjection, Output.Depth);

        // The scene without the canvas, into the scene texture.
        var colourTarget = SceneColour(_scene, clearColour);
        var depthTarget = SceneDepth();
        var pass = BeginPass(commands, &colourTarget, &depthTarget);
        _passSamples = _samples;
        if (Panorama != null && Backdrop == Backdrop.Sky)
        {
            DrawSky(commands, pass, view, Panorama);
        }

        foreach (var order in Passes)
        {
            if (order != Pass.Overlay)
            {
                DrawPass(ref commands, ref pass, _commands, order, view);
            }
        }

        SDL_EndGPURenderPass(pass);
        _passSamples = 1;

        SDL_GenerateMipmapsForGPUTexture(commands, _scene);
        RenderPost(commands, view, lighting);
        if (_insets.Count == 0)
        {
            RenderCanvas(ref commands);
        }
        else
        {
            RenderInsets(ref commands);
        }

        _sunMatrix = null;
    }

    /// <summary>A depth pass: the sun's (the shadow casters) or the camera's (the opaque triangles).</summary>
    private void RenderDepth(ref SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, Matrix4x4 viewProjection, Output output)
    {
        var depthTarget = new SDL_GPUDepthStencilTargetInfo
        {
            texture = target,
            clear_depth = 1f,
            load_op = SDL_GPULoadOp.SDL_GPU_LOADOP_CLEAR,
            store_op = SDL_GPUStoreOp.SDL_GPU_STOREOP_STORE,
            stencil_load_op = SDL_GPULoadOp.SDL_GPU_LOADOP_DONT_CARE,
            stencil_store_op = SDL_GPUStoreOp.SDL_GPU_STOREOP_DONT_CARE,
        };
        var pass = BeginPass(commands, null, &depthTarget);
        foreach (ref readonly var command in CollectionsMarshal.AsSpan(_commands))
        {
            if (output == Output.Shadow ? CastsShadow(command) : Opaque(command))
            {
                Reserve(ref commands, ref pass);

                // The sun's depth is the world's; the camera's covers as the scene does.
                if (output == Output.Depth)
                {
                    UseBand(pass, command.Band);
                }

                DrawDepth(commands, pass, command, viewProjection, output);
            }
        }

        SDL_EndGPURenderPass(pass);
    }

    private void DrawDepth(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, in DrawCommand command, Matrix4x4 viewProjection, Output output)
    {
        var material = command.Material;
        var cull = material.Pass == Pass.Solid ? Pass.Solid : Pass.DoubleSided;
        SDL_BindGPUGraphicsPipeline(pass, Pipeline(new PipelineKey("depth", cull, Primitive.Triangles, Geometry.Mesh, 1, output)));

        var binding = new SDL_GPUBufferBinding { buffer = (SDL_GPUBuffer*)_buffers[command.Mesh] };
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        var transform = command.World * viewProjection;
        SDL_PushGPUVertexUniformData(commands, 0, (IntPtr)(&transform), (uint)sizeof(Matrix4x4));

        // Mirrors and flat colours have no holes.
        var textured = material.Texture != Material.None && material.Pass != Pass.Mirror;
        var sampler = new SDL_GPUTextureSamplerBinding { texture = (SDL_GPUTexture*)_textures[textured ? material.Texture : 0], sampler = _repeatSampler };
        SDL_BindGPUFragmentSamplers(pass, 0, &sampler, 1);
        var uniforms = new FragmentUniforms
        {
            Textured = textured ? 1 : 0,
            FrameCount = material.FrameCount,
            Frame = command.Frame,
        };
        SDL_PushGPUFragmentUniformData(commands, 0, (IntPtr)(&uniforms), (uint)sizeof(FragmentUniforms));
        DrawPrimitives(pass, command.Count, command.First, command.Primitive);
    }

    /// <summary>A draw through the enhanced shader (its pipeline and vertex buffer bound).</summary>
    private void DrawEnhanced(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, in DrawCommand command, Matrix4x4 transform, Mode mode, View view)
    {
        var vertex = new EnhancedVertexUniforms { Transform = transform, World = command.World };
        SDL_PushGPUVertexUniformData(commands, 0, (IntPtr)(&vertex), (uint)sizeof(EnhancedVertexUniforms));

        // Without a texture or a shadow map, any texture is bound.
        var material = command.Material;
        var textured = material.Texture != Material.None;
        var shadowed = _sunMatrix != null && mode == Mode.Lit;
        var samplers = stackalloc SDL_GPUTextureSamplerBinding[EnhancedSamplers];
        samplers[0] = new SDL_GPUTextureSamplerBinding { texture = (SDL_GPUTexture*)_textures[textured ? material.Texture : 0], sampler = _clampSampler };
        samplers[1] = new SDL_GPUTextureSamplerBinding { texture = (SDL_GPUTexture*)_textures[textured ? material.Palette : 0], sampler = _clampSampler };
        samplers[2] = new SDL_GPUTextureSamplerBinding { texture = shadowed ? _shadowMap : (SDL_GPUTexture*)_textures[0], sampler = _clampSampler };
        // Canvas images aren't tiled: clamped, no anisotropy.
        samplers[3] = new SDL_GPUTextureSamplerBinding
        {
            texture = textured && mode != Mode.Canvas ? ColoursOf(material) : _blankColours,
            sampler = mode == Mode.Image ? _mipSampler : _colourSampler,
        };
        SDL_BindGPUFragmentSamplers(pass, 0, samplers, EnhancedSamplers);

        // Without lighting: unlit (white light all around), no haze.
        var lighting = Lighting;
        var sky = lighting?.Sky ?? Vector3.One;
        var ground = lighting?.Ground ?? Vector3.One;
        var uniforms = new EnhancedUniforms
        {
            Colour = material.Colour,
            Textured = textured ? 1 : 0,
            FrameCount = material.FrameCount,
            Frame = command.Frame,
            Mode = mode,
            SunMatrix = _sunMatrix ?? Matrix4x4.Identity,
            Sun = lighting == null ? Vector4.UnitW : new Vector4(Vector3.Normalize(lighting.SunDirection), lighting.Exposure),
            SunColour = new Vector4(lighting?.Sun ?? Vector3.Zero, 0f),
            Sky = new Vector4(sky, Tonemap.Knee),
            Ground = new Vector4(ground, 0f),
            Camera = new Vector4(view.Position, 0f),
            Haze = lighting == null ? Vector4.Zero : lighting.HazeColour with { W = lighting.HazeDensity },
            Shadow = new Vector4(ShadowSide, ShadowDepthBias / (2f * SunShadow.Reach), ShadowNormalOffset, shadowed ? 1f : 0f),
        };
        SDL_PushGPUFragmentUniformData(commands, 0, (IntPtr)(&uniforms), (uint)sizeof(EnhancedUniforms));
        DrawPrimitives(pass, command.Count, command.First, command.Primitive);
    }

    /// <summary>Ambient occlusion (noisy, into its own target), then blurred with the glow from the
    /// scene into the frame.</summary>
    private void RenderPost(SDL_GPUCommandBuffer* commands, View view, Lighting lighting)
    {
        Matrix4x4.Invert(view.ClipToDirection, out var viewToClip);
        var uniforms = new PostUniforms
        {
            ClipToView = view.ClipToDirection,
            ViewToClip = viewToClip,
            Screen = new Vector4(_targetWidth, _targetHeight, 1f / _targetWidth, 1f / _targetHeight),
            Occlusion = new Vector4(OcclusionRadius, OcclusionStrength, lighting.HazeDensity, OcclusionStep),
            Glow = new Vector4(lighting.Glow, GlowThreshold, GlowIntensity, GlowMip),
        };

        var samplers = stackalloc SDL_GPUTextureSamplerBinding[PostSamplers];
        samplers[0] = new SDL_GPUTextureSamplerBinding { texture = _scene, sampler = _mipSampler };
        samplers[1] = new SDL_GPUTextureSamplerBinding { texture = _viewDepth, sampler = _clampSampler };
        samplers[2] = new SDL_GPUTextureSamplerBinding { texture = _occlusion, sampler = _clampSampler };
        ScreenPass(commands, _occlusion, "occlusion", samplers, DefaultSamplers, uniforms);
        ScreenPass(commands, _frame, "post", samplers, PostSamplers, uniforms);
    }

    /// <summary>A screen-filling triangle of <paramref name="program"/> into <paramref name="target"/>.</summary>
    private void ScreenPass(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, string program,
        SDL_GPUTextureSamplerBinding* samplers, uint count, PostUniforms uniforms)
    {
        var colourTarget = new SDL_GPUColorTargetInfo
        {
            texture = target,
            load_op = SDL_GPULoadOp.SDL_GPU_LOADOP_DONT_CARE,
            store_op = SDL_GPUStoreOp.SDL_GPU_STOREOP_STORE,
        };
        var pass = SDL_BeginGPURenderPass(commands, &colourTarget, 1, null);
        SDL_BindGPUGraphicsPipeline(pass, Pipeline(new PipelineKey(program, Pass.DoubleSided, Primitive.Triangles, Geometry.Post, 1, Output.Colour)));
        SDL_BindGPUFragmentSamplers(pass, 0, samplers, count);
        SDL_PushGPUFragmentUniformData(commands, 0, (IntPtr)(&uniforms), (uint)sizeof(PostUniforms));
        DrawPrimitives(pass, ScreenTriangle, 0, Primitive.Triangles);
        SDL_EndGPURenderPass(pass);
    }
}
