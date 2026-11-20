#pragma once

#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Formats/ModelAnimation.h"
#include "Mdk.Formats/Texture.h"
#include "Mdk.Formats/TextureArchive.h"
#include "Mdk.Game/Fall/FallPalette.h"

namespace Mdk::Game::Fall
{
    // The fall's models (FALL3D.BNI, without the arena models' flags word) in the
    // textures of FALL3D_n.MTI and the FALLPn palette, both faces drawn: a mesh per
    // pose (model, animation, frame), built once. Flat colours follow the palette's effects.
    //   (model, animation, frame) ──► cached mesh + batches per surface ──(world matrix)──► renderer
    class FallModels
    {
    public:
        FallModels(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Bni> bni, std::shared_ptr<Formats::TextureArchive> mti,
            std::shared_ptr<FallPalette> palette, int paletteId);

        // A model animation of the BNI.
        std::shared_ptr<Formats::ModelAnimation> Animation(const std::string& name);

        // Draws a model at rest, or in a frame of an animation; textureFrame picks an animated
        // texture's frame, pass how it meets the scene.
        void Draw(const std::string& name, Matrix4x4 world, const std::string& animation = "", int frame = 0, int textureFrame = 0,
            Engine::Render::Pass pass = Engine::Render::Pass::DoubleSided);

        // A model of FALL3D.BNI (its data: the trails' ends), or null.
        std::shared_ptr<Formats::Model> Find(const std::string& name) { return Get(name); }

    private:
        // Material values from 256 are special (glass, mirrors): not in the fall.
        static constexpr int SpecialFirst = 256;
        static constexpr const char* PenPrefix = "PEN_";
        // A textured batch's palette colour: none.
        static constexpr int NoPen = -1;

        // Triangles of a surface; Pen is a flat colour's palette index.
        struct Batch
        {
            int First = 0;
            int Count = 0;
            Engine::Render::Material Material;
            int Pen = 0;

            bool operator==(const Batch&) const = default;
        };

        struct PoseMesh
        {
            int Mesh = 0;
            std::vector<Batch> Batches;
        };

        // A triangle's surface, its texture (null when flat) and its pen.
        struct Surface
        {
            Engine::Render::Material Material;
            std::shared_ptr<Formats::Texture> Texture;
            int Pen = 0;
        };

        // Models with named parts (bit 7 of the table 0x490ca4).
        static const std::unordered_set<std::string>& NamedParts();

        Engine::Render::Renderer& _renderer;
        std::shared_ptr<Formats::Bni> _bni;
        std::shared_ptr<Formats::TextureArchive> _mti;
        std::shared_ptr<FallPalette> _palette;
        int _paletteId;

        std::unordered_map<std::string, std::shared_ptr<Formats::Model>> _models;
        std::unordered_map<std::string, std::shared_ptr<Formats::ModelAnimation>> _animations;
        std::unordered_map<std::string, std::vector<std::vector<std::vector<Vector3>>>> _baked;
        std::map<std::tuple<std::string, std::string, int>, std::optional<PoseMesh>> _meshes;
        // Keyed by identity (C# reference keys).
        std::unordered_map<const Formats::Texture*, int> _textures;

        std::shared_ptr<Formats::Model> Get(const std::string& name);

        std::optional<PoseMesh> Build(const std::string& name, const std::string& animation, int frame);

        // A triangle's surface: a texture of the MTI, or a palette colour (negative values, the
        // MTI's colours, PEN_n); null when it isn't drawn.
        std::optional<Surface> Resolve(int value, std::span<const std::string> names);

        std::optional<Surface> Colour(int index);

        int TextureId(const std::shared_ptr<Formats::Texture>& texture);
    };
}
