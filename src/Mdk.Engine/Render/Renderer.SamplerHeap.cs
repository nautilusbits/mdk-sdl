using SDL;
using static SDL.SDL3;

namespace Mdk.Engine.Render;

/// <summary>SDL 3.5's Direct3D 12 driver copies a draw's samplers into its command buffer's sampler
/// heap (2048) and checks for room only before the copy, not for the whole draw: 4 samplers at 2046
/// run past the heap's end (D3D12 error #646, the debug layer's exception 0x87D; without the layer,
/// memory overwritten). A command buffer is kept within one heap: a long pass goes on in the next.
/// <code>
///   draws ─► 2048 - reserve reached ─► end pass, submit ─► new command buffer ─► pass resumed (loaded)
/// </code></summary>
public sealed unsafe partial class Renderer
{
    private const int SamplerHeap = 2048;
    /// <summary>A draw's most samplers (the enhanced shader's).</summary>
    private const int DrawSamplers = EnhancedSamplers;
    /// <summary>The frame's samplers outside the passes' draw loops: sky, post, gamma, stereo,
    /// mipmaps, blits (about 30 with stereo and insets).</summary>
    private const int OtherSamplers = 64;

    /// <summary>An open pass's targets (loaded) and area: where it goes on in a new command buffer.</summary>
    private struct OpenPass
    {
        public SDL_GPUColorTargetInfo Colour;
        public bool Coloured;
        public SDL_GPUDepthStencilTargetInfo Depth;
        public SDL_GPUViewport Viewport;
        public SDL_Rect Scissor;
        public bool Clipped;
    }

    private OpenPass _openPass;
    /// <summary>The samplers the command buffer's draw loops may have bound (a draw's most each).</summary>
    private int _samplersBound;
    /// <summary>The frame's point lights, pushed again into a new command buffer.</summary>
    private LightUniforms _lightUniforms;
    /// <summary>The frame's command buffer holding the swapchain texture (null: none): presents at its submit.</summary>
    private SDL_GPUCommandBuffer* _presenting;

    /// <summary>Only Direct3D 12 has the bug.</summary>
    private bool HeapBound => Gpu.Backend == GpuBackend.Direct3D12;

    /// <summary>Kept for the next command buffer when a pass may go on there (Direct3D 12).</summary>
    private SDL_GPUStoreOp DepthStore => HeapBound ? SDL_GPUStoreOp.SDL_GPU_STOREOP_STORE : SDL_GPUStoreOp.SDL_GPU_STOREOP_DONT_CARE;

    /// <summary>Begins a pass that <see cref="Reserve"/> can resume.</summary>
    private SDL_GPURenderPass* BeginPass(SDL_GPUCommandBuffer* commands, SDL_GPUColorTargetInfo* colour, SDL_GPUDepthStencilTargetInfo* depth)
    {
        _openPass = new OpenPass { Coloured = colour != null, Depth = *depth };
        _openPass.Depth.load_op = SDL_GPULoadOp.SDL_GPU_LOADOP_LOAD;
        if (colour != null)
        {
            _openPass.Colour = *colour;
            _openPass.Colour.load_op = SDL_GPULoadOp.SDL_GPU_LOADOP_LOAD;
        }

        _band = null;
        return SDL_BeginGPURenderPass(commands, colour, colour == null ? 0u : 1u, depth);
    }

    /// <summary>The open pass's viewport and scissor, set again when it's resumed.</summary>
    private void ClipPass(SDL_GPURenderPass* pass, SDL_GPUViewport viewport, SDL_Rect scissor)
    {
        SDL_SetGPUViewport(pass, &viewport);
        SDL_SetGPUScissor(pass, &scissor);
        (_openPass.Viewport, _openPass.Scissor, _openPass.Clipped) = (viewport, scissor, true);
    }

    /// <summary>Room for a draw's samplers: when the heap would run out, the pass ends, the command
    /// buffer is submitted and the pass goes on in a new one.</summary>
    private void Reserve(ref SDL_GPUCommandBuffer* commands, ref SDL_GPURenderPass* pass)
    {
        if (!HeapBound)
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
            throw new InvalidOperationException("The swapchain's command buffer submitted before the frame's end");
        }

        SDL_EndGPURenderPass(pass);
        SDL_SubmitGPUCommandBuffer(commands);
        commands = SDL_AcquireGPUCommandBuffer(_device);
        _samplersBound = DrawSamplers;

        // Uniforms belong to a command buffer: the lights again (the draws push the rest).
        fixed (LightUniforms* lights = &_lightUniforms)
        {
            SDL_PushGPUFragmentUniformData(commands, LightSlot, (IntPtr)lights, (uint)sizeof(LightUniforms));
        }

        var colour = _openPass.Colour;
        var depth = _openPass.Depth;
        pass = SDL_BeginGPURenderPass(commands, _openPass.Coloured ? &colour : null, _openPass.Coloured ? 1u : 0u, &depth);
        _band = null;
        if (!_openPass.Clipped)
        {
            return;
        }

        var viewport = _openPass.Viewport;
        var scissor = _openPass.Scissor;
        SDL_SetGPUViewport(pass, &viewport);
        SDL_SetGPUScissor(pass, &scissor);
    }
}
