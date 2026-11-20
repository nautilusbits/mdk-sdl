#include <doctest.h>

#include "Mdk.Formats/Arena.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/Outlines.h"
#include "Shared/GameData.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Mdk;
using namespace Mdk::Game::Level;
using Mdk::Tests::GameData;

// Outlined edges of arena triangles (flag bit 23, edges bits 20-22; _add_outlines).
namespace
{
    constexpr uint V0V1 = 1u << 20;
    constexpr uint V1V2 = 1u << 21;
    constexpr uint V2V0 = 1u << 22;

    constexpr uint AllEdges = Outlines::Outlined | V0V1 | V1V2 | V2V0;

    // Level 7's DANT_7 glass column: a fan triangle (t6) flags its diagonal, as the editor gave
    // the fan the flags of a quad's half.
    const std::string Shaft = "DANT_7";
    constexpr int FanTriangle = 6;
    const std::pair<Vector3, Vector3> FanDiagonal{Vector3(406.f, 4450.f, -226.f), Vector3(427.f, 4429.f, -204.f)};

    const LevelData& Level7()
    {
        static LevelData level(*GameData::Data(), 7);
        return level;
    }

    Formats::Arena TwoTriangles(uint first, uint second)
    {
        Formats::Arena arena;
        arena.Vertices = {Vector3::Zero(), Vector3::UnitX(), Vector3::UnitY(), Vector3::UnitZ()};
        arena.TriangleIndices = {0, 1, 2, 1, 2, 3};
        arena.TriangleFlags = {first, second};
        return arena;
    }

    // A unit square in z = 0, split along v1-v2, both halves flagging every edge.
    Formats::Arena Square()
    {
        Formats::Arena arena;
        arena.Vertices = {Vector3::Zero(), Vector3::UnitX(), Vector3::UnitY(), Vector3(1.f, 1.f, 0.f)};
        arena.TriangleIndices = {0, 1, 2, 2, 1, 3};
        arena.TriangleFlags = {AllEdges, AllEdges};
        return arena;
    }

    bool HasLine(const std::vector<Vector3>& lines, const std::pair<Vector3, Vector3>& line)
    {
        for (size_t i = 0; i < lines.size(); i += 2)
        {
            if ((lines[i] == line.first && lines[i + 1] == line.second) || (lines[i] == line.second && lines[i + 1] == line.first))
            {
                return true;
            }
        }

        return false;
    }

    const std::vector<int> Both{0, 1};
}

TEST_CASE("OutlineTests.EdgeBitsPickTheEdgesOfOutlinedTriangles")
{
    auto arena = TwoTriangles(Outlines::Outlined | V0V1 | V2V0, Outlines::Outlined | V1V2);
    auto lines = Outlines::Of(arena, Both);
    std::vector<Vector3> expected{Vector3::Zero(), Vector3::UnitX(), Vector3::UnitY(), Vector3::Zero(), Vector3::UnitY(), Vector3::UnitZ()};
    CHECK(lines == expected);
}

TEST_CASE("OutlineTests.EdgeBitsWithoutTheOutlineFlagDrawNothing")
{
    auto arena = TwoTriangles(V0V1 | V1V2 | V2V0, 0);
    CHECK(Outlines::Of(arena, Both).empty());
}

TEST_CASE("OutlineTests.OnlyTheGivenTrianglesCount")
{
    auto arena = TwoTriangles(Outlines::Outlined | V0V1, Outlines::Outlined | V0V1);
    std::vector<int> second{1};
    CHECK(Outlines::Of(arena, second).size() == 2);
}

TEST_CASE("OutlineTests.FramesDropEdgesInsideAFlatSurface")
{
    auto arena = Square();
    CHECK(Outlines::Of(arena, Both).size() == 12);
    CHECK(Outlines::Of(arena, Both, OutlineEdges::Frame).size() == 8);
}

TEST_CASE("OutlineTests.FramesKeepCorners")
{
    // The two triangles share v1-v2 at an angle: a corner, not a diagonal.
    auto arena = TwoTriangles(Outlines::Outlined | V1V2, Outlines::Outlined | V0V1);
    CHECK(Outlines::Of(arena, Both, OutlineEdges::Frame).size() == 4);
}

DATA_TEST_CASE("OutlineTests.ColumnFramesSkipTheFanDiagonal")
{
    std::shared_ptr<Formats::Arena> arena;
    for (auto& a : Level7().Arenas)
    {
        if (a->Name == Shaft)
        {
            arena = a;
            break;
        }
    }

    REQUIRE(arena != nullptr);
    std::vector<int> fan{FanTriangle};
    CHECK(HasLine(Outlines::Of(*arena, fan), FanDiagonal));

    std::vector<int> glass;
    for (int t = 0; t < arena->TriangleCount(); t++)
    {
        if (arena->TriangleMaterials[t] == arena->TriangleMaterials[FanTriangle])
        {
            glass.push_back(t);
        }
    }

    CHECK_FALSE(HasLine(Outlines::Of(*arena, glass, OutlineEdges::Frame), FanDiagonal));
}
