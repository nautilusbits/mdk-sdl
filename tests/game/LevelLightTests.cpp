#include <doctest.h>

#include "engine/render/Renderer.h"
#include "formats/Arena.h"
#include "formats/Dti.h"
#include "formats/Palette.h"
#include "formats/Texture.h"
#include "game/level/ArenaShape.h"
#include "game/level/EnhancedLook.h"
#include "game/level/LevelData.h"
#include "game/level/SkyLight.h"
#include "shared/GameData.h"
#include "Asserts.h"

#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Mdk;
using namespace Mdk::Engine::Render;
using namespace Mdk::Formats;
using namespace Mdk::Game::Level;

// The enhanced look's light per level and arena: the sky's colours (SkyLight), the roof over
// an arena (ArenaShape) and the light made of them (EnhancedLook).
namespace
{
    constexpr float Tolerance = 1e-3f;
    constexpr byte Red = 1;
    constexpr byte Blue = 2;
    constexpr byte White = 3;
    constexpr int Width = 4;
    constexpr int Extra = 4;
    constexpr int Rows = 4;
    constexpr byte ByteMax = std::numeric_limits<byte>::max();

    std::shared_ptr<Palette> Colours()
    {
        std::vector<byte> rgb(Palette::Size * 3);
        rgb[Red * 3] = ByteMax;
        rgb[Blue * 3 + 2] = ByteMax;
        rgb[White * 3] = rgb[White * 3 + 1] = rgb[White * 3 + 2] = ByteMax;
        return Palette::FromRgb(rgb);
    }

    // A panorama of Rows rows: above above the horizon, below from it down, its wrap columns white.
    std::shared_ptr<Dti> SkyOf(byte above, byte below, int horizon)
    {
        int width = Width + Extra;
        std::vector<byte> indices(width * Rows);
        for (int row = 0; row < Rows; row++)
        {
            for (int column = 0; column < width; column++)
            {
                indices[row * width + column] = column >= Width ? White : row < horizon ? above : below;
            }
        }

        auto dti = std::make_shared<Dti>();
        dti->Palette = Colours();
        dti->Sky = std::make_shared<Texture>();
        dti->Sky->Width = width;
        dti->Sky->Height = Rows;
        dti->Sky->Indices = indices;
        dti->SkyWrapWidth = Width;
        dti->SkyHorizonRow = horizon;
        dti->SkyTopColor = White;
        dti->SkyBottomColor = White;
        return dti;
    }

    void Near(Vector3 expected, Vector3 actual)
    {
        CHECK_NEAR(expected.X, actual.X, Tolerance);
        CHECK_NEAR(expected.Y, actual.Y, Tolerance);
        CHECK_NEAR(expected.Z, actual.Z, Tolerance);
    }

    using Triangle = std::vector<Vector3>;

    // Triangles as MDK winds them: the cross product of their edges points into the solid
    // (a floor's down).
    std::shared_ptr<Arena> ArenaOf(std::initializer_list<Triangle> triangles)
    {
        auto arena = std::make_shared<Arena>();
        for (const Triangle& triangle : triangles)
        {
            arena->Vertices.insert(arena->Vertices.end(), triangle.begin(), triangle.end());
        }

        for (int i = 0; i < static_cast<int>(arena->Vertices.size()); i++)
        {
            arena->TriangleIndices.push_back(i);
        }

        arena->TriangleMaterials = std::vector<int>(triangles.size());
        arena->TriangleFlags = std::vector<uint>(triangles.size());
        return arena;
    }

    const Triangle Floor{Vector3::Zero(), Vector3::UnitY(), Vector3::UnitX()};
    const Triangle Ceiling{Vector3::UnitZ(), Vector3::UnitZ() + Vector3::UnitX(), Vector3::UnitZ() + Vector3::UnitY()};
    const Triangle Wall{Vector3::Zero(), Vector3::UnitX(), Vector3::UnitZ()};
}

TEST_CASE("LevelLightTests.SkyAveragesAboveAndBelowTheHorizon")
{
    auto sky = SkyLight::Of(*SkyOf(Blue, Red, Rows / 2));

    Near(Vector3::UnitZ(), sky.Sky);
    Near(Vector3::UnitX(), sky.Ground);
}

TEST_CASE("LevelLightTests.SkyBeyondThePanoramaTakesItsColours")
{
    // The horizon above the panorama: nothing above it but the top colour.
    auto sky = SkyLight::Of(*SkyOf(Blue, Red, 0));

    Near(Vector3::One(), sky.Sky);
    Near(Vector3::UnitX(), sky.Ground);
}

TEST_CASE("LevelLightTests.SkyAveragesInLinearColour")
{
    // Half red, half blue rows above: half of each in linear light.
    auto dti = SkyOf(Blue, Red, Rows / 2);
    dti->Sky->Indices[0] = Red;
    dti->Sky->Indices[1] = Red;

    auto sky = SkyLight::Of(*dti);

    Near(Vector3(0.25f, 0.f, 0.75f), sky.Sky);
}

TEST_CASE("LevelLightTests.TintKeepsTheBrightness")
{
    for (float saturation : {0.f, 0.5f, 1.f})
    {
        CAPTURE(saturation);
        auto tint = SkyLight::Tint(Vector3(0.1f, 0.2f, 0.6f), saturation);

        CHECK_NEAR(1.f, SkyLight::Luminance(tint), Tolerance);
    }
}

TEST_CASE("LevelLightTests.TintOfBlackIsWhite")
{
    Near(Vector3::One(), SkyLight::Tint(Vector3::Zero(), 1.f));
    Near(Vector3::One(), SkyLight::Tint(Vector3::UnitZ(), 0.f));
}

TEST_CASE("LevelLightTests.FloorAloneIsOpen")
{
    CHECK(ArenaShape::CoverOf(*ArenaOf({Floor, Wall})) == Cover::Open);
}

TEST_CASE("LevelLightTests.FloorUnderACeilingIsCovered")
{
    CHECK(ArenaShape::CoverOf(*ArenaOf({Floor, Ceiling, Wall})) == Cover::Covered);
}

TEST_CASE("LevelLightTests.FacesAreMeanedByArea")
{
    // A floor and a wall of the same area: up 1 and 0.
    auto arena = ArenaOf({Floor, Wall});

    CHECK_NEAR(0.5f, ArenaShape::MeanUp(*arena), Tolerance);
    CHECK_NEAR(0.5f, ArenaShape::MeanFacing(*arena, -Vector3::UnitZ()), Tolerance);
}

DATA_TEST_CASE("LevelLightTests.LevelArenasAreCoveredOrOpen")
{
    struct Row { int Level; std::string Name; Cover Expected; };
    const Row rows[] = {
        {3, "HMO_1", Cover::Open},
        {3, "HMO_2", Cover::Covered},
        {4, "MEAT_3", Cover::Open},
        {4, "MEAT_1", Cover::Covered},
        {5, "MUSE_1", Cover::Covered},
        {6, "OLYM_5", Cover::Covered},
        {7, "DANT_1", Cover::Covered},
        {7, "DANT_5", Cover::Open},
        {7, "CDANT_1", Cover::Covered},
        {8, "GUNT_1", Cover::Open},
    };

    for (const Row& row : rows)
    {
        CAPTURE(row.Name);
        auto arena = LevelData(*Mdk::Tests::GameData::Data(), row.Level).ArenaNamed(row.Name);
        REQUIRE(arena != nullptr);

        CHECK(ArenaShape::CoverOf(*arena) == row.Expected);
    }
}

TEST_CASE("LevelLightTests.CoveredArenasHaveNoSun")
{
    auto light = EnhancedLook::Lighting(*SkyOf(Blue, Red, Rows / 2), *ArenaOf({Floor, Ceiling, Wall}));

    CHECK(light.Sun == Vector3::Zero());
    CHECK(light.Shadows == Shadows::Off);
}

TEST_CASE("LevelLightTests.OpenArenasHaveSunAndShadows")
{
    auto light = EnhancedLook::Lighting(*SkyOf(Blue, Red, Rows / 2), *ArenaOf({Floor, Wall}));

    CHECK(SkyLight::Luminance(light.Sun) > 0.f);
    CHECK(light.Shadows == Shadows::On);
}

TEST_CASE("LevelLightTests.LightFromAboveIsTheSkys")
{
    // A blue sky over red ground: blue from above, red from below.
    auto light = EnhancedLook::Lighting(*SkyOf(Blue, Red, Rows / 2), *ArenaOf({Floor, Wall}));

    CHECK(light.Sky.Z > light.Sky.X);
    CHECK(light.Ground.X > light.Ground.Z);
    CHECK(SkyLight::Luminance(light.Sky) > SkyLight::Luminance(light.Ground));
}

TEST_CASE("LevelLightTests.BrighterSkyMakesAStrongerSun")
{
    auto arena = ArenaOf({Floor, Wall});

    auto dark = EnhancedLook::Lighting(*SkyOf(Blue, Red, Rows / 2), *arena);
    auto bright = EnhancedLook::Lighting(*SkyOf(White, Red, Rows / 2), *arena);

    CHECK(SkyLight::Luminance(bright.Sun) > SkyLight::Luminance(dark.Sun));
}

TEST_CASE("LevelLightTests.ExposureEvensTheArenasMeanLight")
{
    for (Cover cover : {Cover::Open, Cover::Covered})
    {
        CAPTURE(static_cast<int>(cover));
        auto arena = cover == Cover::Open ? ArenaOf({Floor, Wall}) : ArenaOf({Floor, Ceiling, Wall});
        auto light = EnhancedLook::Lighting(*SkyOf(Blue, Red, Rows / 2), *arena);

        // The hemisphere's mean over the faces, plus the sun on its lit share.
        float up = ArenaShape::MeanUp(*arena) * 0.5f + 0.5f;
        Vector3 mean = Vector3::Lerp(light.Ground, light.Sky, up)
            + light.Sun * ArenaShape::MeanFacing(*arena, light.SunDirection) * EnhancedLook::SunlitShare;

        CHECK_NEAR(EnhancedLook::MeanLight, SkyLight::Luminance(mean) * light.Exposure, Tolerance);
    }
}
