#pragma once

#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/Dictionary.h"
#include "core/Numerics.h"
#include "engine/render/Renderer.h"
#include "formats/Arena.h"
#include "formats/Palette.h"
#include "formats/Texture.h"
#include "formats/TextureArchive.h"
#include "game/level/LevelData.h"
#include "game/level/MaterialResolver.h"
#include "game/level/Outlines.h"
#include "game/level/TriangleGroups.h"
#include "game/scripts/EndLevel.h"

namespace Mdk::Game::Scripts
{
    class AnimatedTextures;
}

namespace Mdk::Game::Level
{
    // The level's arenas on the GPU: a vertex buffer per triangle group, its triangles in
    // batches by surface. Hidden groups and unreachable arenas aren't drawn; a group that changes
    // texture is rebuilt.
    // At the end of a level the triangles torn off Kurt's arena are collapsed in their group's mesh
    // (made dynamic) and drawn as flying pieces.
    //   arena ─┬─ group 0 (the rest)   ─► mesh, batches
    //          └─ group n (scripted)   ─► mesh, batches   (hidden? retextured?)
    //   end of level: torn triangles ─► collapsed in their mesh;  pieces ─► one dynamic mesh
    class LevelView
    {
    public:
        // The arenas' surfaces come from resolver (shared with the objects and effects: one GPU
        // copy of each texture, one upload per bullet hole).
        LevelView(Engine::Render::Renderer& renderer, LevelData& level, std::shared_ptr<TriangleGroups> groups,
            Engine::Render::Shading shading, std::shared_ptr<MaterialResolver> resolver);

        int TriangleCount = 0;
        // The outlined edges built (glass frames).
        int OutlineCount = 0;

        // Shows an arena Kurt can only reach by a teleport.
        void Enter(const std::string& arena);

        // Queues the visible groups of arenas (the original draws Kurt's arena and the active
        // second one, 0x41e344), or of every reachable arena when empty, animated textures at
        // their frames; the camera's arena over the others (ArenaBand).
        void Draw(std::span<const std::string> arenas, Scripts::AnimatedTextures& frames, const std::string& camera);

        // The end of a level: the torn triangles vanish from the arena and fly as pieces, in
        // its band (camera: the camera's arena).
        void DrawEnd(Scripts::EndLevel* end, const std::string& camera);

    private:
        // Triangles (or outlines) of one surface; Animated names a texture whose frame the
        // scripts set (opcode 133).
        struct Batch
        {
            int First = 0;
            int Count = 0;
            Engine::Render::Material Material;
            std::optional<std::string> Animated;
            Engine::Render::Primitive Primitive = Engine::Render::Primitive::Triangles;

            bool operator==(const Batch&) const = default;
        };

        struct GroupMesh
        {
            GroupMesh(int mesh, std::vector<Batch> batches, std::vector<Engine::Render::Vertex> vertices)
                : Mesh(mesh), Batches(std::move(batches)), Vertices(std::move(vertices)) {}

            int Mesh;
            std::vector<Batch> Batches;
            std::vector<Engine::Render::Vertex> Vertices;
            bool Dynamic = false;
        };

        // Where a triangle is drawn: its group, first vertex, surface and UV scale.
        struct Placement
        {
            int Group = 0;
            int First = 0;
            Engine::Render::Material Material;
            Vector2 Scale{};

            bool operator==(const Placement&) const = default;
        };

        struct ArenaView
        {
            ArenaView(std::shared_ptr<Formats::Arena> arena, std::shared_ptr<Formats::Palette> palette,
                std::vector<std::shared_ptr<Formats::TextureArchive>> archives, bool reachable);

            std::shared_ptr<Formats::Arena> Arena;
            std::vector<Vector3> Positions;
            std::vector<int> Layers;
            std::shared_ptr<Formats::Palette> Palette;
            std::vector<std::shared_ptr<Formats::TextureArchive>> Archives;
            bool Reachable;
            Mdk::Dictionary<int, std::shared_ptr<GroupMesh>> Groups;
            std::unordered_map<int, Placement> Placements;
        };

        struct SurfaceTriangles
        {
            std::shared_ptr<Formats::Texture> Texture;
            std::vector<int> Triangles;
        };

        using BySurface = Mdk::Dictionary<Engine::Render::Material, SurfaceTriangles, MaterialHash>;

        static constexpr int TriangleVertices = 3;

        Engine::Render::Renderer& _renderer;
        std::shared_ptr<MaterialResolver> _resolver;
        std::shared_ptr<TriangleGroups> _groups;
        OutlineEdges _edges;
        Mdk::Dictionary<std::string, std::shared_ptr<ArenaView>> _arenas;
        int _tornShown = 0;
        int _piecesMesh = -1;
        std::vector<Engine::Render::Vertex> _pieceVertices;

        void Draw(ArenaView& view, Scripts::AnimatedTextures& frames);

        // Collapses the newly torn triangles in their groups' meshes (made dynamic).
        void Tear(ArenaView& view, std::span<const int> torn);

        // The pieces, turned by their angle around their centre, both faces, batched by surface.
        void DrawPieces(ArenaView& view, std::span<const std::shared_ptr<Scripts::EndLevel::Piece>> pieces);

        int AddPiece(ArenaView& view, const Scripts::EndLevel::Piece& piece, int at);

        // A group took another texture: its mesh is built again (the old buffer stays until the level ends).
        void Rebuild(const std::string& arena, int number);

        // The outlines of the flat-coloured surfaces, after the triangles, in their colour.
        void AddOutlines(const Formats::Arena& arena, const BySurface& bySurface,
            std::vector<Engine::Render::Vertex>& vertices, std::vector<Batch>& batches);

        std::shared_ptr<GroupMesh> Build(ArenaView& view, const TriangleGroups::Group& group);
    };
}
