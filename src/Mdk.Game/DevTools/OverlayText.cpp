#include "Mdk.Game/DevTools/OverlayText.h"

#include <array>
#include <format>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/WorkingSet.h"

namespace Mdk::Game::DevTools
{
    namespace
    {
        // C++ has no enum names: the profiler's sections, by value, in lower case.
        constexpr std::array<const char*, Engine::Diagnostics::SectionCount> SectionNames{"scene", "render", "physics", "scripts", "audio"};
    }

    MemorySample MemorySample::Now()
    {
        // No GC in C++: no heap, no collections.
        return {0, Mdk::WorkingSet(), {}};
    }

    std::vector<std::string> OverlayText::Lines(const OverlaySample& sample)
    {
        const auto& profiler = *sample.Profiler;
        std::vector<std::string> sectionTexts;
        for (auto s = 0; s < Engine::Diagnostics::SectionCount; s++)
        {
            sectionTexts.push_back(std::format("{} {}", SectionNames[s], Format(profiler.Milliseconds(static_cast<Engine::Diagnostics::Section>(s)), "0.0")));
        }

        auto sections = Str::Join("  ", sectionTexts);
        const auto& memory = sample.Memory;
        std::vector<std::string> collections;
        for (auto count : memory.Collections)
        {
            collections.push_back(std::to_string(count));
        }

        std::vector<std::string> lines{
            std::format("FPS {} ({} ms)", Format(profiler.FramesPerSecond(), "0"), Format(profiler.FrameMilliseconds(), "0.0")),
            std::format("{} ms", sections),
            std::format("draw calls {}, triangles {}, GPU wait {} ms", sample.Render.DrawCalls, sample.Render.Triangles, Format(sample.Render.GpuWait, "0.0")),
            std::format("GC heap {} MB, working set {} MB, collections {}", Format(memory.GcHeap / Megabyte, "0.0"),
                Format(memory.WorkingSet / Megabyte, "0.0"), Str::Join("/", collections)),
        };
        lines.insert(lines.end(), sample.System.begin(), sample.System.end());
        lines.insert(lines.end(), sample.Screen.begin(), sample.Screen.end());
        return lines;
    }

    std::vector<std::string> OverlayText::Level(const Placement& kurt, int objects, const std::string& state)
    {
        auto feet = kurt.Feet;
        return {
            std::format("Kurt {} {} {} yaw {} {}", Format(feet.X, "0.00"), Format(feet.Y, "0.00"), Format(feet.Z, "0.00"), Format(kurt.Yaw, "0"), state),
            std::format("level {} arena {}, objects {}", kurt.Level, kurt.Arena, objects),
        };
    }

    std::string OverlayText::Format(double value, const std::string& format)
    {
        // "0", "0.0", "0.00": as many decimals as zeros after the point, rounded half away from zero.
        auto point = Str::IndexOf(format, '.');
        auto decimals = point < 0 ? 0 : static_cast<int>(format.size()) - point - 1;
        auto scale = std::pow(10.0, decimals);
        return std::format("{:.{}f}", Math::RoundAwayFromZero(value * scale) / scale, decimals);
    }
}
