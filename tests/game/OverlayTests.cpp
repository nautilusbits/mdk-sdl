#include <doctest.h>

#include "engine/diagnostics/Profiler.h"
#include "engine/render/Renderer.h"
#include "game/dev_tools/OverlayText.h"

#include <string>
#include <vector>

using namespace Mdk;
using namespace Mdk::Engine::Diagnostics;
using namespace Mdk::Engine::Render;
using namespace Mdk::Game::DevTools;

// The debug overlay's text.
namespace
{
    constexpr long long Megabyte = 1024 * 1024;

    std::string Join(const std::vector<std::string>& lines)
    {
        std::string text;
        for (size_t i = 0; i < lines.size(); i++)
        {
            text += (i == 0 ? "" : "\n") + lines[i];
        }

        return text;
    }

    bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
}

TEST_CASE("OverlayTests.LinesShowTimingsRendererMemoryAndKurt")
{
    Profiler profiler;
    OverlaySample sample{&profiler, RenderStats{120, 4500},
        MemorySample{12 * Megabyte, 80 * Megabyte, {3, 2, 1}},
        OverlayText::Level(Placement{6, "ARENA_2", Vector3(1.f, -2.5f, 30.f), 270.f}, 42, "Run")};

    auto text = Join(OverlayText::Lines(sample));
    CHECK(Contains(text, "draw calls 120, triangles 4500"));
    CHECK(Contains(text, "GC heap 12.0 MB, working set 80.0 MB, collections 3/2/1"));
    CHECK(Contains(text, "Kurt 1.00 -2.50 30.00 yaw 270 Run"));
    CHECK(Contains(text, "level 6 arena ARENA_2, objects 42"));
    CHECK(Contains(text, "render"));
    CHECK(Contains(text, "audio"));
}

// Other screens show their own lines (a menu its name).
TEST_CASE("OverlayTests.ScreensAddTheirLines")
{
    Profiler profiler;
    OverlaySample sample{&profiler, RenderStats{}, MemorySample{0, 0, {0}}, {"main menu"}};
    auto lines = OverlayText::Lines(sample);
    CHECK(lines[0].starts_with("FPS"));
    CHECK(lines.back() == "main menu");
}

// The GPU's and the display's lines come before the screen's.
TEST_CASE("OverlayTests.SystemLinesBeforeTheScreens")
{
    Profiler profiler;
    OverlaySample sample{&profiler, RenderStats{}, MemorySample{0, 0, {0}}, {"main menu"}};
    sample.System = {"GPU X, Vulkan"};
    auto lines = OverlayText::Lines(sample);
    CHECK(lines[lines.size() - 2] == "GPU X, Vulkan");
    CHECK(lines.back() == "main menu");
}
