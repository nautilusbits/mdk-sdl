#include "Mdk.Engine/Render/Renderer.h"

#include "Mdk.Core/DotNet.h"

namespace Mdk::Engine::Render
{
    bool StereoModes::TryParse(const std::string& name, Stereo& stereo)
    {
        const auto key = Str::Replace(Str::Replace(Str::ToLower(Str::Trim(name)), "_", ""), " ", "");
        if (key == "off")
        {
            stereo = Stereo::Off;
            return true;
        }

        if (key == "sbs" || key == "sidebyside")
        {
            stereo = Stereo::Sbs;
            return true;
        }

        if (key == "sbsfull" || key == "sidebysidefull")
        {
            stereo = Stereo::SbsFull;
            return true;
        }

        if (key == "cross" || key == "crossview")
        {
            stereo = Stereo::CrossView;
            return true;
        }

        if (key == "int" || key == "interlaced")
        {
            stereo = Stereo::Interlaced;
            return true;
        }

        if (key == "intr" || key == "interlacedreversed" || key == "interlacedreverse")
        {
            stereo = Stereo::InterlacedReversed;
            return true;
        }

        if (key == "tab" || key == "topandbottom" || key == "overunder")
        {
            stereo = Stereo::Tab;
            return true;
        }

        if (key == "tabr" || key == "tabreversed" || key == "topandbottomreversed" || key == "topandbottomreverse" || key == "overunderreversed")
        {
            stereo = Stereo::TabReversed;
            return true;
        }

        if (key == "vr" || key == "vrviewer" || key == "vrviewerphone")
        {
            stereo = Stereo::Vr;
            return true;
        }

        stereo = Stereo::Off;
        return false;
    }

    void Renderer::EnsureEyes()
    {
        if ((StereoMode != Stereo::Off) == (_eyes[0] != nullptr))
        {
            return;
        }

        ReleaseEyes();
        if (StereoMode == Stereo::Off)
        {
            return;
        }

        for (auto& eye : _eyes)
        {
            eye = CreateTexture(ColourFormat, _targetWidth, _targetHeight,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        }
    }

    void Renderer::ReleaseEyes()
    {
        for (auto& eye : _eyes)
        {
            if (eye != nullptr)
            {
                SDL_ReleaseGPUTexture(_device, eye);
                eye = nullptr;
            }
        }
    }

    void Renderer::RenderEye(SDL_GPUCommandBuffer*& commands, const View& view, Vector4 clearColour, int eye, float side)
    {
        _frame = _eyes[eye];
        _eyeSide = side;
        RenderScene(commands, view.Eye(side, StereoSeparation, StereoConvergence), clearColour);
    }

    void Renderer::CompositeStereo(SDL_GPUCommandBuffer* commands)
    {
        auto layout = 0.0f;
        switch (StereoMode)
        {
        case Stereo::Interlaced:
        case Stereo::InterlacedReversed:
            layout = 1.0f;
            break;
        case Stereo::Tab:
        case Stereo::TabReversed:
            layout = 2.0f;
            break;
        case Stereo::Vr:
            layout = 3.0f;
            break;
        default:
            break;
        }

        const auto swapped = StereoMode == Stereo::CrossView || StereoMode == Stereo::InterlacedReversed || StereoMode == Stereo::TabReversed;
        const StereoUniforms uniforms{Vector4(layout, swapped ? 1.0f : 0.0f, AspectRatio(), 0.0f)};

        std::array<SDL_GPUTextureSamplerBinding, StereoEyes> samplers{};
        samplers[0] = {_eyes[0], _mipSampler};
        samplers[1] = {_eyes[1], _mipSampler};

        SDL_GPUColorTargetInfo colourTarget{};
        colourTarget.texture = _target;
        colourTarget.load_op = SDL_GPU_LOADOP_DONT_CARE;
        colourTarget.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &colourTarget, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(pass, Pipeline(PipelineKey{"stereo", Render::Pass::DoubleSided, Render::Primitive::Triangles, Geometry::Post, 1, Output::Colour}));
        SDL_BindGPUFragmentSamplers(pass, 0, samplers.data(), StereoEyes);
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(StereoUniforms));
        DrawPrimitives(pass, ScreenTriangle, 0, Render::Primitive::Triangles);
        SDL_EndGPURenderPass(pass);
    }
}
