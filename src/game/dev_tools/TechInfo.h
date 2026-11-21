#pragma once

#include <string>
#include <vector>

#include "core/Drawing.h"
#include "engine/platform/Display.h"
#include "engine/platform/Window.h"
#include "engine/render/GpuBackend.h"
#include "engine/render/Renderer.h"

namespace Mdk::Game::Flow
{
    enum class Graphics;
}

namespace Mdk::Game::DevTools
{
    // How the program was compiled: ahead of time (the published exe) or just in time.
    enum class Compilation { NativeAot, Jit };

    // The program's version, .NET's, how it's compiled, and SDL's version.
    struct BuildInfo
    {
        std::string Version;
        std::string Runtime;
        DevTools::Compilation Compilation{};
        std::string Sdl;

        bool operator==(const BuildInfo&) const = default;

        static BuildInfo Current();
    };

    // The display and the frame: the desktop's mode, the window, the frame drawn and how it's shown.
    struct ScreenInfo
    {
        Engine::Platform::ScreenMode Desktop;
        Engine::Platform::Resolution Window;
        Engine::Platform::Fullscreen Fullscreen{};
        Engine::Platform::Visibility Visibility{};
        Engine::Platform::Resolution Target;
        std::string Swapchain;
        std::string Depth;
        int Samples = 0;
        Engine::Render::VSync VSync{};

        bool operator==(const ScreenInfo&) const = default;
    };

    // The technical lines logged at start (the display's again when it changes) and the
    // overlay's.
    //   MDK SDL 2.0.0 | clang 22.1.8 | SDL 3.5.0
    //   GPU: NVIDIA GeForce RTX 3060 (Vulkan via SDL_GPU), driver 581.57
    //   Display: 3440x1440 @ 144 Hz, window 1280x960 (windowed), render target 1280x960, swapchain
    //            B8G8R8A8_UNORM (8 bits per channel, SDR), depth D32_FLOAT, MSAA 4x, present mode VSYNC
    //   Look: Enhanced, mods: hd-textures
    struct TechInfo
    {
        static std::string Build(const BuildInfo& build);

        static std::string Gpu(const Engine::Render::GpuInfo& gpu);

        static std::string Display(const ScreenInfo& screen);

        // What sizes the layout and the frame's cost: the density (window units an inch),
        // the safe area (cut-outs), the render scale and the enhanced look's quality.
        //   Layout: 420 units/inch, safe area 0,0 2156x2344, render scale Auto (64 %), quality Mobile
        static std::string Layout(float unitsPerMm, RectangleF safe, int scale, Engine::Platform::Resolution window,
            Engine::Render::EffectQuality quality);

        static std::string Look(Flow::Graphics graphics, const std::vector<std::string>& mods);

        // The overlay's two lines: the GPU, and the window and frame.
        static std::vector<std::string> Overlay(const Engine::Render::GpuInfo& gpu, const ScreenInfo& screen);

    private:
        static constexpr float MmPerInch = 25.4f;

        static std::string WindowMode(const ScreenInfo& screen);

        static std::string Msaa(int samples) { return samples > 1 ? std::to_string(samples) + "x" : "off"; }
    };
}
