#include <string>
#include <vector>

#include <doctest.h>

#include "Mdk.Core/Drawing.h"
#include "Mdk.Engine/Platform/Display.h"
#include "Mdk.Engine/Platform/Window.h"
#include "Mdk.Engine/Render/GpuBackend.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/DevTools/TechInfo.h"
#include "Mdk.Game/Flow/Settings.h"

using namespace Mdk;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Engine::Render;
using namespace Mdk::Game::DevTools;
using namespace Mdk::Game::Flow;

// The technical lines logged at start (and the display's again when it changes) and shown by
// the overlay.
namespace
{
    const GpuInfo TestGpu{GpuBackend::Vulkan, "NVIDIA GeForce RTX 3060", "581.57"};

    const ScreenInfo Screen{ScreenMode{3440, 1440, 144.0f}, Resolution{1280, 960}, Fullscreen::Off, Visibility::Shown,
        Resolution{1920, 1440}, "B8G8R8A8_UNORM", "D32_FLOAT", 4, VSync::On};

    bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
}

TEST_CASE("TechInfoTests.BuildLine")
{
    CHECK(TechInfo::Build(BuildInfo{"1.0.0-beta.1", "10.0.1", Compilation::NativeAot, "3.4.0"})
        == "MDK SDL 1.0.0-beta.1 | .NET 10.0.1 Native AOT | SDL 3.4.0");
}

TEST_CASE("TechInfoTests.GpuLine")
{
    CHECK(TechInfo::Gpu(TestGpu) == "GPU: NVIDIA GeForce RTX 3060 (Vulkan via SDL_GPU), driver 581.57");
}

TEST_CASE("TechInfoTests.GpuLineWithoutDriverDetails")
{
    CHECK(TechInfo::Gpu(GpuInfo{GpuBackend::Direct3D12, "", ""}) == "GPU: unknown (Direct3D 12 via SDL_GPU)");
}

TEST_CASE("TechInfoTests.DisplayLine")
{
    CHECK(TechInfo::Display(Screen)
        == "Display: 3440x1440 @ 144 Hz, window 1280x960 (windowed), render target 1920x1440, "
           "swapchain B8G8R8A8_UNORM (8 bits per channel, SDR), depth D32_FLOAT, MSAA 4x, present mode VSYNC");
}

TEST_CASE("TechInfoTests.DisplayLineNamesTheModes")
{
    auto modes = Screen;
    modes.Fullscreen = Fullscreen::Exclusive;
    modes.Samples = 1;
    modes.VSync = VSync::Adaptive;
    auto line = TechInfo::Display(modes);

    CHECK(Contains(line, "(exclusive fullscreen)"));
    CHECK(Contains(line, "MSAA off"));
    CHECK(Contains(line, "present mode MAILBOX"));
    auto hidden = Screen;
    hidden.Visibility = Visibility::Hidden;
    CHECK(Contains(TechInfo::Display(hidden), "(hidden)"));
}

TEST_CASE("TechInfoTests.LookLine")
{
    CHECK(TechInfo::Look(Graphics::Enhanced, {"hd-textures", "gltf"}) == "Look: Enhanced, mods: hd-textures, gltf");
    CHECK(TechInfo::Look(Graphics::Original, {}) == "Look: Original, mods: none");
}

TEST_CASE("TechInfoTests.LayoutLine")
{
    // A foldable's inner display at 420 dpi (2.625 units a dp, 160 dp an inch).
    RectangleF safe(0.0f, 0.0f, 2156.0f, 2344.0f);

    CHECK(TechInfo::Layout(420.0f / 25.4f, safe, Renderer::AutoScale, Resolution{2156, 2344}, EffectQuality::Mobile)
        == "Layout: 420 units/inch, safe area 0,0 2156x2344, render scale Auto (64 %), quality Mobile");
    CHECK(Contains(TechInfo::Layout(0.0f, safe, 150, Resolution{1280, 960}, EffectQuality::Full), "render scale 150 %, quality Full"));
}

TEST_CASE("TechInfoTests.OverlayLines")
{
    CHECK(TechInfo::Overlay(TestGpu, Screen)
        == std::vector<std::string>{"GPU NVIDIA GeForce RTX 3060, Vulkan", "window 1280x960 windowed, target 1920x1440, B8G8R8A8_UNORM, VSYNC"});
}
