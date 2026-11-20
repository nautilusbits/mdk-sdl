#pragma once

#include "Mdk.Engine/Platform/Display.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Mdk::Engine::Render
{
    // SDL_GPU's driver: chosen by SDL (Auto) or asked for (Options, Display; --gpu).
    enum class GpuBackend { Auto, Direct3D12, Vulkan, Metal };

    // The backends per OS and their SDL names. One that fails (or isn't on the OS) falls back
    // to SDL's choice.
    //   Windows: Direct3D 12, Vulkan   Linux: Vulkan   macOS: Metal   Android: Vulkan only
    struct GpuBackends
    {
        static std::vector<GpuBackend> On(Platform::Os os);

        // The backend asked for if the OS has it, else the OS's first.
        static GpuBackend Resolve(GpuBackend wanted, Platform::Os os);

        // The backends tried in turn: the one asked for, then SDL's choice.
        static std::vector<GpuBackend> Attempts(GpuBackend wanted, Platform::Os os);

        // SDL's driver name (SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING); none for Auto.
        static std::optional<std::string> Driver(GpuBackend backend);

        // The backend of an SDL driver name (Auto if unknown).
        static GpuBackend Of(const std::string& driver);

        // --gpu's names: d3d12 (or direct3d12), vulkan, metal, auto; null if none.
        static std::optional<GpuBackend> Parse(const std::string& text);

        static std::string Name(GpuBackend backend);
    };

    // How frames meet the display's refresh: VSync on (SDL_GPU_PRESENTMODE_VSYNC), off
    // (IMMEDIATE: may tear) or adaptive (MAILBOX: no tearing, no waiting; the newest frame shows).
    enum class VSync { On, Off, Adaptive };

    class PresentModes
    {
    public:
        // The first mode of the choice's order the window supports.
        static VSync Choose(VSync wanted, const std::function<bool(VSync)>& supported);

        // SDL's name of a mode, without its prefix.
        static std::string Name(VSync mode);

    private:
        // The modes tried for each choice; VSync is always supported.
        static std::vector<VSync> Order(VSync wanted);
    };

    // The GPU device: its backend, name and driver version (empty when SDL doesn't tell).
    struct GpuInfo
    {
        GpuBackend Backend = GpuBackend::Auto;
        std::string Device;
        std::string DriverVersion;

        bool operator==(const GpuInfo&) const = default;
    };
}
