#include "Mdk.Engine/Render/Renderer.h"

#include "Mdk.Engine/Render/SunShadow.h"
#include "Mdk.Engine/Render/Tonemap.h"

namespace Mdk::Engine::Render
{
    // The enhanced look's frame (with Renderer::Lighting).
    //   sun's depth ──► shadow map ───────┐
    //   camera's depth (opaque) ──────────┼─────────────┐
    //   scene: sky, lit surfaces (MSAA) ──┴─► scene ─mips─► post: occlusion, glow ─► frame ─► canvas, insets

    void Renderer::SetQuality(EffectQuality value)
    {
        if (value == _quality)
        {
            return;
        }

        _quality = value;
        ReleaseShadowMap();
        _shadowMap = nullptr;
        if (_occlusion != nullptr)
        {
            SDL_ReleaseGPUTexture(_device, _occlusion);
            _occlusion = nullptr;
        }
    }

    uint Renderer::SamplerCount(const std::string& program)
    {
        if (program == "enhanced")
        {
            return EnhancedSamplers;
        }

        if (program == "depth")
        {
            return DepthSamplers;
        }

        if (program == "post")
        {
            return PostSamplers;
        }

        if (program == "stereo")
        {
            return StereoEyes;
        }

        if (program == "gamma")
        {
            return GammaSamplers;
        }

        return DefaultSamplers;
    }

    void Renderer::PushLights(SDL_GPUCommandBuffer* commands, const View& view)
    {
        std::array<PointLight, PointLights::Shown> packed{};
        const auto count = !Lighting.has_value() ? 0 : _lights.Pack(view.Position, packed);
        LightUniforms uniforms{};
        uniforms.Count = Vector4(static_cast<float>(count), 0.0f, 0.0f, 0.0f);
        for (int i = 0; i < count; i++)
        {
            const auto& light = packed[i];
            uniforms.Lights[i * LightVectors] = Vector4(light.Position, light.Radius);
            uniforms.Lights[i * LightVectors + 1] = Vector4(light.Colour, 0.0f);
        }

        _lightUniforms = uniforms;
        SDL_PushGPUFragmentUniformData(commands, LightSlot, &uniforms, sizeof(LightUniforms));
    }

    SDL_GPUSampleCount Renderer::SampleCount(uint samples)
    {
        switch (samples)
        {
        case 4:
            return SDL_GPU_SAMPLECOUNT_4;
        case 2:
            return SDL_GPU_SAMPLECOUNT_2;
        default:
            return SDL_GPU_SAMPLECOUNT_1;
        }
    }

    uint Renderer::SupportedSamples()
    {
        auto samples = AntiAliasing == Render::AntiAliasing::X4 ? 4u : AntiAliasing == Render::AntiAliasing::X2 ? 2u : 1u;
        while (samples > 1 && !(SDL_GPUTextureSupportsSampleCount(_device, ColourFormat, SampleCount(samples))
            && SDL_GPUTextureSupportsSampleCount(_device, _depthFormat, SampleCount(samples))))
        {
            samples /= 2;
        }

        return samples;
    }

    void Renderer::CreateMultisampled(uint width, uint height)
    {
        if (_samples == 1)
        {
            return;
        }

        _msaaColour = CreateTexture(ColourFormat, width, height, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, 1, _samples);
        _msaaDepth = CreateTexture(_depthFormat, width, height, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, 1, _samples);
    }

    void Renderer::ReleaseSizedTargets()
    {
        for (auto* texture : {_msaaColour, _msaaDepth, _scene, _viewDepth, _occlusion})
        {
            if (texture != nullptr)
            {
                SDL_ReleaseGPUTexture(_device, texture);
            }
        }

        _msaaColour = _msaaDepth = _scene = _viewDepth = _occlusion = nullptr;
        ReleaseEyes();
        ReleaseGraded();
    }

    void Renderer::ReleaseShadowMap()
    {
        if (_shadowMap != nullptr)
        {
            SDL_ReleaseGPUTexture(_device, _shadowMap);
        }
    }

    SDL_GPUColorTargetInfo Renderer::SceneColour(SDL_GPUTexture* texture, Vector4 clearColour) const
    {
        SDL_GPUColorTargetInfo info{};
        info.texture = texture;
        info.clear_color = SDL_FColor{clearColour.X, clearColour.Y, clearColour.Z, clearColour.W};
        info.load_op = Backdrop == Render::Backdrop::Keep ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        info.store_op = SDL_GPU_STOREOP_STORE;
        if (_msaaColour == nullptr)
        {
            return info;
        }

        info.texture = _msaaColour;
        info.resolve_texture = texture;
        info.store_op = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
        return info;
    }

    SDL_GPUDepthStencilTargetInfo Renderer::SceneDepth() const
    {
        SDL_GPUDepthStencilTargetInfo info{};
        info.texture = _msaaDepth != nullptr ? _msaaDepth : _depth;
        info.clear_depth = 1.0f;
        info.load_op = SDL_GPU_LOADOP_CLEAR;
        info.store_op = DepthStore();
        info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        return info;
    }

    std::optional<Renderer::Mode> Renderer::ModeOf(const DrawCommand& command) const
    {
        const auto& material = command.Material;
        if (material.Pass == Render::Pass::Overlay)
        {
            // The original look's canvas keeps its index textures, replaced or not.
            if (CanvasSampling != Sampling::Linear)
            {
                return std::nullopt;
            }

            return _images.contains(KeyOf(material)) ? Mode::Image : Mode::Canvas;
        }

        if (material.Pass == Render::Pass::Mirror)
        {
            return std::nullopt;
        }

        if (material.Shading == Shading::Lit && command.Primitive == Render::Primitive::Triangles)
        {
            return Mode::Lit;
        }

        if (material.Shading == Shading::Original)
        {
            return std::nullopt;
        }

        return Mode::Sprite;
    }

    bool Renderer::Opaque(const DrawCommand& command)
    {
        const auto pass = command.Material.Pass;
        return command.Primitive == Render::Primitive::Triangles
            && (pass == Render::Pass::Solid || pass == Render::Pass::DoubleSided || pass == Render::Pass::Mirror);
    }

    bool Renderer::CastsShadow(const DrawCommand& command)
    {
        return Opaque(command) && command.Material.Shading == Shading::Lit && command.Material.Pass != Render::Pass::Mirror;
    }

    void Renderer::EnsureEnhanced()
    {
        if (_shadowMap == nullptr)
        {
            _shadowMap = CreateTexture(_sampledDepthFormat, ShadowSide(), ShadowSide(), SampledDepthUsage);
        }

        if (_occlusion == nullptr)
        {
            _occlusion = CreateTexture(ColourFormat, OcclusionSide(_targetWidth, OcclusionStep()), OcclusionSide(_targetHeight, OcclusionStep()),
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        }

        if (_scene != nullptr)
        {
            return;
        }

        // Mips down to a pixel at most (a tiny window).
        const auto mips = Math::Min(SceneMips, static_cast<uint>(Math::Log2(static_cast<double>(Math::Max(1u, Math::Min(_targetWidth, _targetHeight))))) + 1);
        _scene = CreateTexture(ColourFormat, _targetWidth, _targetHeight,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, mips);
        _viewDepth = CreateTexture(_sampledDepthFormat, _targetWidth, _targetHeight, SampledDepthUsage);
    }

    void Renderer::RenderEnhanced(SDL_GPUCommandBuffer*& commands, const View& view, Vector4 clearColour, const Render::Lighting& lighting)
    {
        EnsureEnhanced();
        if (lighting.Shadows == Shadows::On)
        {
            _sunMatrix = SunShadow::Matrix(lighting.SunDirection, view.Position, lighting.ShadowDistance, ShadowSide());
            RenderDepth(commands, _shadowMap, *_sunMatrix, Output::Shadow);
        }

        RenderDepth(commands, _viewDepth, view.ViewProjection, Output::Depth);

        // The scene without the canvas, into the scene texture.
        auto colourTarget = SceneColour(_scene, clearColour);
        auto depthTarget = SceneDepth();
        auto* pass = BeginPass(commands, &colourTarget, &depthTarget);
        _passSamples = _samples;
        if (Panorama.has_value() && Backdrop == Render::Backdrop::Sky)
        {
            DrawSky(commands, pass, view, *Panorama);
        }

        for (const auto order : Passes)
        {
            if (order != Render::Pass::Overlay)
            {
                DrawPass(commands, pass, _commands, order, view);
            }
        }

        SDL_EndGPURenderPass(pass);
        _passSamples = 1;

        SDL_GenerateMipmapsForGPUTexture(commands, _scene);
        RenderPost(commands, view, lighting);
        if (_insets.empty())
        {
            RenderCanvas(commands);
        }
        else
        {
            RenderInsets(commands);
        }

        _sunMatrix.reset();
    }

    void Renderer::RenderDepth(SDL_GPUCommandBuffer*& commands, SDL_GPUTexture* target, const Matrix4x4& viewProjection, Renderer::Output output)
    {
        SDL_GPUDepthStencilTargetInfo depthTarget{};
        depthTarget.texture = target;
        depthTarget.clear_depth = 1.0f;
        depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        depthTarget.store_op = SDL_GPU_STOREOP_STORE;
        depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto* pass = BeginPass(commands, nullptr, &depthTarget);
        for (const auto& command : _commands)
        {
            if (output == Output::Shadow ? CastsShadow(command) : Opaque(command))
            {
                Reserve(commands, pass);

                // The sun's depth is the world's; the camera's covers as the scene does.
                if (output == Output::Depth)
                {
                    UseBand(pass, command.Band);
                }

                DrawDepth(commands, pass, command, viewProjection, output);
            }
        }

        SDL_EndGPURenderPass(pass);
    }

    void Renderer::DrawDepth(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const DrawCommand& command, const Matrix4x4& viewProjection, Renderer::Output output)
    {
        const auto& material = command.Material;
        const auto cull = material.Pass == Render::Pass::Solid ? Render::Pass::Solid : Render::Pass::DoubleSided;
        SDL_BindGPUGraphicsPipeline(pass, Pipeline(PipelineKey{"depth", cull, Render::Primitive::Triangles, Geometry::Mesh, 1, output}));

        SDL_GPUBufferBinding binding{};
        binding.buffer = _buffers[command.Mesh];
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        const auto transform = command.World * viewProjection;
        SDL_PushGPUVertexUniformData(commands, 0, &transform, sizeof(Matrix4x4));

        // Mirrors and flat colours have no holes.
        const auto textured = material.Texture != Material::None && material.Pass != Render::Pass::Mirror;
        const SDL_GPUTextureSamplerBinding sampler{_textures[textured ? material.Texture : 0], _repeatSampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &sampler, 1);
        FragmentUniforms uniforms{};
        uniforms.Textured = textured ? 1 : 0;
        uniforms.FrameCount = material.FrameCount;
        uniforms.Frame = command.Frame;
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(FragmentUniforms));
        DrawPrimitives(pass, command.Count, command.First, command.Primitive);
    }

    void Renderer::DrawEnhanced(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const DrawCommand& command, const Matrix4x4& transform, Renderer::Mode mode, const View& view)
    {
        const EnhancedVertexUniforms vertex{transform, command.World};
        SDL_PushGPUVertexUniformData(commands, 0, &vertex, sizeof(EnhancedVertexUniforms));

        // Without a texture or a shadow map, any texture is bound.
        const auto& material = command.Material;
        const auto textured = material.Texture != Material::None;
        const auto shadowed = _sunMatrix.has_value() && mode == Mode::Lit;
        std::array<SDL_GPUTextureSamplerBinding, EnhancedSamplers> samplers{};
        samplers[0] = {_textures[textured ? material.Texture : 0], _clampSampler};
        samplers[1] = {_textures[textured ? material.Palette : 0], _clampSampler};
        samplers[2] = {shadowed ? _shadowMap : _textures[0], _clampSampler};
        // Canvas images aren't tiled: clamped, no anisotropy.
        samplers[3] = {textured && mode != Mode::Canvas ? ColoursOf(material) : _blankColours,
            mode == Mode::Image ? _mipSampler : _colourSampler};
        SDL_BindGPUFragmentSamplers(pass, 0, samplers.data(), EnhancedSamplers);

        // Without lighting: unlit (white light all around), no haze.
        const auto& lighting = Lighting;
        const auto sky = lighting.has_value() ? lighting->Sky : Vector3::One();
        const auto ground = lighting.has_value() ? lighting->Ground : Vector3::One();
        EnhancedUniforms uniforms{};
        uniforms.Colour = material.Colour;
        uniforms.Textured = textured ? 1 : 0;
        uniforms.FrameCount = material.FrameCount;
        uniforms.Frame = command.Frame;
        uniforms.Mode = mode;
        uniforms.SunMatrix = _sunMatrix.value_or(Matrix4x4::Identity());
        uniforms.Sun = !lighting.has_value() ? Vector4::UnitW() : Vector4(Vector3::Normalize(lighting->SunDirection), lighting->Exposure);
        uniforms.SunColour = Vector4(lighting.has_value() ? lighting->Sun : Vector3::Zero(), 0.0f);
        uniforms.Sky = Vector4(sky, Tonemap::Knee);
        uniforms.Ground = Vector4(ground, 0.0f);
        uniforms.Camera = Vector4(view.Position, 0.0f);
        uniforms.Haze = !lighting.has_value() ? Vector4::Zero() : Vector4(lighting->HazeColour.XYZ(), lighting->HazeDensity);
        uniforms.Shadow = Vector4(static_cast<float>(ShadowSide()), ShadowDepthBias / (2.0f * SunShadow::Reach), ShadowNormalOffset, shadowed ? 1.0f : 0.0f);
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(EnhancedUniforms));
        DrawPrimitives(pass, command.Count, command.First, command.Primitive);
    }

    void Renderer::RenderPost(SDL_GPUCommandBuffer* commands, const View& view, const Render::Lighting& lighting)
    {
        Matrix4x4 viewToClip;
        Matrix4x4::Invert(view.ClipToDirection, viewToClip);
        PostUniforms uniforms{};
        uniforms.ClipToView = view.ClipToDirection;
        uniforms.ViewToClip = viewToClip;
        uniforms.Screen = Vector4(static_cast<float>(_targetWidth), static_cast<float>(_targetHeight), 1.0f / static_cast<float>(_targetWidth), 1.0f / static_cast<float>(_targetHeight));
        uniforms.Occlusion = Vector4(OcclusionRadius, OcclusionStrength, lighting.HazeDensity, static_cast<float>(OcclusionStep()));
        uniforms.Glow = Vector4(lighting.Glow, GlowThreshold, GlowIntensity, GlowMip);

        std::array<SDL_GPUTextureSamplerBinding, PostSamplers> samplers{};
        samplers[0] = {_scene, _mipSampler};
        samplers[1] = {_viewDepth, _clampSampler};
        samplers[2] = {_occlusion, _clampSampler};
        ScreenPass(commands, _occlusion, "occlusion", samplers.data(), DefaultSamplers, uniforms);
        ScreenPass(commands, _frame, "post", samplers.data(), PostSamplers, uniforms);
    }

    void Renderer::ScreenPass(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, const std::string& program,
        const SDL_GPUTextureSamplerBinding* samplers, uint count, const PostUniforms& uniforms)
    {
        SDL_GPUColorTargetInfo colourTarget{};
        colourTarget.texture = target;
        colourTarget.load_op = SDL_GPU_LOADOP_DONT_CARE;
        colourTarget.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &colourTarget, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(pass, Pipeline(PipelineKey{program, Render::Pass::DoubleSided, Render::Primitive::Triangles, Geometry::Post, 1, Output::Colour}));
        SDL_BindGPUFragmentSamplers(pass, 0, samplers, count);
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(PostUniforms));
        DrawPrimitives(pass, ScreenTriangle, 0, Render::Primitive::Triangles);
        SDL_EndGPURenderPass(pass);
    }
}
