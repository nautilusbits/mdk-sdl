#include "engine/render/Renderer.h"

namespace Mdk::Engine::Render
{
    // The display's gamma: the finished frame (scene, HUD, menus; both eyes) brightened or
    // darkened before it's shown or saved. At 1 nothing is drawn: the frame shows as it is.
    //   frame ─► "gamma" ─► graded frame ─► swapchain, or the screenshot

    void Renderer::ApplyGamma(SDL_GPUCommandBuffer* commands)
    {
        _shown = _target;
        if (Gamma == DefaultGamma)
        {
            return;
        }

        if (_graded == nullptr)
        {
            _graded = CreateTexture(ColourFormat, _targetWidth, _targetHeight,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        }

        const GammaUniforms uniforms{Vector4(1.0f / Gamma, 0.0f, 0.0f, 0.0f)};
        const SDL_GPUTextureSamplerBinding sampler{_target, _mipSampler};
        SDL_GPUColorTargetInfo colourTarget{};
        colourTarget.texture = _graded;
        colourTarget.load_op = SDL_GPU_LOADOP_DONT_CARE;
        colourTarget.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &colourTarget, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(pass, Pipeline(PipelineKey{"gamma", Render::Pass::DoubleSided, Render::Primitive::Triangles, Geometry::Post, 1, Output::Colour}));
        SDL_BindGPUFragmentSamplers(pass, 0, &sampler, GammaSamplers);
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(GammaUniforms));
        DrawPrimitives(pass, ScreenTriangle, 0, Render::Primitive::Triangles);
        SDL_EndGPURenderPass(pass);
        _shown = _graded;
    }

    void Renderer::ReleaseGraded()
    {
        if (_graded == nullptr)
        {
            return;
        }

        SDL_ReleaseGPUTexture(_device, _graded);
        _graded = nullptr;
    }
}
