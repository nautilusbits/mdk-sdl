#include <doctest.h>

#include "game/menu/HeadsView.h"

#include <algorithm>
#include <vector>

using namespace Mdk;
using namespace Mdk::Game::Menu;

// The Score-O-matic's heads (0x433268): one row below 5, else two rows of (count + 1) / 2.

TEST_CASE("HeadsTests.FewHeadsMakeOneRow")
{
    auto places = HeadsView::Places(3, 3);

    std::vector<float> xs;
    for (const auto& p : places)
    {
        xs.push_back(p.X);
    }
    CHECK(xs == std::vector<float>{-5.0f, 0.0f, 5.0f});

    for (const auto& p : places)
    {
        CHECK(p.Z == -3.0f);
    }
}

TEST_CASE("HeadsTests.ManyHeadsMakeTwoRowsOfAtMostEight")
{
    auto places = HeadsView::Places(20, 20);
    CHECK(places.size() == 16);
    CHECK(std::count_if(places.begin(), places.end(), [](const Vector3& p) { return p.Z == -4.5f; }) == 8);
}
