#include <chrono>
#include <thread>

#include <doctest.h>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Engine/Diagnostics/Profiler.h"
#include "Mdk.Game.Tests/Asserts.h"

using namespace Mdk;
using namespace Mdk::Engine::Diagnostics;

// The debug overlay's timings: sections of a frame, averaged over frames.

TEST_CASE("ProfilerTests.SectionsAddUpWithinAFrame")
{
    Profiler profiler;
    long long ticks = StopwatchFrequency / 1000;
    profiler.Add(Section::Scripts, ticks);
    profiler.Add(Section::Scripts, ticks);
    profiler.EndFrame();

    // The first frame sets the average.
    CHECK_PRECISION(2.0f, profiler.Milliseconds(Section::Scripts), 3);
    CHECK(profiler.Milliseconds(Section::Render) == 0.0f);
}

TEST_CASE("ProfilerTests.MeasureTimesItsScope")
{
    Profiler profiler;
    {
        auto measure = profiler.Measure(Section::Render);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    profiler.EndFrame();
    CHECK(profiler.Milliseconds(Section::Render) >= 4.0f);
}
