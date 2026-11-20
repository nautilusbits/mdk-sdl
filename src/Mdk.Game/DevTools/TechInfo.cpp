#include "Mdk.Game/DevTools/TechInfo.h"

#include <array>
#include <format>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/Flow/Settings.h"

namespace Mdk::Game::DevTools
{
    namespace
    {
        // The program's version (C#: the assembly's informational version); CMake's project version.
#ifdef MDK_VERSION
        constexpr const char* ProgramVersion = MDK_VERSION;
#else
        constexpr const char* ProgramVersion = "1.2.6";
#endif

        // C++ has no enum names: those the lines print, by value.
        constexpr std::array<const char*, 2> QualityNames{"Full", "Mobile"};
        constexpr std::array<const char*, 2> GraphicsNames{"Original", "Enhanced"};

        // value.ToString("0"): rounded half away from zero.
        std::string Whole(float value)
        {
            return std::format("{:.0f}", MathF::RoundAwayFromZero(value));
        }

        // Environment.Version's counterpart: the compiler that built the program.
        std::string CompilerVersion()
        {
#if defined(__clang__)
            return std::format("clang {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
            return std::format("gcc {}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
            return std::format("msvc {}", _MSC_VER);
#else
            return "unknown";
#endif
        }
    }

    BuildInfo BuildInfo::Current()
    {
        std::string version = ProgramVersion;
        // "1.0.0-beta.1+commit": the commit stays out.
        auto plus = Str::IndexOf(version, '+');
        // C++ is always compiled ahead of time.
        auto compilation = Compilation::NativeAot;
        return {plus < 0 ? version : version.substr(0, plus), CompilerVersion(), compilation, Engine::Platform::Window::SdlVersion()};
    }

    std::string TechInfo::Build(const BuildInfo& build)
    {
        auto compiled = build.Compilation == Compilation::NativeAot ? "Native AOT" : "JIT";
        return std::format("MDK SDL {} | .NET {} {} | SDL {}", build.Version, build.Runtime, compiled, build.Sdl);
    }

    std::string TechInfo::Gpu(const Engine::Render::GpuInfo& gpu)
    {
        auto device = !gpu.Device.empty() ? gpu.Device : "unknown";
        auto driver = !gpu.DriverVersion.empty() ? std::format(", driver {}", gpu.DriverVersion) : "";
        return std::format("GPU: {} ({} via SDL_GPU){}", device, Engine::Render::GpuBackends::Name(gpu.Backend), driver);
    }

    std::string TechInfo::Display(const ScreenInfo& screen)
    {
        const auto& desktop = screen.Desktop;
        auto refresh = Str::FromFloatTrim(desktop.Refresh, 2);
        return std::format("Display: {} @ {} Hz, window {} ({}), render target {}, ", desktop.Size().ToString(), refresh,
                   screen.Window.ToString(), WindowMode(screen), screen.Target.ToString())
            + std::format("swapchain {} (8 bits per channel, SDR), depth {}, MSAA {}, ", screen.Swapchain, screen.Depth, Msaa(screen.Samples))
            + std::format("present mode {}", Engine::Render::PresentModes::Name(screen.VSync));
    }

    std::string TechInfo::Layout(float unitsPerMm, RectangleF safe, int scale, Engine::Platform::Resolution window,
        Engine::Render::EffectQuality quality)
    {
        auto density = Whole(unitsPerMm * MmPerInch);
        auto scaleText = scale == Engine::Render::Renderer::AutoScale
            ? std::format("Auto ({} %)", Engine::Render::Renderer::AutoPercent(static_cast<uint>(window.Width), static_cast<uint>(window.Height)))
            : std::format("{} %", scale);
        return std::format("Layout: {} units/inch, safe area {},{} {}x{}, ", density, Whole(safe.X), Whole(safe.Y), Whole(safe.Width), Whole(safe.Height))
            + std::format("render scale {}, quality {}", scaleText, QualityNames[static_cast<int>(quality)]);
    }

    std::string TechInfo::Look(Flow::Graphics graphics, const std::vector<std::string>& mods)
    {
        return std::format("Look: {}, mods: {}", GraphicsNames[static_cast<int>(graphics)], !mods.empty() ? Str::Join(", ", mods) : "none");
    }

    std::vector<std::string> TechInfo::Overlay(const Engine::Render::GpuInfo& gpu, const ScreenInfo& screen)
    {
        return {
            std::format("GPU {}, {}", !gpu.Device.empty() ? gpu.Device : "unknown", Engine::Render::GpuBackends::Name(gpu.Backend)),
            std::format("window {} {}, target {}, {}, {}", screen.Window.ToString(), WindowMode(screen), screen.Target.ToString(), screen.Swapchain,
                Engine::Render::PresentModes::Name(screen.VSync)),
        };
    }

    std::string TechInfo::WindowMode(const ScreenInfo& screen)
    {
        if (screen.Visibility == Engine::Platform::Visibility::Hidden)
        {
            return "hidden";
        }

        switch (screen.Fullscreen)
        {
            case Engine::Platform::Fullscreen::Desktop:
                return "fullscreen";
            case Engine::Platform::Fullscreen::Exclusive:
                return "exclusive fullscreen";
            default:
                return "windowed";
        }
    }
}
