#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <doctest.h>

#include "core/Numerics.h"
#include "formats/Glb.h"
#include "formats/Model.h"
#include "formats/Palette.h"
#include "formats/Texture.h"
#include "formats/TextureArchive.h"
#include "game/mods/ModelExport.h"
#include "game/mods/ModelSwap.h"
#include "game/mods/PartFit.h"

using namespace Mdk;
using namespace Mdk::Formats;
using namespace Mdk::Game::Mods;

// Mods' models: parts replaced by name, glTF's axes, a replaced part following its original's
// pose (PartFit), and the export reading back as it was.
namespace
{
    constexpr float Tolerance = 1e-3f;

    // A box-ish part: a tetrahedron's corners.
    const std::vector<Vector3> Tetrahedron = {Vector3(0, 0, 0), Vector3(2, 0, 0), Vector3(0, 3, 0), Vector3(0, 0, 4)};
    const std::vector<int> TetrahedronTriangles = {0, 1, 2, 0, 1, 3, 0, 2, 3, 1, 2, 3};

    std::string Text(Vector3 v)
    {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "<%.9g, %.9g, %.9g>", v.X, v.Y, v.Z);
        return buffer;
    }

    std::string Text(Vector2 v)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "<%.9g, %.9g>", v.X, v.Y);
        return buffer;
    }

    void Near(Vector3 expected, Vector3 actual)
    {
        INFO("expected " << Text(expected) << ", got " << Text(actual));
        CHECK(Vector3::Distance(expected, actual) < Tolerance);
    }

    std::vector<Vector3> Transformed(const std::vector<Vector3>& points, const Matrix4x4& m)
    {
        std::vector<Vector3> result;
        for (const auto& v : points)
        {
            result.push_back(Vector3::Transform(v, m));
        }

        return result;
    }

    std::shared_ptr<Model> Grunt()
    {
        auto model = std::make_shared<Model>();
        model->Name = "GRUNT";
        model->Materials = {"SKIN", "CLOTH"};
        for (const char* name : {"BODY", "HEAD", "ARM", "ARM"})
        {
            auto part = std::make_shared<Model::Part>();
            part->Name = name;
            part->Vertices = Tetrahedron;
            part->TriangleIndices = TetrahedronTriangles;
            part->TriangleMaterials = {0, 0, 1, 1};
            for (int i = 0; i < 12; i++)
            {
                part->TriangleUvs.push_back(Vector2(static_cast<float>(i % 4), static_cast<float>(i / 4)));
            }

            model->PartList.push_back(part);
        }

        return model;
    }

    Glb::Mesh Node(const std::string& name)
    {
        Glb::Primitive primitive{{Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(0, 1, 0)},
            {Vector2::Zero(), Vector2::UnitX(), Vector2::UnitY()}, {0, 1, 2}, 0};
        return {name, {primitive}};
    }

    // Triangles' corners (position, UV) in a comparable order (-0 as 0).
    std::vector<std::string> Corners(const std::vector<Vector3>& positions, const std::vector<Vector2>& uvs)
    {
        std::vector<std::string> triangles;
        size_t count = std::min(positions.size(), uvs.size());
        for (size_t t = 0; t < count; t += 3)
        {
            std::string triangle;
            for (size_t c = t; c < std::min(t + 3, count); c++)
            {
                if (c != t)
                {
                    triangle += ";";
                }

                triangle += Text(positions[c] + Vector3::Zero()) + " " + Text(uvs[c]);
            }

            triangles.push_back(triangle);
        }

        std::sort(triangles.begin(), triangles.end());
        return triangles;
    }
}

// --- Poses -------------------------------------------------------------------------------

TEST_CASE("ModelSwapTests.RestPoseIsIdentity")
{
    auto fit = PartFit(Tetrahedron, TetrahedronTriangles).Fit(Tetrahedron);

    Near(Vector3(5, 6, 7), Vector3::Transform(Vector3(5, 6, 7), fit));
}

// A rigid move (a matrix track): any point of the replacement moves as the part.
TEST_CASE("ModelSwapTests.RigidMoveIsExact")
{
    auto move = Matrix4x4::CreateRotationZ(0.7f) * Matrix4x4::CreateRotationX(-0.3f) * Matrix4x4::CreateTranslation(10, -4, 2);
    auto posed = Transformed(Tetrahedron, move);

    auto fit = PartFit(Tetrahedron, TetrahedronTriangles).Fit(posed);

    Near(Vector3::Transform(Vector3(1, 1, 1), move), Vector3::Transform(Vector3(1, 1, 1), fit));
    Near(Vector3::Transform(Vector3(-3, 8, 0), move), Vector3::Transform(Vector3(-3, 8, 0), fit));
}

// A flat part (a door panel) still turns: its triangles' apexes leave the plane.
TEST_CASE("ModelSwapTests.FlatPartTurns")
{
    std::vector<Vector3> square = {Vector3(0, 0, 0), Vector3(4, 0, 0), Vector3(4, 4, 0), Vector3(0, 4, 0)};
    std::vector<int> triangles = {0, 1, 2, 0, 2, 3};
    auto turn = Matrix4x4::CreateRotationY(1.2f) * Matrix4x4::CreateTranslation(0, 0, 5);

    auto fit = PartFit(square, triangles).Fit(Transformed(square, turn));

    Near(Vector3::Transform(Vector3(2, 2, 1), turn), Vector3::Transform(Vector3(2, 2, 1), fit));
}

// A part of one point only moves.
TEST_CASE("ModelSwapTests.PointOnlyMoves")
{
    std::vector<Vector3> posed = {Vector3(3, 1, 1)};
    auto fit = PartFit({Vector3(1, 1, 1)}, {}).Fit(posed);

    Near(Vector3(7, 5, 5), Vector3::Transform(Vector3(5, 5, 5), fit));
}

// --- Parts by name -----------------------------------------------------------------------

TEST_CASE("ModelSwapTests.NodesReplaceTheirParts")
{
    Glb::Scene scene{{Node("head"), Node("TAIL"), Node("ARM"), Node("ARM"), Node("ARM")}, {Glb::Material{"M", Vector4::One(), std::nullopt}}};
    std::vector<std::string> unknown;

    auto swap = ModelSwap::Of(scene, *Grunt(), unknown);

    REQUIRE(swap != nullptr);
    CHECK_FALSE(swap->Parts[0].has_value());
    CHECK(swap->Parts[1].has_value());
    CHECK(swap->Parts[2].has_value());
    CHECK(swap->Parts[3].has_value());
    // No such part, and a third ARM.
    CHECK(unknown == std::vector<std::string>{"TAIL", "ARM"});
}

TEST_CASE("ModelSwapTests.NoPartNoSwap")
{
    std::vector<std::string> unknown;
    CHECK(ModelSwap::Of(Glb::Scene{{Node("WING")}, {}}, *Grunt(), unknown) == nullptr);
}

// A model of one unnamed part takes the node of the model's name.
TEST_CASE("ModelSwapTests.UnnamedPartIsTheModel")
{
    Model model;
    model.Name = "BARREL";
    auto part = std::make_shared<Model::Part>();
    part->Vertices = Tetrahedron;
    part->TriangleIndices = TetrahedronTriangles;
    model.PartList = {part};

    CHECK(ModelSwap::PartName(model, 0) == "BARREL");
    std::vector<std::string> unknown;
    auto swap = ModelSwap::Of(Glb::Scene{{Node("barrel")}, {}}, model, unknown);
    REQUIRE(swap != nullptr);
    CHECK(swap->Parts[0].has_value());
}

// glTF is Y up, MDK Z up: Blender shows MDK's up as up.
TEST_CASE("ModelSwapTests.AxesTurn")
{
    CHECK(ModelSwap::ToMdk(Vector3(1, 2, 3)) == Vector3(1, -3, 2));
    CHECK(ModelSwap::ToMdk(ModelSwap::ToGltf(Vector3(1, 2, 3))) == Vector3(1, 2, 3));
}

// --- Export round trip -------------------------------------------------------------------

// An exported model read back: every part's triangles (corners, UVs as 0-1), its texture's
// image and colour; the replacement at rest is the original.
TEST_CASE("ModelSwapTests.ExportReadsBack")
{
    auto model = Grunt();
    auto archive = std::make_shared<TextureArchive>();
    auto skinTexture = std::make_shared<Texture>();
    skinTexture->Name = "SKIN";
    skinTexture->Width = 4;
    skinTexture->Height = 3;
    for (int i = 0; i < 12; i++)
    {
        skinTexture->Indices.push_back(static_cast<byte>(i));
    }

    archive->Textures["SKIN"] = skinTexture;
    archive->Colors["CLOTH"] = 5;
    Palette palette;
    for (int i = 0; i < Palette::Size * 4; i++)
    {
        palette.Rgba[i] = static_cast<byte>(i * 7);
    }

    auto bytes = Glb::Write(ModelExport::Scene(*model, palette, {archive}));
    std::vector<std::string> unknown;
    auto swap = ModelSwap::Of(Glb::Read(bytes), *model, unknown);

    REQUIRE(swap != nullptr);
    for (int p = 0; p < static_cast<int>(model->PartList.size()); p++)
    {
        const auto& part = *model->PartList[p];
        REQUIRE(swap->Parts[p].has_value());
        const auto& mesh = *swap->Parts[p];

        std::vector<Vector3> originalPositions;
        for (int i : part.TriangleIndices)
        {
            originalPositions.push_back(part.Vertices[i]);
        }

        std::vector<Vector2> originalUvs;
        for (size_t i = 0; i < part.TriangleUvs.size(); i++)
        {
            originalUvs.push_back(part.TriangleMaterials[i / 3] == 0 ? part.TriangleUvs[i] / Vector2(4, 3) : Vector2::Zero());
        }

        std::vector<Vector3> readPositions;
        std::vector<Vector2> readUvs;
        for (int i : mesh.Triangles)
        {
            readPositions.push_back(mesh.Vertices[i]);
            readUvs.push_back(mesh.Uvs[i]);
        }

        CHECK(Corners(originalPositions, originalUvs) == Corners(readPositions, readUvs));

        swap->Pose(model->RestPose(), 0);
        Near(mesh.Vertices[0], swap->Vertex(p, 0));
    }

    const ModelSwap::Surface* skin = nullptr;
    const ModelSwap::Surface* cloth = nullptr;
    int skins = 0;
    int cloths = 0;
    for (const auto& s : swap->Surfaces)
    {
        if (s.Name == "SKIN")
        {
            skin = &s;
            skins++;
        }

        if (s.Name == "CLOTH")
        {
            cloth = &s;
            cloths++;
        }
    }

    REQUIRE(skins == 1);
    REQUIRE(skin->Image.has_value());
    CHECK(skin->Image->Width == 4);
    CHECK(skin->Image->Height == 3);
    // Index 5 of the palette: 140, 147, 154.
    std::vector<byte> pixel(skin->Image->Rgba.begin() + 5 * 4, skin->Image->Rgba.begin() + 5 * 4 + 4);
    CHECK(pixel == std::vector<byte>{140, 147, 154, 255});
    REQUIRE(cloths == 1);
    CHECK_FALSE(cloth->Image.has_value());
    Near(Vector3(140, 147, 154) / 255.f, Vector3(cloth->Colour.X, cloth->Colour.Y, cloth->Colour.Z));
}
