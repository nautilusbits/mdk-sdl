#pragma once

// System.TimeSpan: a count of 100 ns ticks.
//   TimeSpan::FromTicks(n) → TimeSpan(n);  span.Ticks → span.count();  TimeSpan.Zero → TimeSpan::zero()

#include <chrono>

namespace Mdk
{
    using TimeSpan = std::chrono::duration<long long, std::ratio<1, 10'000'000>>;

    constexpr long long TicksPerSecond = 10'000'000;
    constexpr long long TicksPerMillisecond = 10'000;
}
