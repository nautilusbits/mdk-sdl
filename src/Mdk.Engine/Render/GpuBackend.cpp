#include "Mdk.Engine/Render/GpuBackend.h"

#include "Mdk.Core/DotNet.h"

#include <algorithm>
#include <array>

namespace Mdk::Engine::Render
{
    namespace
    {
        // Enum.GetValues<GpuBackend>().
        constexpr std::array AllBackends{GpuBackend::Auto, GpuBackend::Direct3D12, GpuBackend::Vulkan, GpuBackend::Metal};
    }

    std::vector<GpuBackend> GpuBackends::On(Platform::Os os)
    {
        switch (os)
        {
        case Platform::Os::Windows:
            return {GpuBackend::Auto, GpuBackend::Direct3D12, GpuBackend::Vulkan};
        case Platform::Os::MacOs:
            return {GpuBackend::Auto, GpuBackend::Metal};
        case Platform::Os::Android:
            return {GpuBackend::Vulkan};
        default:
            return {GpuBackend::Auto, GpuBackend::Vulkan};
        }
    }

    GpuBackend GpuBackends::Resolve(GpuBackend wanted, Platform::Os os)
    {
        const auto backends = On(os);
        return std::find(backends.begin(), backends.end(), wanted) != backends.end() ? wanted : backends[0];
    }

    std::vector<GpuBackend> GpuBackends::Attempts(GpuBackend wanted, Platform::Os os)
    {
        const auto backend = Resolve(wanted, os);
        if (backend != GpuBackend::Auto)
        {
            return {backend, GpuBackend::Auto};
        }

        return {GpuBackend::Auto};
    }

    std::optional<std::string> GpuBackends::Driver(GpuBackend backend)
    {
        switch (backend)
        {
        case GpuBackend::Direct3D12:
            return "direct3d12";
        case GpuBackend::Vulkan:
            return "vulkan";
        case GpuBackend::Metal:
            return "metal";
        default:
            return std::nullopt;
        }
    }

    GpuBackend GpuBackends::Of(const std::string& driver)
    {
        for (const auto backend : AllBackends)
        {
            if (Driver(backend) == driver)
            {
                return backend;
            }
        }

        return GpuBackend::Auto;
    }

    std::optional<GpuBackend> GpuBackends::Parse(const std::string& text)
    {
        const auto lower = Str::ToLower(text);
        if (lower == "d3d12" || lower == "direct3d12")
        {
            return GpuBackend::Direct3D12;
        }

        if (lower == "vulkan")
        {
            return GpuBackend::Vulkan;
        }

        if (lower == "metal")
        {
            return GpuBackend::Metal;
        }

        if (lower == "auto")
        {
            return GpuBackend::Auto;
        }

        return std::nullopt;
    }

    std::string GpuBackends::Name(GpuBackend backend)
    {
        // backend.ToString() for the others.
        switch (backend)
        {
        case GpuBackend::Direct3D12:
            return "Direct3D 12";
        case GpuBackend::Vulkan:
            return "Vulkan";
        case GpuBackend::Metal:
            return "Metal";
        default:
            return "Auto";
        }
    }

    std::vector<VSync> PresentModes::Order(VSync wanted)
    {
        switch (wanted)
        {
        case VSync::Off:
            return {VSync::Off, VSync::Adaptive, VSync::On};
        case VSync::Adaptive:
            return {VSync::Adaptive, VSync::On};
        default:
            return {VSync::On};
        }
    }

    VSync PresentModes::Choose(VSync wanted, const std::function<bool(VSync)>& supported)
    {
        for (const auto mode : Order(wanted))
        {
            if (supported(mode))
            {
                return mode;
            }
        }

        return VSync::On;
    }

    std::string PresentModes::Name(VSync mode)
    {
        switch (mode)
        {
        case VSync::Off:
            return "IMMEDIATE";
        case VSync::Adaptive:
            return "MAILBOX";
        default:
            return "VSYNC";
        }
    }
}
