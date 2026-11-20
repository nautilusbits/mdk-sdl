#include <doctest.h>

#include "Mdk.Game/Flow/LoadProgress.h"

#include <chrono>
#include <vector>

using namespace Mdk;
using namespace Mdk::Game::Flow;

// The loading screen's bar: redrawn as the level loads, not on every step.

TEST_CASE("LoadProgressTests.RedrawsAtMostEveryTenthOfASecond")
{
    TimeSpan now = TimeSpan::zero();
    std::vector<float> drawn;
    LoadProgress progress([&](float done) { drawn.push_back(done); }, [&] { return now; });

    progress.Report(0.1f);
    now += std::chrono::milliseconds(50);
    progress.Report(0.2f);
    now += std::chrono::milliseconds(60);
    progress.Report(0.3f);

    CHECK(drawn == std::vector<float>{0.1f, 0.3f});
}
