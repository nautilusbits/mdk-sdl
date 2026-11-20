#include "Mdk.Game/DevTools/PerfLog.h"

#include <cmath>
#include <format>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/WorkingSet.h"

namespace Mdk::Game::DevTools
{
    namespace
    {
        // C++ has no enum names: the profiler's sections, by value, in lower case.
        constexpr std::array<const char*, Engine::Diagnostics::SectionCount> SectionNames{"scene", "render", "physics", "scripts", "audio"};

        double SecondsSince(long long start)
        {
            return static_cast<double>(StopwatchTicks() - start) / StopwatchFrequency;
        }
    }

    PerfLog::PerfLog(float warmup) : _warmup(warmup), _start(StopwatchTicks())
    {
    }

    void PerfLog::Loaded(TimeSpan time)
    {
        _loadMilliseconds = std::chrono::duration<double, std::milli>(time).count();
    }

    void PerfLog::Frame(float time, const Engine::Diagnostics::Profiler& profiler, Engine::Render::RenderStats stats)
    {
        // No GC in C++: no managed allocations are counted.
        long long allocated = 0;
        if (time < _warmup)
        {
            return;
        }

        // The first measured frame is the baseline.
        if (_allocated < 0)
        {
            _allocated = allocated;
            _windowStart = StopwatchTicks();
            // No GC in C++: the collections stay 0.
            _collections.fill(0);
            return;
        }

        // The previous frame is counted now: the last (the screenshot's download) never is.
        Count(_pending);
        _pending = {allocated - _allocated, profiler.LastFrameMilliseconds(), stats};
        profiler.CopyLast(_pendingSections);
        _allocated = allocated;
    }

    void PerfLog::Count(const Pending& frame)
    {
        if (frame.Frame == 0.f)
        {
            return;
        }

        auto delta = frame.Allocated;
        _allocatedSum += delta;
        _allocatedMax = std::max(_allocatedMax, delta);
        _count++;
        _frameSum += frame.Frame;
        _gpuWait += frame.Stats.GpuWait;
        _draws = frame.Stats;
        for (size_t i = 0; i < _sections.size(); i++)
        {
            _sections[i] += _pendingSections[i];
        }
    }

    std::vector<std::string> PerfLog::Report()
    {
        if (_count == 0)
        {
            return {"Perf: no frame measured"};
        }

        auto frame = _frameSum / _count;
        std::vector<std::string> sectionTexts;
        for (auto s = 0; s < Sections; s++)
        {
            sectionTexts.push_back(std::format("{} {}", SectionNames[s], Ms(_sections[s] / _count)));
        }

        auto sections = Str::Join(" ", sectionTexts);
        auto seconds = SecondsSince(_windowStart);
        // No GC in C++: every generation's count is 0, less the start's.
        std::vector<std::string> collections;
        for (auto start : _collections)
        {
            collections.push_back(std::to_string(0 - start));
        }

        // No GC in C++: the heap is 0, live or not.
        constexpr double GcHeap = 0;
        return {
            std::format("Perf: {} frames, {} ms ({} FPS): {} ms (GPU wait {})", _count, Ms(frame), F(1000.0 / frame, "0"), sections, Ms(_gpuWait / _count)),
            std::format("Perf: allocated {} B/frame (max {}), collections {} in {} s, draw calls {}, triangles {}", _allocatedSum / _count, _allocatedMax,
                Str::Join("/", collections), F(seconds, "0.0"), _draws.DrawCalls, _draws.Triangles),
            std::format("Perf: load {} ms, run {} s, GC heap {} MB (live {} MB), working set {} MB", F(_loadMilliseconds, "0"), F(SecondsSince(_start), "0.0"),
                F(GcHeap / Megabyte, "0.0"), F(GcHeap / Megabyte, "0.0"), F(Mdk::WorkingSet() / Megabyte, "0.0")),
        };
    }

    std::string PerfLog::F(double value, const std::string& format)
    {
        // "0", "0.0", "0.00": as many decimals as zeros after the point, rounded half away from zero.
        auto point = Str::IndexOf(format, '.');
        auto decimals = point < 0 ? 0 : static_cast<int>(format.size()) - point - 1;
        auto scale = std::pow(10.0, decimals);
        return std::format("{:.{}f}", Math::RoundAwayFromZero(value * scale) / scale, decimals);
    }
}
