#pragma once

#include <climits>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/TextureArchive.h"
#include "Mdk.Game/Level/MaterialResolver.h"
#include "Mdk.Game/Mods/ModelSwap.h"
#include "Mdk.Game/Objects/MdkObject.h"

namespace Mdk::Game::Mods
{
    class ModModels;
}

namespace Mdk::Game::Objects
{
    // Draws objects' models: each model's triangles laid out once by surface (per palette),
    // then every frame posed (animation frame, hidden parts) into the renderer's vertex stream and placed
    // by the object's transform. No mesh is built while playing. Models show both faces. The mods'
    // parts (ModelSwap: their own triangles) are fitted to the part's pose.
    //   model + palette ──once──► layout: batches by surface (part, vertex, UV per corner)
    //   object ──pose (shared frames)──► stream vertices ──(transform)──► renderer
    class ObjectView
    {
    public:
        // Palette and texture archives an arena's objects draw with.
        struct Look
        {
            std::shared_ptr<Formats::Palette> Palette;
            std::vector<std::shared_ptr<Formats::TextureArchive>> Archives;

            bool operator==(const Look&) const = default;
        };

        ObjectView(Engine::Render::Renderer& renderer, std::shared_ptr<Level::MaterialResolver> resolver);

        // The mods' model replacements (the enhanced look's), or null.
        std::shared_ptr<Mods::ModModels> Swaps;

        // Lays out a model with a look before it's drawn (a level's load).
        void Preload(const std::shared_ptr<Formats::Model>& model, const Look& look) { LayoutOf(model, look); }

        void Draw(MdkObject& obj, const Look& look) { Draw(obj, look, obj.Transform()); }

        void Draw(MdkObject& obj, const Look& look, Matrix4x4 transform) { Draw(obj, look, transform, obj.PoseParts()); }

        // Draws an object placed by transform in pose (a frame between ticks).
        void Draw(MdkObject& obj, const Look& look, Matrix4x4 transform, const MdkObject::Parts& pose);

    private:
        static constexpr int TriangleVertices = 3;
        // A batch with no visible triangle.
        static constexpr int Unseen = INT_MAX;

        // Where a batch's corners come from: the model's parts, or a mod's replacements.
        enum class Source { Model, Mod };

        // Whether an object shines by itself (an explosion) or is lit.
        enum class Glow { Lit, Shines };

        // A surface's corners in the model's order: their part, vertex and UV; per part, how
        // many corners and where its first triangle comes in the model (the batch's place when parts hide).
        struct Batch
        {
            Batch(Engine::Render::Material material, int parts, ObjectView::Source source)
                : Material(material), Source(source), PartCorners(parts), FirstTriangle(parts, Unseen) {}

            Engine::Render::Material Material;
            ObjectView::Source Source;
            std::vector<int> Parts;
            std::vector<int> Indices;
            std::vector<Vector2> Uvs;
            std::vector<int> PartCorners;
            std::vector<int> FirstTriangle;
        };

        // A model's batches, in the order of their first triangles, and the frame's scratch.
        struct Layout
        {
            explicit Layout(std::vector<std::shared_ptr<Batch>> batches)
                : Batches(std::move(batches)), Order(Batches.size()), Keys(Batches.size()) {}

            std::vector<std::shared_ptr<Batch>> Batches;
            std::vector<int> Order;
            std::vector<int> Keys;
        };

        struct BatchKeyHash
        {
            size_t operator()(const std::pair<Engine::Render::Material, Source>& key) const
            {
                return Level::MaterialHash()(key.first) * 31 + static_cast<size_t>(key.second);
            }
        };

        using BySurface = std::unordered_map<std::pair<Engine::Render::Material, Source>, std::shared_ptr<Batch>, BatchKeyHash>;

        Engine::Render::Renderer& _renderer;
        std::shared_ptr<Level::MaterialResolver> _resolver;

        // Keyed by identity (C# reference keys).
        std::map<std::pair<const Formats::Model*, const Formats::Palette*>, std::shared_ptr<Layout>> _layouts;
        std::map<std::pair<const Mods::ModelSwap*, const Formats::Palette*>, std::vector<std::optional<Engine::Render::Material>>> _surfaces;

        // An explosion's surfaces in the enhanced look shine unlit (their own point light
        // would wash them out) and cast no shadow.
        static Engine::Render::Material Shine(Engine::Render::Material material, Glow glow)
        {
            if (glow == Glow::Shines && material.Shading == Engine::Render::Shading::Lit)
            {
                material.Shading = Engine::Render::Shading::Sprite;
            }

            return material;
        }

        static bool IsHidden(int hidden, int part) { return (hidden & (1 << (part & 31))) != 0; }

        // Orders the batches with a visible triangle by their first one (as a mesh built from
        // the visible parts would); returns how many there are.
        static int Arrange(Layout& layout, int hidden);

        static int Visible(const Batch& batch, int hidden);

        Layout* LayoutOf(const std::shared_ptr<Formats::Model>& model, const Look& look);

        std::shared_ptr<Layout> Build(const Formats::Model& model, const Look& look);

        // A replaced part's triangles, in the part's place among the model's (its first triangle's).
        static void AddSwapped(int p, const Mods::ModelSwap::Mesh& mesh, const std::vector<std::optional<Engine::Render::Material>>& surfaces,
            int sequence, BySurface& bySurface, std::vector<std::shared_ptr<Batch>>& batches, int parts);

        // A replacement's materials through a look (made once): its image, else the original
        // material of its name, else its colour; the last is white (triangles without a material).
        const std::vector<std::optional<Engine::Render::Material>>& SurfacesOf(const Mods::ModelSwap& swap, const Look& look);
    };
}
