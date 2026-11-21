#include <string>
#include <vector>

#include <doctest.h>

#include "core/Numerics.h"
#include "formats/TextureArchive.h"
#include "game/level/LevelData.h"
#include "game/scripts/Ribbon.h"
#include "shared/GameData.h"

using namespace Mdk;
using namespace Mdk::Game::Level;
using namespace Mdk::Game::Scripts;
using Mdk::Tests::GameData;

// The twisters' ribbon (0x4392a4, 0x439690).
namespace
{
    std::vector<float> ScalesOf(int count)
    {
        std::vector<float> scales;
        for (int i = 0; i < count; i++)
        {
            scales.push_back(Ribbon::ScaleOf(i, count));
        }

        return scales;
    }
}

TEST_CASE("RibbonTests.OnlyTheNewestTwoEdgesKeepTheirSize")
{
    // Integer division: (i + 1 + 5 - n) / 4.
    CHECK(ScalesOf(5) == std::vector<float>{0.0f, 0.0f, 0.0f, 1.0f, 1.0f});
    CHECK(ScalesOf(3) == std::vector<float>{0.0f, 1.0f, 1.0f});
}

TEST_CASE("RibbonTests.KeepsTheLastFivePositions")
{
    Ribbon ribbon;
    for (int i = 0; i < 7; i++)
    {
        ribbon.Push(Vector3::UnitX() * static_cast<float>(i));
    }

    CHECK(ribbon.Centres().size() == Ribbon::Length);
    CHECK(ribbon.Centres().front() == Vector3::UnitX() * 2.0f);
}

TEST_CASE("RibbonTests.TwoQuadsJoinTheLastEdges")
{
    Ribbon ribbon;
    ribbon.Push(Vector3::Zero());
    CHECK(ribbon.Triangles().empty());

    ribbon.Push(Vector3::UnitX());
    auto corners = ribbon.Triangles();

    // One segment: two triangles, from the bottom of the old edge, rows 0.5-16 of the texture.
    REQUIRE(corners.size() == 6);
    CHECK(corners[0] == Ribbon::Corner{-Vector3::UnitZ(), Vector2(0.5f, 0.5f)});
    CHECK(corners[2] == Ribbon::Corner{Vector3::UnitX() + Vector3::UnitZ(), Vector2(15.5f, 16.0f)});
}

DATA_TEST_CASE("RibbonTests.EveryLevelHasTheRibbonTexture")
{
    auto& data = *GameData::Data();
    for (int number = 3; number < 3 + 6; number++)
    {
        CAPTURE(number);
        CHECK(LevelData(data, number).LevelTextures->Textures.ContainsKey(std::string(Ribbon::Texture)));
    }
}
