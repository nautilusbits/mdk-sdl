#include "game/mods/ModelSwap.h"

#include <algorithm>
#include <deque>
#include <format>
#include <map>

#include "core/Console.h"
#include "core/IoErrors.h"
#include "formats/Png.h"
#include "game/mods/ModCatalog.h"
#include "game/mods/ModImages.h"

namespace Mdk::Game::Mods
{
    ModelSwap::ModelSwap(const Formats::Model& model, std::vector<std::optional<Mesh>> parts, std::vector<Surface> surfaces)
        : Parts(std::move(parts)), Surfaces(std::move(surfaces))
    {
        for (const auto& p : model.PartList)
        {
            _fits.push_back(std::make_shared<PartFit>(p->Vertices, p->TriangleIndices));
        }

        _poses.resize(Parts.size());
    }

    std::shared_ptr<ModelSwap> ModelSwap::Of(const Formats::Glb::Scene& scene, const Formats::Model& model,
        std::vector<std::string>& unknown)
    {
        // Parts of the same name take the nodes of that name in order.
        std::map<std::string, std::deque<int>, FolderLess> byName;
        for (int p = 0; p < std::ssize(model.PartList); p++)
        {
            byName[PartName(model, p)].push_back(p);
        }

        std::vector<std::optional<Mesh>> parts(model.PartList.size());
        for (const auto& mesh : scene.Meshes)
        {
            const auto queue = byName.find(mesh.Name);
            if (queue == byName.end() || queue->second.empty())
            {
                unknown.push_back(mesh.Name);
                continue;
            }

            const auto part = queue->second.front();
            queue->second.pop_front();
            parts[part] = Merge(mesh);
        }

        if (std::all_of(parts.begin(), parts.end(), [](const std::optional<Mesh>& p) { return !p; }))
        {
            return nullptr;
        }

        std::vector<Surface> surfaces;
        for (const auto& m : scene.Materials)
        {
            surfaces.push_back(Surface{m.Name, ToSrgb(m.Colour), m.Png ? Decode(*m.Png) : std::nullopt});
        }

        return std::make_shared<ModelSwap>(model, std::move(parts), std::move(surfaces));
    }

    ModelSwap::Mesh ModelSwap::Merge(const Formats::Glb::Mesh& mesh)
    {
        Mesh merged;
        for (const auto& primitive : mesh.Primitives)
        {
            const auto first = static_cast<int>(merged.Vertices.size());
            for (const auto& position : primitive.Positions)
            {
                merged.Vertices.push_back(ToMdk(position));
            }

            merged.Uvs.insert(merged.Uvs.end(), primitive.Uvs.begin(), primitive.Uvs.end());
            for (const auto i : primitive.Indices)
            {
                merged.Triangles.push_back(first + i);
            }

            merged.Materials.insert(merged.Materials.end(), primitive.Indices.size() / Corners, primitive.Material);
        }

        return merged;
    }

    std::optional<HdTextures::HdImage> ModelSwap::Decode(std::span<const byte> png)
    {
        try
        {
            auto image = Formats::Png::Decode(png);
            ModImages::Premultiply(image.Rgba);
            return HdTextures::HdImage{image.Width, image.Height, 1, std::move(image.Rgba)};
        }
        catch (const std::exception& e)
        {
            if (!IsReadError(e))
            {
                throw;
            }

            Console::Error().WriteLine(std::format("Mod model image: {}", e.what()));
            return std::nullopt;
        }
    }

    Vector4 ModelSwap::ToSrgb(Vector4 linear)
    {
        return Vector4(MathF::Pow(linear.X, 1.f / Gamma), MathF::Pow(linear.Y, 1.f / Gamma), MathF::Pow(linear.Z, 1.f / Gamma), linear.W);
    }

    Vector4 ModelSwap::ToLinear(Vector4 srgb)
    {
        return Vector4(MathF::Pow(srgb.X, Gamma), MathF::Pow(srgb.Y, Gamma), MathF::Pow(srgb.Z, Gamma), srgb.W);
    }

    void ModelSwap::Pose(const std::vector<std::vector<Vector3>>& pose, int hidden)
    {
        for (int p = 0; p < std::ssize(Parts); p++)
        {
            if (!Parts[p] || (hidden & (1 << p)) != 0)
            {
                continue;
            }

            _poses[p] = _fits[p]->Fit(pose[p]);
        }
    }
}
