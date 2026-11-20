#pragma once

#include <functional>
#include <optional>

#include "Mdk.Core/TimeSpan.h"

namespace Mdk::Game::Flow
{
    // How far a level's loading is (0 to 1), for the loading screen's bar: redrawn at most
    // every Every, as each redraw waits for the display.
    class LoadProgress
    {
    public:
        static constexpr TimeSpan Every = std::chrono::milliseconds(100);

        LoadProgress(std::function<void(float)> draw, std::function<TimeSpan()> clock)
            : _draw(std::move(draw)), _clock(std::move(clock)) {}

        // Timed by the wall clock.
        explicit LoadProgress(std::function<void(float)> draw);

        void Report(float done);

    private:
        std::function<void(float)> _draw;
        std::function<TimeSpan()> _clock;
        std::optional<TimeSpan> _drawn;
    };
}
