#include "Mdk.Game/Objects/ObjectView.h"

#include <algorithm>

#include "Mdk.Game/Mods/ModModels.h"

namespace Mdk::Game::Objects
{
    using Engine::Render::Material;
    using Engine::Render::Pass;
    using Engine::Render::Vertex;

    ObjectView::ObjectView(Engine::Render::Renderer& renderer, std::shared_ptr<Level::MaterialResolver> resolver)
        : _renderer(renderer), _resolver(std::move(resolver))
    {
    }

    void ObjectView::Draw(MdkObject& obj, const Look& look, Matrix4x4 transform, const MdkObject::Parts& pose)
    {
        if (obj.Model == nullptr || obj.Dead)
        {
            return;
        }

        auto* layout = LayoutOf(obj.Model, look);
        if (layout == nullptr)
        {
            return;
        }

        auto hidden = obj.HiddenParts;
        auto swap = Swaps != nullptr ? Swaps->Of(*obj.Model) : nullptr;
        if (swap != nullptr)
        {
            swap->Pose(pose, hidden);
        }

        auto frame = obj.TextureFrame >= 0 ? obj.TextureFrame : 0;
        auto count = Arrange(*layout, hidden);
        auto glows = obj.EffectFrames > 0 ? Glow::Shines : Glow::Lit;
        for (auto i = 0; i < count; i++)
        {
            const auto& batch = *layout->Batches[layout->Order[i]];
            auto corners = Visible(batch, hidden);
            int mesh = 0;
            int first = 0;
            auto vertices = _renderer.Stream(corners, mesh, first);
            if (vertices.empty())
            {
                continue;
            }

            auto at = 0;
            for (size_t c = 0; c < batch.Parts.size(); c++)
            {
                auto part = batch.Parts[c];
                if (IsHidden(hidden, part))
                {
                    continue;
                }

                vertices[at++] = batch.Source == Source::Mod
                    ? Vertex(swap->Vertex(part, batch.Indices[c]), batch.Uvs[c])
                    : Vertex(pose[part][batch.Indices[c]], batch.Uvs[c]);
            }

            _renderer.Draw(mesh, first, corners, Shine(batch.Material, glows), frame, transform);
        }
    }

    int ObjectView::Arrange(Layout& layout, int hidden)
    {
        auto count = 0;
        for (size_t b = 0; b < layout.Batches.size(); b++)
        {
            auto first = Unseen;
            const auto& starts = layout.Batches[b]->FirstTriangle;
            for (size_t p = 0; p < starts.size(); p++)
            {
                if (!IsHidden(hidden, static_cast<int>(p)))
                {
                    first = std::min(first, starts[p]);
                }
            }

            if (first == Unseen)
            {
                continue;
            }

            // Insertion sort: a model has a few surfaces.
            auto at = count++;
            while (at > 0 && layout.Keys[at - 1] > first)
            {
                layout.Keys[at] = layout.Keys[at - 1];
                layout.Order[at] = layout.Order[at - 1];
                at--;
            }

            layout.Keys[at] = first;
            layout.Order[at] = static_cast<int>(b);
        }

        return count;
    }

    int ObjectView::Visible(const Batch& batch, int hidden)
    {
        auto corners = 0;
        for (size_t p = 0; p < batch.PartCorners.size(); p++)
        {
            if (!IsHidden(hidden, static_cast<int>(p)))
            {
                corners += batch.PartCorners[p];
            }
        }

        return corners;
    }

    ObjectView::Layout* ObjectView::LayoutOf(const std::shared_ptr<Formats::Model>& model, const Look& look)
    {
        auto key = std::make_pair(model.get(), look.Palette.get());
        auto found = _layouts.find(key);
        if (found == _layouts.end())
        {
            found = _layouts.emplace(key, Build(*model, look)).first;
        }

        return found->second.get();
    }

    std::shared_ptr<ObjectView::Layout> ObjectView::Build(const Formats::Model& model, const Look& look)
    {
        auto parts = static_cast<int>(model.PartList.size());
        BySurface bySurface;
        std::vector<std::shared_ptr<Batch>> batches;
        auto swap = Swaps != nullptr ? Swaps->Of(model) : nullptr;
        auto sequence = 0;
        for (auto p = 0; p < parts; p++)
        {
            const auto& part = *model.PartList[p];
            if (swap != nullptr && swap->Parts[p].has_value())
            {
                AddSwapped(p, *swap->Parts[p], SurfacesOf(*swap, look), sequence, bySurface, batches, parts);
                sequence += static_cast<int>(part.TriangleMaterials.size());
                continue;
            }

            for (size_t t = 0; t < part.TriangleMaterials.size(); t++, sequence++)
            {
                auto surface = _resolver->Resolve(part.TriangleMaterials[t], model.Materials, look.Palette, look.Archives, Pass::DoubleSided);
                if (!surface.has_value())
                {
                    continue;
                }

                const auto& s = *surface;
                auto key = std::make_pair(s.Material, Source::Model);
                auto& batch = bySurface[key];
                if (batch == nullptr)
                {
                    batch = std::make_shared<Batch>(s.Material, parts, Source::Model);
                    batches.push_back(batch);
                }

                // UVs are in texels of one frame.
                auto scale = s.Texture == nullptr ? Vector2::Zero() : Vector2(1.f / s.Texture->Width, 1.f / s.Texture->Height);
                batch->FirstTriangle[p] = std::min(batch->FirstTriangle[p], sequence);
                batch->PartCorners[p] += TriangleVertices;
                for (auto k = 0; k < TriangleVertices; k++)
                {
                    batch->Parts.push_back(p);
                    batch->Indices.push_back(part.TriangleIndices[t * TriangleVertices + k]);
                    batch->Uvs.push_back(part.TriangleUvs[t * TriangleVertices + k] * scale);
                }
            }
        }

        if (batches.empty())
        {
            return nullptr;
        }

        return std::make_shared<Layout>(std::move(batches));
    }

    void ObjectView::AddSwapped(int p, const Mods::ModelSwap::Mesh& mesh, const std::vector<std::optional<Material>>& surfaces,
        int sequence, BySurface& bySurface, std::vector<std::shared_ptr<Batch>>& batches, int parts)
    {
        for (size_t t = 0; t < mesh.Materials.size(); t++)
        {
            auto index = mesh.Materials[t];
            const auto& surface = index >= 0 && index < static_cast<int>(surfaces.size()) ? surfaces[index] : surfaces.back();
            if (!surface.has_value())
            {
                continue;
            }

            const auto& material = *surface;
            auto key = std::make_pair(material, Source::Mod);
            auto& batch = bySurface[key];
            if (batch == nullptr)
            {
                batch = std::make_shared<Batch>(material, parts, Source::Mod);
                batches.push_back(batch);
            }

            batch->FirstTriangle[p] = std::min(batch->FirstTriangle[p], sequence);
            batch->PartCorners[p] += TriangleVertices;
            for (auto k = 0; k < TriangleVertices; k++)
            {
                auto vertex = mesh.Triangles[t * TriangleVertices + k];
                batch->Parts.push_back(p);
                batch->Indices.push_back(vertex);
                batch->Uvs.push_back(mesh.Uvs[vertex]);
            }
        }
    }

    const std::vector<std::optional<Material>>& ObjectView::SurfacesOf(const Mods::ModelSwap& swap, const Look& look)
    {
        auto key = std::make_pair(&swap, look.Palette.get());
        auto made = _surfaces.find(key);
        if (made != _surfaces.end())
        {
            return made->second;
        }

        std::vector<std::optional<Material>> materials;
        for (const auto& s : swap.Surfaces)
        {
            if (s.Image.has_value())
            {
                materials.emplace_back(_resolver->Image(*s.Image, Pass::DoubleSided).Material);
                continue;
            }

            auto named = _resolver->Named(s.Name, look.Palette, look.Archives, Pass::DoubleSided);
            materials.emplace_back(named.has_value() ? named->Material : _resolver->Flat(s.Colour, Pass::DoubleSided).Material);
        }

        materials.emplace_back(_resolver->Flat(Vector4::One(), Pass::DoubleSided).Material);
        return _surfaces[key] = std::move(materials);
    }
}
