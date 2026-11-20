#include "Mdk.Engine/Render/Renderer.h"

namespace Mdk::Engine::Render
{
    // Insets: 3D views in rectangles of the canvas (sniper mode's round cameras and clip).
    //   scene ──► insets under the canvas ──► canvas ──► insets over the canvas
    //             (depth cleared, scissored)              (depth cleared, scissored)

    void Renderer::BeginInset(const View& view, RectangleF area, InsetContent content, InsetLayer layer)
    {
        if (!_spareInsets.empty())
        {
            _open = _spareInsets.top();
            _spareInsets.pop();
        }
        else
        {
            _open = _insetStore.emplace_back(std::make_unique<Inset>()).get();
        }

        _open->View = view;
        _open->Area = area;
        _open->Content = content;
        _open->Layer = layer;
        _open->Commands.clear();
        _insets.push_back(_open);
    }

    void Renderer::ClearInsets()
    {
        for (auto* inset : _insets)
        {
            _spareInsets.push(inset);
        }

        _insets.clear();
        _open = nullptr;
    }

    void Renderer::RenderInsets(SDL_GPUCommandBuffer*& commands)
    {
        RenderInsets(commands, InsetLayer::UnderCanvas);
        RenderCanvas(commands);
        RenderInsets(commands, InsetLayer::OverCanvas);
    }

    void Renderer::RenderInsets(SDL_GPUCommandBuffer*& commands, InsetLayer layer)
    {
        for (const auto* inset : _insets)
        {
            if (inset->Layer == layer)
            {
                RenderInset(commands, *inset);
            }
        }
    }

    void Renderer::RenderInset(SDL_GPUCommandBuffer*& commands, const Inset& inset)
    {
        auto* pass = BeginOver(commands);
        const auto scale = static_cast<float>(_targetHeight) / CanvasHeight;
        SDL_GPUViewport viewport{};
        viewport.x = inset.Area.X * scale;
        viewport.y = inset.Area.Y * scale;
        viewport.w = inset.Area.Width * scale;
        viewport.h = inset.Area.Height * scale;
        viewport.min_depth = 0.0f;
        viewport.max_depth = 1.0f;
        SDL_Rect scissor{};
        scissor.x = ToInt(viewport.x);
        scissor.y = ToInt(viewport.y);
        scissor.w = ToInt(MathF::Ceiling(viewport.w));
        scissor.h = ToInt(MathF::Ceiling(viewport.h));
        ClipPass(pass, viewport, scissor);

        const auto scene = inset.Content == InsetContent::Scene;
        const auto view = _eyeSide == 0.0f ? inset.View : inset.View.Eye(_eyeSide, StereoSeparation, StereoConvergence);
        if (scene && Panorama.has_value())
        {
            DrawSky(commands, pass, view, *Panorama);
        }

        const auto& source = scene ? _commands : inset.Commands;
        for (const auto order : Passes)
        {
            if (order != Render::Pass::Overlay)
            {
                DrawPass(commands, pass, source, order, view);
            }
        }

        SDL_EndGPURenderPass(pass);
    }

    void Renderer::RenderCanvas(SDL_GPUCommandBuffer*& commands)
    {
        auto* pass = BeginOver(commands);
        DrawPass(commands, pass, _commands, Render::Pass::Overlay, View{});

        SDL_EndGPURenderPass(pass);
    }

    SDL_GPURenderPass* Renderer::BeginOver(SDL_GPUCommandBuffer* commands)
    {
        SDL_GPUColorTargetInfo colourTarget{};
        colourTarget.texture = _frame;
        colourTarget.load_op = SDL_GPU_LOADOP_LOAD;
        colourTarget.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depthTarget{};
        depthTarget.texture = _depth;
        depthTarget.clear_depth = 1.0f;
        depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        depthTarget.store_op = DepthStore();
        depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        return BeginPass(commands, &colourTarget, &depthTarget);
    }
}
