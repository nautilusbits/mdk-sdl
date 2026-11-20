#include "Mdk.Game/Fall/FallModels.h"

#include <bit>
#include <functional>

#include "Mdk.Core/Dictionary.h"
#include "Mdk.Core/DotNet.h"

namespace Mdk::Game::Fall
{
    using Engine::Render::Material;
    using Engine::Render::Pass;
    using Engine::Render::Vertex;

    namespace
    {
        // The (material, pen) key of a surface's triangles.
        using SurfaceKey = std::pair<Material, int>;

        struct SurfaceKeyHash
        {
            size_t operator()(const SurfaceKey& key) const
            {
                const auto& m = key.first;
                size_t h = std::hash<int>()(m.Texture);
                auto mix = [&h](size_t v) { h = h * 31 + v; };
                mix(std::hash<int>()(m.Palette));
                mix(std::bit_cast<uint>(m.Colour.X));
                mix(std::bit_cast<uint>(m.Colour.Y));
                mix(std::bit_cast<uint>(m.Colour.Z));
                mix(std::bit_cast<uint>(m.Colour.W));
                mix(std::hash<int>()(m.FrameCount));
                mix(static_cast<size_t>(m.Pass));
                mix(std::hash<int>()(key.second));
                return h;
            }
        };

        struct SurfaceTriangles
        {
            std::shared_ptr<Formats::Texture> Texture;
            std::vector<Vertex> Vertices;
        };
    }

    FallModels::FallModels(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Bni> bni, std::shared_ptr<Formats::TextureArchive> mti,
        std::shared_ptr<FallPalette> palette, int paletteId)
        : _renderer(renderer), _bni(std::move(bni)), _mti(std::move(mti)), _palette(std::move(palette)), _paletteId(paletteId)
    {
    }

    // Models with named parts (bit 7 of the table 0x490ca4).
    const std::unordered_set<std::string>& FallModels::NamedParts()
    {
        static const std::unordered_set<std::string> names = { "KURT", "MISSILE", "CHUTE", "BONES", "SW_DUMMY", "SW_H150", "SW_THUMP", "SW_TWIST", "SW_INTER" };
        return names;
    }

    // A model animation of the BNI.
    std::shared_ptr<Formats::ModelAnimation> FallModels::Animation(const std::string& name)
    {
        auto found = _animations.find(name);
        if (found != _animations.end())
        {
            return found->second;
        }

        auto animation = Formats::ModelAnimation::Parse(name, _bni->Bytes(), _bni->Entries().At(name).Offset);
        _animations[name] = animation;
        return animation;
    }

    // Draws a model at rest, or in a frame of an animation; textureFrame picks an animated texture's
    // frame, pass how it meets the scene.
    void FallModels::Draw(const std::string& name, Matrix4x4 world, const std::string& animation, int frame, int textureFrame, Pass pass)
    {
        auto key = std::make_tuple(name, animation, frame);
        auto found = _meshes.find(key);
        if (found == _meshes.end())
        {
            found = _meshes.emplace(key, Build(name, animation, frame)).first;
        }

        const auto& mesh = found->second;
        if (!mesh)
        {
            return;
        }

        for (const auto& batch : mesh->Batches)
        {
            auto material = batch.Material;
            if (batch.Pen != NoPen)
            {
                material.Colour = _palette->Colour(batch.Pen);
            }

            material.Pass = pass;
            _renderer.Draw(mesh->Mesh, batch.First, batch.Count, material, textureFrame, world);
        }
    }

    std::shared_ptr<Formats::Model> FallModels::Get(const std::string& name)
    {
        auto found = _models.find(name);
        if (found != _models.end())
        {
            return found->second;
        }

        auto parts = NamedParts().contains(name) ? Formats::Model::Parts::Named : Formats::Model::Parts::Single;
        std::shared_ptr<Formats::Model> model;
        if (_bni->Has(name))
        {
            model = Formats::Model::Parse(name, *_bni->Bytes(), _bni->Entries().At(name).Offset, Formats::Model::Header::NoFlags, parts);
        }

        _models[name] = model;
        return model;
    }

    std::optional<FallModels::PoseMesh> FallModels::Build(const std::string& name, const std::string& animation, int frame)
    {
        auto model = Get(name);
        if (!model)
        {
            return std::nullopt;
        }

        auto pose = model->RestPose();
        if (!animation.empty())
        {
            auto baked = _baked.find(animation);
            if (baked == _baked.end())
            {
                baked = _baked.emplace(animation, Animation(animation)->Bake(*model)).first;
            }

            pose = baked->second[frame];
        }

        Mdk::Dictionary<SurfaceKey, SurfaceTriangles, SurfaceKeyHash> bySurface;
        for (auto p = 0; p < static_cast<int>(model->PartList.size()); p++)
        {
            const auto& part = *model->PartList[p];
            for (auto t = 0; t < static_cast<int>(part.TriangleMaterials.size()) && !pose[p].empty(); t++)
            {
                auto surface = Resolve(part.TriangleMaterials[t], model->Materials);
                if (!surface)
                {
                    continue;
                }

                auto key = SurfaceKey(surface->Material, surface->Pen);
                auto* entry = bySurface.Find(key);
                if (!entry)
                {
                    bySurface[key] = SurfaceTriangles{ surface->Texture, {} };
                    entry = bySurface.Find(key);
                }

                auto scale = !entry->Texture ? Vector2::Zero() : Vector2(1.f / entry->Texture->Width, 1.f / entry->Texture->Height);
                for (auto k = 0; k < 3; k++)
                {
                    entry->Vertices.push_back(Vertex(pose[p][part.TriangleIndices[t * 3 + k]], part.TriangleUvs[t * 3 + k] * scale));
                }
            }
        }

        std::vector<Vertex> vertices;
        std::vector<Batch> batches;
        for (const auto& [key, entry] : bySurface)
        {
            batches.push_back(Batch{ static_cast<int>(vertices.size()), static_cast<int>(entry.Vertices.size()), key.first, key.second });
            vertices.insert(vertices.end(), entry.Vertices.begin(), entry.Vertices.end());
        }

        if (vertices.empty())
        {
            return std::nullopt;
        }

        return PoseMesh{ _renderer.CreateMesh(vertices), std::move(batches) };
    }

    // A triangle's surface: a texture of the MTI, or a palette colour (negative values, the MTI's
    // colours, PEN_n); none when it isn't drawn.
    std::optional<FallModels::Surface> FallModels::Resolve(int value, std::span<const std::string> names)
    {
        if (value < 0)
        {
            return Colour(-value);
        }

        const auto& name = names[value];
        if (const auto* texture = _mti->Textures.Find(name))
        {
            return Surface{ Material(TextureId(*texture), _paletteId, Vector4::One(), (*texture)->FrameCount, Pass::DoubleSided), *texture, NoPen };
        }

        if (const auto* index = _mti->Colors.Find(name))
        {
            return Colour(*index);
        }

        auto pen = 0;
        if (Str::StartsWith(name, PenPrefix) && Str::TryParseInt(std::string_view(name).substr(std::string_view(PenPrefix).size()), pen))
        {
            return Colour(pen);
        }

        return std::nullopt;
    }

    std::optional<FallModels::Surface> FallModels::Colour(int index)
    {
        if (index >= SpecialFirst)
        {
            return std::nullopt;
        }

        return Surface{ Material::Flat(_palette->Colour(index), Pass::DoubleSided), nullptr, index };
    }

    int FallModels::TextureId(const std::shared_ptr<Formats::Texture>& texture)
    {
        auto found = _textures.find(texture.get());
        if (found != _textures.end())
        {
            return found->second;
        }

        auto id = _renderer.CreateIndexTexture(texture->Width, texture->Height * texture->FrameCount, texture->Indices);
        _textures[texture.get()] = id;
        return id;
    }
}
