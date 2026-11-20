#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <doctest.h>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/TimeSpan.h"
#include "Mdk.Engine/Platform/Display.h"
#include "Mdk.Engine/Render/GpuBackend.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/Flow/Settings.h"
#include "Mdk.Game/HdTextures/HdMenu.h"
#include "Mdk.Game/Menu/DisplayMenu.h"

using namespace Mdk;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Engine::Render;
using Mdk::Game::Flow::Settings;
using Mdk::Game::HdTextures::Host;
using Mdk::Game::Menu::DisplayItem;
using Mdk::Game::Menu::DisplayMenu;

// The display options: window sizes and screen modes, the GPU backend per OS, VSync's
// present modes, the frame limiter, their settings and the Display page's items.

namespace
{
    template <typename T>
    bool Contains(const std::vector<T>& list, const T& item)
    {
        return std::find(list.begin(), list.end(), item) != list.end();
    }
}

TEST_CASE("DisplayTests.WindowedSizesFitTheUsableArea")
{
    auto sizes = Resolutions::Windowed(Resolution{1920, 1040}, Resolution{1280, 960});

    CHECK(Contains(sizes, Resolution{1280, 960}));
    CHECK(Contains(sizes, Resolution{1600, 900}));
    CHECK_FALSE(Contains(sizes, Resolution{1920, 1080}));
    CHECK_FALSE(Contains(sizes, Resolution{1600, 1200}));
    for (const auto& s : sizes)
    {
        CHECK((s.Width <= 1920 && s.Height <= 1040));
    }
}

TEST_CASE("DisplayTests.WindowedSizesKeepTheCurrentOnceSorted")
{
    auto sizes = Resolutions::Windowed(Resolution{3440, 1400}, Resolution{1234, 777});

    CHECK(1 == std::count(sizes.begin(), sizes.end(), Resolution{1234, 777}));
    CHECK(Contains(sizes, Resolution{1920, 1080}));
    CHECK_FALSE(Contains(sizes, Resolution{2560, 1440}));

    // OrderBy(area).ThenBy(width): a stable sort by (area, width).
    auto sorted = sizes;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Resolution& a, const Resolution& b) {
        return a.Width * a.Height != b.Width * b.Height ? a.Width * a.Height < b.Width * b.Height : a.Width < b.Width;
    });
    CHECK(sorted == sizes);

    // Distinct() keeps the first of each.
    std::vector<Resolution> distinct;
    for (const auto& s : sizes)
    {
        if (!Contains(distinct, s))
        {
            distinct.push_back(s);
        }
    }

    CHECK(distinct == sizes);
}

TEST_CASE("DisplayTests.ExclusiveModesDedupeByHighestRefresh")
{
    std::vector<ScreenMode> modes{
        {1920, 1080, 60.f}, {1920, 1080, 144.f}, {1920, 1080, 120.f},
        {1280, 720, 60.f}, {3440, 1440, 100.f}, {3440, 1440, 144.f},
    };

    auto list = Resolutions::Exclusive(modes);

    CHECK(std::vector<ScreenMode>{{1280, 720, 60.f}, {1920, 1080, 144.f}, {3440, 1440, 144.f}} == list);
}

TEST_CASE("DisplayTests.StepWrapsAndStartsFromUnknown")
{
    std::vector<Resolution> list{{800, 600}, {1280, 960}, {1920, 1080}};

    CHECK(Resolution{1920, 1080} == Resolutions::Step(list, Resolution{1280, 960}, 1));
    CHECK(Resolution{800, 600} == Resolutions::Step(list, Resolution{1920, 1080}, 1));
    CHECK(Resolution{1920, 1080} == Resolutions::Step(list, Resolution{800, 600}, -1));
    CHECK(Resolution{800, 600} == Resolutions::Step(list, Resolution{1, 1}, 1));
}

TEST_CASE("DisplayTests.ResolutionParses")
{
    Resolution size;
    CHECK(Resolution::TryParse("1600x1200", size));
    CHECK(Resolution{1600, 1200} == size);
    CHECK("1600x1200" == size.ToString());
    Resolution unused;
    CHECK_FALSE(Resolution::TryParse("big", unused));
    CHECK_FALSE(Resolution::TryParse("0x600", unused));
}

TEST_CASE("DisplayTests.BackendsPerOs")
{
    struct Row
    {
        Os Os;
        std::vector<GpuBackend> Backends;
    };

    const Row rows[] = {
        {Os::Windows, {GpuBackend::Auto, GpuBackend::Direct3D12, GpuBackend::Vulkan}},
        {Os::Linux, {GpuBackend::Auto, GpuBackend::Vulkan}},
        {Os::MacOs, {GpuBackend::Auto, GpuBackend::Metal}},
        {Os::Android, {GpuBackend::Vulkan}},
    };

    for (const auto& row : rows)
    {
        CAPTURE(static_cast<int>(row.Os));
        CHECK(row.Backends == GpuBackends::On(row.Os));
    }
}

TEST_CASE("DisplayTests.UnavailableBackendFallsBack")
{
    struct Row
    {
        GpuBackend Wanted;
        Os Os;
        GpuBackend Used;
    };

    const Row rows[] = {
        {GpuBackend::Vulkan, Os::Windows, GpuBackend::Vulkan},
        {GpuBackend::Direct3D12, Os::Linux, GpuBackend::Auto},
        {GpuBackend::Metal, Os::Windows, GpuBackend::Auto},
        {GpuBackend::Direct3D12, Os::Android, GpuBackend::Vulkan},
        {GpuBackend::Auto, Os::MacOs, GpuBackend::Auto},
    };

    for (const auto& row : rows)
    {
        CAPTURE(static_cast<int>(row.Wanted));
        CAPTURE(static_cast<int>(row.Os));
        CHECK(row.Used == GpuBackends::Resolve(row.Wanted, row.Os));
    }
}

TEST_CASE("DisplayTests.BackendAttemptsEndWithAuto")
{
    CHECK(std::vector<GpuBackend>{GpuBackend::Vulkan, GpuBackend::Auto} == GpuBackends::Attempts(GpuBackend::Vulkan, Os::Windows));
    CHECK(std::vector<GpuBackend>{GpuBackend::Auto} == GpuBackends::Attempts(GpuBackend::Auto, Os::Windows));
    CHECK(std::vector<GpuBackend>{GpuBackend::Auto} == GpuBackends::Attempts(GpuBackend::Metal, Os::Linux));
}

TEST_CASE("DisplayTests.BackendParses")
{
    struct Row
    {
        const char* Text;
        GpuBackend Backend;
    };

    const Row rows[] = {
        {"d3d12", GpuBackend::Direct3D12},
        {"direct3d12", GpuBackend::Direct3D12},
        {"Vulkan", GpuBackend::Vulkan},
        {"metal", GpuBackend::Metal},
        {"auto", GpuBackend::Auto},
    };

    for (const auto& row : rows)
    {
        CAPTURE(row.Text);
        CHECK(std::optional<GpuBackend>(row.Backend) == GpuBackends::Parse(row.Text));
    }
}

TEST_CASE("DisplayTests.BackendNames")
{
    CHECK_FALSE(GpuBackends::Parse("glide").has_value());
    CHECK(std::optional<std::string>("direct3d12") == GpuBackends::Driver(GpuBackend::Direct3D12));
    CHECK(std::optional<std::string>("vulkan") == GpuBackends::Driver(GpuBackend::Vulkan));
    CHECK(std::optional<std::string>("metal") == GpuBackends::Driver(GpuBackend::Metal));
    CHECK_FALSE(GpuBackends::Driver(GpuBackend::Auto).has_value());
    CHECK(GpuBackend::Direct3D12 == GpuBackends::Of("direct3d12"));
    CHECK("Direct3D 12" == GpuBackends::Name(GpuBackend::Direct3D12));
}

TEST_CASE("DisplayTests.PresentModeFallsBack")
{
    struct Row
    {
        VSync Wanted;
        std::vector<VSync> Supported;
        VSync Used;
    };

    const Row rows[] = {
        {VSync::On, {}, VSync::On},
        {VSync::Off, {VSync::Off}, VSync::Off},
        {VSync::Off, {VSync::Adaptive}, VSync::Adaptive},
        {VSync::Off, {}, VSync::On},
        {VSync::Adaptive, {VSync::Adaptive}, VSync::Adaptive},
        {VSync::Adaptive, {VSync::Off}, VSync::On},
    };

    for (const auto& row : rows)
    {
        CAPTURE(static_cast<int>(row.Wanted));
        const auto& supported = row.Supported;
        CHECK(row.Used == PresentModes::Choose(row.Wanted, [&](VSync mode) { return mode == VSync::On || Contains(supported, mode); }));
    }
}

TEST_CASE("DisplayTests.FrameLimiterWaitsTheRest")
{
    using std::chrono::milliseconds;
    CHECK(TimeSpan::zero() == FrameLimiter::Remaining(milliseconds(5), FrameLimiter::Off));
    CHECK(TimeSpan::zero() == FrameLimiter::Remaining(milliseconds(20), 60));
    CHECK(TimeSpan(TicksPerSecond / 60 - TicksPerMillisecond * 5) == FrameLimiter::Remaining(milliseconds(5), 60));
}

TEST_CASE("DisplayTests.OldSettingsKeepDisplayDefaults")
{
    auto on = Settings::Parse("fullscreen=True\n");
    auto off = Settings::Parse("fullscreen=False\n");

    CHECK(Fullscreen::Desktop == on.Fullscreen);
    CHECK(Fullscreen::Off == off.Fullscreen);
    CHECK(Resolution{1280, 960} == off.WindowSize);
    CHECK_FALSE(off.ExclusiveSize.has_value());
    CHECK(100 == off.RenderScale);
    CHECK(VSync::On == off.VSync);
    CHECK(FrameLimiter::Off == off.FrameLimit);
    CHECK(GpuBackend::Auto == off.Backend);
}

TEST_CASE("DisplayTests.DisplaySettingsRoundTrip")
{
    Settings settings;
    settings.Fullscreen = Fullscreen::Exclusive;
    settings.WindowSize = Resolution{1600, 1200};
    settings.ExclusiveSize = Resolution{2560, 1440};
    settings.RenderScale = 150;
    settings.VSync = VSync::Adaptive;
    settings.FrameLimit = 144;
    settings.Backend = GpuBackend::Vulkan;

    auto read = Settings::Parse(settings.Format());

    CHECK(settings.Format() == read.Format());
    CHECK(Fullscreen::Exclusive == read.Fullscreen);
    CHECK(Resolution{1600, 1200} == read.WindowSize);
    CHECK(std::optional<Resolution>(Resolution{2560, 1440}) == read.ExclusiveSize);
    CHECK(150 == read.RenderScale);
    CHECK(VSync::Adaptive == read.VSync);
    CHECK(144 == read.FrameLimit);
    CHECK(GpuBackend::Vulkan == read.Backend);
}

TEST_CASE("DisplayTests.AutoRenderScaleAimsAt1080p")
{
    struct Row
    {
        uint Width, Height;
        int Percent;
    };

    const Row rows[] = {
        {2880u, 1800u, 63},
        {2344u, 2156u, 64},
        {2400u, 1080u, 89},
        {1920u, 1080u, 100},
        {1280u, 720u, 100},
        {7680u, 4320u, 50},
        {0u, 0u, 100},
    };

    for (const auto& row : rows)
    {
        CAPTURE(row.Width);
        CAPTURE(row.Height);

        // A tablet's 5.2 MP drawn as about 2.1 MP; never above the window's, never below half.
        CHECK(row.Percent == Renderer::AutoPercent(row.Width, row.Height));
    }
}

TEST_CASE("DisplayTests.MobileOcclusionHasHalfTheSides")
{
    struct Row
    {
        uint Side, Step, Occlusion;
    };

    const Row rows[] = {
        {1800u, 1u, 1800u},
        {1800u, 2u, 900u},
        {1801u, 2u, 901u},
    };

    for (const auto& row : rows)
    {
        CAPTURE(row.Side);
        CAPTURE(row.Step);

        // Whole texels: the last one partly off an odd frame.
        CHECK(row.Occlusion == Renderer::OcclusionSide(row.Side, row.Step));
    }
}

TEST_CASE("DisplayTests.AutoRenderScaleIsSaved")
{
    Settings settings;
    settings.RenderScale = Renderer::AutoScale;
    auto read = Settings::Parse(settings.Format());
    CHECK(Renderer::AutoScale == read.RenderScale);
}

TEST_CASE("DisplayTests.BadDisplaySettingsKeepDefaults")
{
    auto read = Settings::Parse("fullscreen=Maybe\nwindow_size=big\nexclusive_size=0x0\nrender_scale=33\nvsync=7\nframe_limit=59\ngpu_backend=Glide\n");
    Settings defaults;

    CHECK(defaults.Format() == read.Format());
}

TEST_CASE("DisplayTests.AndroidHidesWindowAndBackend")
{
    CHECK(std::vector<DisplayItem>{DisplayItem::Mode, DisplayItem::Resolution, DisplayItem::Scale, DisplayItem::VSync, DisplayItem::FrameLimit, DisplayItem::Backend, DisplayItem::Gamma}
        == DisplayMenu::Items(Host::Desktop));
    CHECK(std::vector<DisplayItem>{DisplayItem::Scale, DisplayItem::VSync, DisplayItem::FrameLimit, DisplayItem::Gamma} == DisplayMenu::Items(Host::Android));
}
