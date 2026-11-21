#pragma once

#include <array>
#include <string>
#include <tuple>
#include <vector>

#include "core/TimeSpan.h"
#include "engine/diagnostics/Profiler.h"
#include "engine/render/Renderer.h"

namespace Mdk::Game::DevTools
{
    // A test run's frame costs (--perf): after `warmup` seconds of game time, each frame's sections,
    // GPU wait, managed allocations and the collections, summed for a report.
    //   Perf: 600 frames, 2.10 ms (476 FPS): scene 0.31 render 1.20 (GPU wait 0.80) physics 0.05 ...
    //   Perf: allocated 0 B/frame (max 0), collections 0/0/0 in 1.3 s, load 850 ms, ...
    class PerfLog
    {
    public:
        explicit PerfLog(float warmup);

        // A level's loading time (the viewer's creation).
        void Loaded(TimeSpan time);

        // A frame has ended at `time` (game seconds of the screen).
        void Frame(float time, const Engine::Diagnostics::Profiler& profiler, Engine::Render::RenderStats stats);

        // The report's lines (none before a measured frame).
        std::vector<std::string> Report();

    private:
        // A frame waiting to be counted: its allocations, its time and its draws.
        struct Pending
        {
            long long Allocated = 0;
            float Frame = 0;
            Engine::Render::RenderStats Stats;
        };

        static constexpr double Megabyte = 1024.0 * 1024.0;
        // Enum.GetValues<Section>().Length.
        static constexpr int Sections = 5;
        // GC.MaxGeneration + 1 (C++ has no collections: they stay 0).
        static constexpr int Generations = 3;

        float _warmup;
        std::array<double, Sections> _sections{};
        std::array<int, Generations> _collections{};
        long long _start;
        long long _windowStart = 0;
        long long _allocated = -1;
        long long _allocatedSum = 0;
        long long _allocatedMax = 0;
        double _frameSum = 0;
        double _gpuWait = 0;
        int _count = 0;
        double _loadMilliseconds = 0;
        Pending _pending;
        std::array<float, Sections> _pendingSections{};
        Engine::Render::RenderStats _draws;

        void Count(const Pending& frame);

        static std::string Ms(double value) { return F(value, "0.00"); }

        static std::string F(double value, const std::string& format);
    };
}
