#include "game/mods/ModelExport.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>

#include "core/Dictionary.h"
#include "core/DotNet.h"
#include "formats/Model.h"
#include "formats/Palette.h"
#include "formats/Png.h"
#include "formats/Texture.h"
#include "formats/TextureArchive.h"
#include "game/hd_textures/TextureExport.h"
#include "game/level/MaterialResolver.h"
#include "game/mods/ModelSwap.h"

namespace Mdk::Game::Mods
{
    namespace
    {
        // A corner's (vertex, UV), as C#'s tuple key: 0 and -0 are one.
        struct Corner
        {
            int Vertex = 0;
            Vector2 Uv;

            bool operator==(const Corner&) const = default;
        };

        struct CornerHash
        {
            std::size_t operator()(const Corner& c) const
            {
                const auto x = std::bit_cast<std::uint32_t>(c.Uv.X + 0.f);
                const auto y = std::bit_cast<std::uint32_t>(c.Uv.Y + 0.f);
                return std::hash<std::uint64_t>()((static_cast<std::uint64_t>(x) << 32) | y) ^ std::hash<int>()(c.Vertex);
            }
        };

        // One material's primitive being built: its corners welded.
        struct Building
        {
            std::vector<Vector3> Positions;
            std::vector<Vector2> Uvs;
            std::vector<int> Indices;
            std::unordered_map<Corner, int, CornerHash> Welded;
        };
    }

    Formats::Glb::Scene ModelExport::Scene(const Formats::Model& model, const Formats::Palette& palette,
        const std::vector<std::shared_ptr<Formats::TextureArchive>>& archives)
    {
        std::vector<Formats::Glb::Material> materials;
        std::unordered_map<std::string, int> materialIndex;
        std::vector<Formats::Glb::Mesh> meshes;
        for (int p = 0; p < std::ssize(model.PartList); p++)
        {
            const auto& part = *model.PartList[p];
            Dictionary<int, std::shared_ptr<Building>> primitives;
            for (int t = 0; t < std::ssize(part.TriangleMaterials); t++)
            {
                const auto value = part.TriangleMaterials[t];
                if (!Level::MaterialResolver::IsDrawn(value, model.Materials, archives))
                {
                    continue;
                }

                const auto name = value < 0 ? PenPrefix + std::to_string(-value) : model.Materials[value];
                auto found = materialIndex.find(name);
                if (found == materialIndex.end())
                {
                    found = materialIndex.emplace(name, static_cast<int>(materials.size())).first;
                    materials.push_back(Material(name, palette, archives));
                }

                const auto material = found->second;
                auto& primitive = primitives[material];
                if (primitive == nullptr)
                {
                    primitive = std::make_shared<Building>();
                }

                const auto texture = Find(name, archives);
                const auto scale = texture == nullptr ? Vector2::Zero() : Vector2(1.f / texture->Width, 1.f / texture->Height);
                for (int k = 0; k < Corners; k++)
                {
                    const auto vertex = part.TriangleIndices[t * Corners + k];
                    const auto uv = part.TriangleUvs[t * Corners + k] * scale;
                    auto welded = primitive->Welded.find(Corner{vertex, uv});
                    if (welded == primitive->Welded.end())
                    {
                        welded = primitive->Welded.emplace(Corner{vertex, uv}, static_cast<int>(primitive->Positions.size())).first;
                        primitive->Positions.push_back(ModelSwap::ToGltf(part.Vertices[vertex]));
                        primitive->Uvs.push_back(uv);
                    }

                    primitive->Indices.push_back(welded->second);
                }
            }

            std::vector<Formats::Glb::Primitive> built;
            for (const auto& [material, primitive] : primitives)
            {
                built.push_back(Formats::Glb::Primitive{primitive->Positions, primitive->Uvs, primitive->Indices, material});
            }

            meshes.push_back(Formats::Glb::Mesh{ModelSwap::PartName(model, p), std::move(built)});
        }

        return Formats::Glb::Scene{std::move(meshes), std::move(materials)};
    }

    Formats::Glb::Material ModelExport::Material(const std::string& name, const Formats::Palette& palette,
        const std::vector<std::shared_ptr<Formats::TextureArchive>>& archives)
    {
        if (const auto texture = Find(name, archives))
        {
            const auto rgba = HdTextures::TextureExport::Frame(*texture, palette, 0);
            return Formats::Glb::Material{name, Vector4::One(), Formats::Png::Encode(texture->Width, texture->Height, rgba, Formats::Png::Channels::Rgba)};
        }

        std::optional<int> archived;
        for (const auto& a : archives)
        {
            if (const auto* c = a->Colors.Find(name))
            {
                archived = *c;
                break;
            }
        }

        int pen = 0;
        const auto index = archived ? *archived
            : (Str::StartsWith(name, PenPrefix) && Str::TryParseInt(std::string_view(name).substr(std::string_view(PenPrefix).size()), pen) ? pen : Formats::Palette::Size);
        if (index >= Formats::Palette::Size)
        {
            return Formats::Glb::Material{name, SpecialColour, std::nullopt};
        }

        const auto colour = Vector4(palette.Rgba[index * Channels], palette.Rgba[index * Channels + 1], palette.Rgba[index * Channels + 2],
            std::numeric_limits<byte>::max()) * ByteToUnit;
        return Formats::Glb::Material{name, ModelSwap::ToLinear(colour), std::nullopt};
    }

    std::shared_ptr<Formats::Texture> ModelExport::Find(const std::string& name,
        const std::vector<std::shared_ptr<Formats::TextureArchive>>& archives)
    {
        for (const auto& archive : archives)
        {
            if (const auto* texture = archive->Textures.Find(name))
            {
                return *texture;
            }
        }

        return nullptr;
    }
}
