#include "game/level/LevelView.h"

#include <algorithm>
#include <array>

#include "game/Angles.h"
#include "game/level/ArenaBand.h"
#include "game/level/Layers.h"
#include "game/scripts/AnimatedTextures.h"

namespace Mdk::Game::Level
{
    using Engine::Render::DepthBand;
    using Engine::Render::Material;
    using Engine::Render::Pass;
    using Engine::Render::Primitive;
    using Engine::Render::Vertex;

    LevelView::ArenaView::ArenaView(std::shared_ptr<Formats::Arena> arena, std::shared_ptr<Formats::Palette> palette,
        std::vector<std::shared_ptr<Formats::TextureArchive>> archives, bool reachable)
        : Arena(std::move(arena)), Palette(std::move(palette)), Archives(std::move(archives)), Reachable(reachable)
    {
        Positions.reserve(Arena->TriangleIndices.size());
        for (auto i : Arena->TriangleIndices)
        {
            Positions.push_back(Arena->Vertices[i]);
        }

        Layers = Level::Layers::Of(*Arena);
    }

    LevelView::LevelView(Engine::Render::Renderer& renderer, LevelData& level, std::shared_ptr<TriangleGroups> groups,
        Engine::Render::Shading shading, std::shared_ptr<MaterialResolver> resolver)
        : _renderer(renderer), _resolver(std::move(resolver)), _groups(std::move(groups)),
          _edges(shading == Engine::Render::Shading::Original ? OutlineEdges::Flagged : OutlineEdges::Frame)
    {
        for (const auto& arena : level.Arenas)
        {
            _groups->Add(arena);
            auto view = std::make_shared<ArenaView>(arena, level.PaletteOf(*arena), level.ArchivesOf(*arena), level.IsReachable(arena->Name));
            _arenas[arena->Name] = view;
            for (const auto& group : _groups->Of(arena->Name))
            {
                view->Groups[group->Number] = Build(*view, *group);
            }
        }

        // C#'s groups.Changed += Rebuild: an earlier handler still runs first.
        auto previous = std::move(_groups->Changed);
        _groups->Changed = [this, previous](const std::string& arena, int number)
        {
            if (previous)
            {
                previous(arena, number);
            }

            Rebuild(arena, number);
        };
    }

    void LevelView::Enter(const std::string& arena)
    {
        if (auto* view = _arenas.Find(arena))
        {
            (*view)->Reachable = true;
        }
    }

    void LevelView::Draw(std::span<const std::string> arenas, Scripts::AnimatedTextures& frames, const std::string& camera)
    {
        if (arenas.empty())
        {
            for (const auto& [name, view] : _arenas)
            {
                if (view->Reachable)
                {
                    Draw(*view, frames);
                }
            }

            return;
        }

        for (size_t i = 0; i < arenas.size(); i++)
        {
            if (auto* view = _arenas.Find(arenas[i]))
            {
                _renderer.Band = ArenaBand::Of(arenas[i], camera);
                Draw(**view, frames);
            }
        }

        _renderer.Band = DepthBand::Front;
    }

    void LevelView::Draw(ArenaView& view, Scripts::AnimatedTextures& frames)
    {
        for (const auto& [number, mesh] : view.Groups)
        {
            if (mesh == nullptr || (_groups->Get(view.Arena->Name, number)->State & TriangleGroups::State::Hidden) != TriangleGroups::State::None)
            {
                continue;
            }

            for (const auto& batch : mesh->Batches)
            {
                if (batch.Primitive == Primitive::Lines)
                {
                    _renderer.DrawLines(mesh->Mesh, batch.First, batch.Count, batch.Material);
                    continue;
                }

                auto frame = batch.Animated.has_value() ? frames.FrameOf(view.Arena->Name, *batch.Animated) : 0;
                _renderer.Draw(mesh->Mesh, batch.First, batch.Count, batch.Material, frame);
            }
        }
    }

    void LevelView::DrawEnd(Scripts::EndLevel* end, const std::string& camera)
    {
        if (end == nullptr)
        {
            return;
        }

        auto arena = end->Arena();
        auto* view = _arenas.Find(arena);
        if (view == nullptr)
        {
            return;
        }

        Tear(**view, end->Torn());
        _renderer.Band = ArenaBand::Of(arena, camera);
        DrawPieces(**view, end->Pieces());
        _renderer.Band = DepthBand::Front;
    }

    void LevelView::Tear(ArenaView& view, std::span<const int> torn)
    {
        if (static_cast<int>(torn.size()) == _tornShown)
        {
            return;
        }

        // C#'s HashSet: each mesh once (here in the order first torn).
        std::vector<GroupMesh*> changed;
        for (; _tornShown < static_cast<int>(torn.size()); _tornShown++)
        {
            auto it = view.Placements.find(torn[_tornShown]);
            if (it == view.Placements.end())
            {
                continue;
            }

            const auto& place = it->second;
            auto* mesh = view.Groups.At(place.Group).get();
            if (mesh == nullptr)
            {
                continue;
            }

            for (auto k = 1; k < TriangleVertices; k++)
            {
                mesh->Vertices[place.First + k] = mesh->Vertices[place.First];
            }

            if (std::find(changed.begin(), changed.end(), mesh) == changed.end())
            {
                changed.push_back(mesh);
            }
        }

        for (auto* mesh : changed)
        {
            if (!mesh->Dynamic)
            {
                mesh->Mesh = _renderer.CreateDynamicMesh(static_cast<int>(mesh->Vertices.size()));
                mesh->Dynamic = true;
            }

            _renderer.UpdateMesh(mesh->Mesh, mesh->Vertices);
        }
    }

    void LevelView::DrawPieces(ArenaView& view, std::span<const std::shared_ptr<Scripts::EndLevel::Piece>> pieces)
    {
        if (_piecesMesh < 0)
        {
            _pieceVertices = std::vector<Vertex>(view.Arena->TriangleCount() * TriangleVertices);
            _piecesMesh = _renderer.CreateDynamicMesh(static_cast<int>(_pieceVertices.size()));
        }

        // The placed pieces grouped by surface, in the order each surface first appears (C#'s GroupBy).
        Mdk::Dictionary<Material, std::vector<const Scripts::EndLevel::Piece*>, MaterialHash> bySurfaces;
        for (const auto& piece : pieces)
        {
            auto it = view.Placements.find(piece->Triangle);
            if (it == view.Placements.end())
            {
                continue;
            }

            bySurfaces[it->second.Material].push_back(piece.get());
        }

        auto count = 0;
        std::vector<Batch> batches;
        for (const auto& [key, bySurface] : bySurfaces)
        {
            auto first = count;
            for (const auto* piece : bySurface)
            {
                count = AddPiece(view, *piece, count);
            }

            auto material = key;
            if (material.Pass == Pass::Solid)
            {
                material.Pass = Pass::DoubleSided;
            }

            batches.push_back(Batch{first, count - first, material});
        }

        if (count == 0)
        {
            return;
        }

        _renderer.UpdateMesh(_piecesMesh, std::span<const Vertex>(_pieceVertices.data(), count));
        for (const auto& batch : batches)
        {
            _renderer.Draw(_piecesMesh, batch.First, batch.Count, batch.Material);
        }
    }

    int LevelView::AddPiece(ArenaView& view, const Scripts::EndLevel::Piece& piece, int at)
    {
        const auto& place = view.Placements.at(piece.Triangle);
        auto t = piece.Triangle;
        std::array<Vector3, TriangleVertices> corners;
        auto middle = Vector3::Zero();
        for (auto k = 0; k < TriangleVertices; k++)
        {
            corners[k] = view.Positions[t * TriangleVertices + k];
            middle += corners[k] / static_cast<float>(TriangleVertices);
        }

        auto turn = Matrix4x4::CreateRotationZ(DegreesToRadians(piece.Angle));
        for (auto k : {0, 1, 2})
        {
            auto position = piece.Centre + Vector3::Transform(corners[k] - middle, turn);
            _pieceVertices[at++] = Vertex(position, view.Arena->TriangleUvs[t * TriangleVertices + k] * place.Scale);
        }

        return at;
    }

    void LevelView::Rebuild(const std::string& arena, int number)
    {
        auto& view = *_arenas.At(arena);
        auto group = _groups->Get(arena, number);
        if (group->Material.has_value())
        {
            view.Groups[number] = Build(view, *group);
        }
    }

    void LevelView::AddOutlines(const Formats::Arena& arena, const BySurface& bySurface,
        std::vector<Vertex>& vertices, std::vector<Batch>& batches)
    {
        for (const auto& [material, entry] : bySurface)
        {
            // Mirrors show the panorama: no colour for lines.
            if (entry.Texture != nullptr || material.Pass == Pass::Mirror)
            {
                continue;
            }

            auto lines = Outlines::Of(arena, entry.Triangles, _edges);
            if (lines.empty())
            {
                continue;
            }

            OutlineCount += static_cast<int>(lines.size()) / 2;
            batches.push_back(Batch{static_cast<int>(vertices.size()), static_cast<int>(lines.size()), material, std::nullopt, Primitive::Lines});
            for (const auto& p : lines)
            {
                vertices.push_back(Vertex(p, Vector2::Zero()));
            }
        }
    }

    std::shared_ptr<LevelView::GroupMesh> LevelView::Build(ArenaView& view, const TriangleGroups::Group& group)
    {
        const auto& arena = *view.Arena;

        // Triangles by surface, so each batch is one draw.
        BySurface bySurface;
        for (auto t : group.Triangles)
        {
            // The 1996 demo's triangles that only stop Kurt; the retail's disabled ones.
            if ((arena.TriangleFlags[t] & (arena.ClipFlag | TriangleGroups::Disabled)) != 0)
            {
                continue;
            }

            auto value = group.Material.value_or(arena.TriangleMaterials[t]);
            auto surface = _resolver->Resolve(value, arena.Materials, view.Palette, view.Archives, Pass::Solid);
            if (!surface.has_value())
            {
                continue;
            }

            // Details over the surfaces they lie on.
            auto material = surface->Material;
            material.DepthLayer = view.Layers[t];
            auto* entry = bySurface.Find(material);
            if (entry == nullptr)
            {
                entry = &(bySurface[material] = SurfaceTriangles{surface->Texture, {}});
            }

            entry->Triangles.push_back(t);
        }

        std::vector<Vertex> vertices;
        std::vector<Batch> batches;
        for (const auto& [material, entry] : bySurface)
        {
            // UVs are in texels of one frame.
            const auto& texture = entry.Texture;
            const auto& triangles = entry.Triangles;
            auto scale = texture == nullptr ? Vector2::Zero() : Vector2(1.f / texture->Width, 1.f / texture->Height);
            auto first = static_cast<int>(vertices.size());
            for (auto t : triangles)
            {
                for (auto k = 0; k < 3; k++)
                {
                    vertices.push_back(Vertex(view.Positions[t * 3 + k], arena.TriangleUvs[t * 3 + k] * scale));
                }
            }

            auto animated = texture != nullptr && texture->FrameCount > 1 ? std::optional<std::string>(texture->Name) : std::nullopt;
            batches.push_back(Batch{first, static_cast<int>(vertices.size()) - first, material, animated});
            for (size_t i = 0; i < triangles.size(); i++)
            {
                view.Placements[triangles[i]] = Placement{group.Number, first + static_cast<int>(i) * TriangleVertices, material, scale};
            }
        }

        if (vertices.empty())
        {
            return nullptr;
        }

        TriangleCount += static_cast<int>(vertices.size()) / 3;
        AddOutlines(arena, bySurface, vertices, batches);
        auto mesh = _renderer.CreateMesh(vertices);
        return std::make_shared<GroupMesh>(mesh, batches, vertices);
    }
}
