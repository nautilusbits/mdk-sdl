#include <doctest.h>

#include "formats/Arena.h"
#include "formats/Mto.h"
#include "game/level/Layers.h"
#include "shared/GameData.h"

#include <array>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

using namespace Mdk;
using namespace Mdk::Game::Level;
using Mdk::Tests::GameData;

// Depth layers of coplanar arena triangles (details drawn over the surfaces they lie on).
namespace
{
    // Level 6's OLYM_5: a hub's rim (159) lies half on the glass floor around it (115).
    const std::string Hubs = "OLYM_5";
    constexpr int HubRim = 159;
    constexpr int GlassFloor = 115;

    // Level 6's OLYM_7: a dark floor triangle (2) under a mirror tile (56).
    const std::string Mirrors = "OLYM_7";
    constexpr int DarkFloor = 2;
    constexpr int MirrorTile = 56;

    using Triangle = std::array<Vector2, 3>;

    // Level 6's arenas, each parsed when asked for.
    const std::shared_ptr<Formats::Mto>& Level6()
    {
        static auto mto = Formats::Mto::Load(GameData::Data()->PathOf("TRAVERSE/LEVEL6/LEVEL6O.MTO"));
        return mto;
    }

    // Triangles in z = 0 (three corners each, wound as MDK's floors).
    Formats::Arena Flat(std::initializer_list<Triangle> triangles)
    {
        Formats::Arena arena;
        int index = 0;
        for (auto& t : triangles)
        {
            for (auto c : t)
            {
                arena.Vertices.push_back(Vector3(c, 0.f));
                arena.TriangleIndices.push_back(index++);
            }
        }

        arena.TriangleMaterials.assign(triangles.size(), 0);
        arena.TriangleFlags.assign(triangles.size(), 0u);
        return arena;
    }

    const Triangle Wall{Vector2(0.f, 0.f), Vector2(0.f, 10.f), Vector2(10.f, 0.f)};
}

TEST_CASE("LayerTests.APosterOnAWallIsOneLayerUp")
{
    auto arena = Flat({Wall, {Vector2(1.f, 1.f), Vector2(1.f, 2.f), Vector2(2.f, 1.f)}});
    CHECK(Layers::Of(arena) == std::vector<int>{0, 1});
}

TEST_CASE("LayerTests.APosterHalfOnAWallIsOneLayerUp")
{
    // Its centre (9, 1.3) is off the wall, a corner on it.
    auto arena = Flat({Wall, {Vector2(7.f, 1.f), Vector2(7.f, 2.f), Vector2(13.f, 1.f)}});
    CHECK(Layers::Of(arena) == std::vector<int>{0, 1});
}

TEST_CASE("LayerTests.NeighboursSharingAnEdgeStayFlat")
{
    auto arena = Flat({Wall, {Vector2(0.f, 10.f), Vector2(10.f, 10.f), Vector2(10.f, 0.f)}});
    CHECK(Layers::Of(arena) == std::vector<int>{0, 0});
}

TEST_CASE("LayerTests.APosterOnAPosterIsTwoLayersUp")
{
    auto arena = Flat({Wall, {Vector2(1.f, 1.f), Vector2(1.f, 5.f), Vector2(5.f, 1.f)}, {Vector2(1.5f, 1.5f), Vector2(1.5f, 2.f), Vector2(2.f, 1.5f)}});
    CHECK(Layers::Of(arena) == std::vector<int>{0, 1, 2});
}

TEST_CASE("LayerTests.OfTwoEqualTrianglesTheLaterIsOnTop")
{
    auto arena = Flat({Wall, Wall});
    CHECK(Layers::Of(arena) == std::vector<int>{0, 1});
}

TEST_CASE("LayerTests.OtherPlanesStayFlat")
{
    auto arena = Flat({Wall, {Vector2(1.f, 1.f), Vector2(1.f, 2.f), Vector2(2.f, 1.f)}});
    arena.Vertices[3].Z = arena.Vertices[4].Z = arena.Vertices[5].Z = 1.f;
    CHECK(Layers::Of(arena) == std::vector<int>{0, 0});
}

DATA_TEST_CASE("LayerTests.AHubRimHalfOnGlassIsOverIt")
{
    auto arena = Level6()->GetArena(Hubs);
    auto layers = Layers::Of(*arena);
    CHECK(layers[HubRim] > layers[GlassFloor]);
}

DATA_TEST_CASE("LayerTests.AFloorHalfUnderAMirrorTileIsOverIt")
{
    auto arena = Level6()->GetArena(Mirrors);
    auto layers = Layers::Of(*arena);
    CHECK(layers[DarkFloor] > layers[MirrorTile]);
}
