#include "Mdk.Engine/Render/Renderer.h"

namespace Mdk::Engine::Render
{
    // SDL 3.5's Direct3D 12 driver copies a draw's samplers into its command buffer's sampler
    // heap (2048) and checks for room only before the copy, not for the whole draw: 4 samplers at 2046
    // run past the heap's end (D3D12 error #646, the debug layer's exception 0x87D; without the layer,
    // memory overwritten). A command buffer is kept within one heap: a long pass goes on in the next.
    //   draws ─► 2048 - reserve reached ─► end pass, submit ─► new command buffer ─► pass resumed (loaded)

    SDL_GPURenderPass* Renderer::BeginPass(SDL_GPUCommandBuffer* commands, const SDL_GPUColorTargetInfo* colour, const SDL_GPUDepthStencilTargetInfo* depth)
    {
        _openPass = OpenPass{};
        _openPass.Coloured = colour != nullptr;
        _openPass.Depth = *depth;
        _openPass.Depth.load_op = SDL_GPU_LOADOP_LOAD;
        if (colour != nullptr)
        {
            _openPass.Colour = *colour;
            _openPass.Colour.load_op = SDL_GPU_LOADOP_LOAD;
        }

        _band.reset();
        return SDL_BeginGPURenderPass(commands, colour, colour == nullptr ? 0u : 1u, depth);
    }

    void Renderer::ClipPass(SDL_GPURenderPass* pass, SDL_GPUViewport viewport, SDL_Rect scissor)
    {
        SDL_SetGPUViewport(pass, &viewport);
        SDL_SetGPUScissor(pass, &scissor);
        _openPass.Viewport = viewport;
        _openPass.Scissor = scissor;
        _openPass.Clipped = true;
    }

    void Renderer::Reserve(SDL_GPUCommandBuffer*& commands, SDL_GPURenderPass*& pass)
    {
        if (!HeapBound())
        {
            return;
        }

        _samplersBound += DrawSamplers;
        if (_samplersBound + OtherSamplers <= SamplerHeap)
        {
            return;
        }

        // Submitting the swapchain's command buffer presents it: the frame shown unfinished.
        if (commands == _presenting)
        {
            throw InvalidOperationException("The swapchain's command buffer submitted before the frame's end");
        }

        SDL_EndGPURenderPass(pass);
        SDL_SubmitGPUCommandBuffer(commands);
        commands = SDL_AcquireGPUCommandBuffer(_device);
        _samplersBound = DrawSamplers;

        // Uniforms belong to a command buffer: the lights again (the draws push the rest).
        SDL_PushGPUFragmentUniformData(commands, LightSlot, &_lightUniforms, sizeof(LightUniforms));

        auto colour = _openPass.Colour;
        auto depth = _openPass.Depth;
        pass = SDL_BeginGPURenderPass(commands, _openPass.Coloured ? &colour : nullptr, _openPass.Coloured ? 1u : 0u, &depth);
        _band.reset();
        if (!_openPass.Clipped)
        {
            return;
        }

        auto viewport = _openPass.Viewport;
        auto scissor = _openPass.Scissor;
        SDL_SetGPUViewport(pass, &viewport);
        SDL_SetGPUScissor(pass, &scissor);
    }
}
