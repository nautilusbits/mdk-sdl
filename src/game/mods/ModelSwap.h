#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/DotNet.h"
#include "core/Numerics.h"
#include "formats/Glb.h"
#include "formats/Model.h"
#include "game/hd_textures/HdImage.h"
#include "game/mods/PartFit.h"

namespace Mdk::Game::Mods
{
    // A mod's replacement of a model's parts, from a glTF file (Glb): each mesh node named as a part
    // (case ignored; a model of one unnamed part: the model's name) replaces that part's look; parts
    // without a node keep theirs. The original geometry still collides, is hit and animates: a
    // replaced part follows its original through PartFit.
    //   glTF (Y up)  x, y, z  ─►  MDK (Z up)  x, −z, y        1 unit = 1 MDK unit, the model's rest pose
    //   node "HEAD" ─► part HEAD: its triangles, UVs (0-1), materials
    //   material: its PNG ─► a mod image │ none, named as an original (WALL1, PEN_12, GLASS1) ─► that │ else its colour
    class ModelSwap
    {
    public:
        // A replacement part in the model's space (rest pose): triangles of vertices with UVs, a material each.
        struct Mesh
        {
            std::vector<Vector3> Vertices;
            std::vector<Vector2> Uvs;
            std::vector<int> Triangles;
            std::vector<int> Materials;

            bool operator==(const Mesh&) const = default;
        };

        // A replacement material: its name, colour (sRGB) and image (premultiplied), if any.
        struct Surface
        {
            std::string Name;
            Vector4 Colour;
            std::optional<HdTextures::HdImage> Image;

            bool operator==(const Surface&) const = default;
        };

        // Per original part, its replacement (null: kept).
        std::vector<std::optional<Mesh>> Parts;
        std::vector<Surface> Surfaces;

        // A glTF point (Y up) in MDK coordinates (Z up).
        static Vector3 ToMdk(Vector3 gltf) { return Vector3(gltf.X, -gltf.Z, gltf.Y); }

        // An MDK point in glTF coordinates.
        static Vector3 ToGltf(Vector3 mdk) { return Vector3(mdk.X, mdk.Z, -mdk.Y); }

        // A part's name in glTF files: its own, or the model's for a model of one unnamed part.
        static std::string PartName(const Formats::Model& model, int part)
        {
            return !model.PartList[part]->Name.empty() ? model.PartList[part]->Name : model.Name;
        }

        // The replacement a scene makes of a model; null when no node names a part. Nodes naming
        // no part are listed in `unknown`.
        static std::shared_ptr<ModelSwap> Of(const Formats::Glb::Scene& scene, const Formats::Model& model,
            std::vector<std::string>& unknown);

        // glTF's base colour is linear; the game's colours sRGB.
        static Vector4 ToSrgb(Vector4 linear);

        static Vector4 ToLinear(Vector4 srgb);

        // Fits each replaced, shown part to its pose (a draw; no allocation).
        void Pose(const std::vector<std::vector<Vector3>>& pose, int hidden);

        // A replaced part's vertex in the pose last fitted.
        Vector3 Vertex(int part, int index) const { return Vector3::Transform(Parts[part]->Vertices[index], _poses[part]); }

        ModelSwap(const Formats::Model& model, std::vector<std::optional<Mesh>> parts, std::vector<Surface> surfaces);

    private:
        static constexpr float Gamma = 2.2f;
        static constexpr int Corners = 3;

        std::vector<std::shared_ptr<PartFit>> _fits;
        // The draw's transform of each replaced part (scratch).
        std::vector<Matrix4x4> _poses;

        // A node's primitives as one mesh, in MDK coordinates; no material: a white one at the end.
        static Mesh Merge(const Formats::Glb::Mesh& mesh);

        // A material's PNG for the renderer, or null (broken: its colour instead).
        static std::optional<HdTextures::HdImage> Decode(std::span<const byte> png);
    };
}
