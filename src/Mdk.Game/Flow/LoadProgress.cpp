#include "Mdk.Game/Flow/LoadProgress.h"

#include <algorithm>
#include <chrono>

namespace Mdk::Game::Flow
{
    LoadProgress::LoadProgress(std::function<void(float)> draw)
        : LoadProgress(std::move(draw), [start = std::chrono::steady_clock::now()]
            {
                return std::chrono::duration_cast<TimeSpan>(std::chrono::steady_clock::now() - start);
            })
    {
    }

    void LoadProgress::Report(float done)
    {
        auto now = _clock();
        if (_drawn.has_value() && now - *_drawn < Every)
        {
            return;
        }

        _drawn = now;
        _draw(std::clamp(done, 0.f, 1.f));
    }
}
