#pragma once

#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/DepthBands.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Level/MaterialResolver.h"
#include "Mdk.Game/Objects/ObjectView.h"
#include "Mdk.Game/Scripts/Debris.h"
#include "Mdk.Game/Scripts/Ribbon.h"

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;
    class Twister;
}

namespace Mdk::Game::Objects
{
    // Draws the flying pieces, sparks and sprite effects of the scripts, rebuilt every frame
    // into one dynamic mesh: pieces (both faces) batched by surface, sparks as flat triangles in palette
    // colours by how much they face the camera (0x406e24), sprites (effects, spawn_box objects) as
    // camera-facing quads showing one frame of their animated texture.
    //   Debris.Pieces ──► textured triangles │ spark triangles ─┐
    //   Effects.All, ScriptRuntime.Boxes ──► billboard quads ───┤
    //   twisters' ribbons ──► WMIBTEX triangles ────────────────┴─► dynamic mesh ──► renderer
    class EffectsView
    {
    public:
        EffectsView(Engine::Render::Renderer& renderer, std::shared_ptr<Level::MaterialResolver> resolver, Level::LevelData& level,
            Engine::Render::Shading sprites);

        // Queues everything for camera, the pieces and effects tick of the way from the last
        // tick's start to now (0-1), each in its arena's band (cameraArena: the camera's).
        void Draw(Scripts::ScriptRuntime& scripts, const Engine::Render::View& camera, float tick, const std::string& cameraArena);

    private:
        // Vertices drawn at most per frame (the rest is dropped).
        static constexpr int MaxVertices = 1 << 16;
        static constexpr int QuadVertices = 6;
        // A spark's colour ranges over the palette.
        static constexpr int LastColour = 255;
        static constexpr float ByteToUnit = 1.f / 255.f;

        struct Queued
        {
            int First = 0;
            int Count = 0;
            Engine::Render::Material Material;
            int Frame = 0;
            Engine::Render::DepthBand Band = Engine::Render::DepthBand::Front;
        };

        struct PieceKey
        {
            Engine::Render::Material Material;
            Engine::Render::DepthBand Band = Engine::Render::DepthBand::Front;

            bool operator==(const PieceKey&) const = default;
        };

        struct PieceKeyHash
        {
            size_t operator()(const PieceKey& key) const
            {
                return Level::MaterialHash()(key.Material) * 31 + static_cast<size_t>(key.Band);
            }
        };

        Engine::Render::Renderer& _renderer;
        std::shared_ptr<Level::MaterialResolver> _resolver;
        Level::LevelData& _level;
        Engine::Render::Shading _spritesShading;

        int _mesh;
        std::unordered_map<std::string, ObjectView::Look> _looks;
        std::vector<Engine::Render::Vertex> _vertices;
        std::vector<Queued> _draws;
        // This frame's piece batches in the order their surfaces (and bands) came, their
        // vertices in lists kept from frame to frame.
        std::vector<PieceKey> _pieceOrder;
        std::unordered_map<PieceKey, int, PieceKeyHash> _pieceBatch;
        std::vector<std::vector<Engine::Render::Vertex>> _pieceVertices;
        std::map<std::pair<std::string, std::string>, std::optional<Level::MaterialResolver::Surface>> _sprites;
        std::vector<std::string> _spriteName = std::vector<std::string>(1);
        std::vector<Scripts::Ribbon::Corner> _ribbon;

        // Pieces' triangles batched by surface; sparks' by palette colour.
        void AddPieces(std::span<const std::shared_ptr<Scripts::Debris::Piece>> pieces, Vector3 forward, float tick, const std::string& cameraArena);

        // The vertex list of a surface's batch this frame (a kept list, emptied).
        std::vector<Engine::Render::Vertex>& PieceBatch(const Engine::Render::Material& material, Engine::Render::DepthBand band);

        std::optional<Level::MaterialResolver::Surface> PieceSurface(const Scripts::Debris::Piece& piece, int triangle, const ObjectView::Look& look);

        // A spark's face: palette colour base + range × |facing|, where facing is how much it
        // turns to the camera (face-on: base + range, edge-on: base).
        static Level::MaterialResolver::Surface SparkSurface(const Scripts::Debris::Piece& piece, const Formats::Palette& palette,
            Vector3 a, Vector3 b, Vector3 c, Vector3 forward);

        // An animated texture of an arena (or the level) as a sprite surface (shaded as
        // sprites), or null.
        std::optional<Level::MaterialResolver::Surface> Sprite(const std::string& arena, const std::string& texture);

        // A sprite centred at center, right and up being one texel.
        void AddQuad(Vector3 center, Vector3 right, Vector3 up, const Level::MaterialResolver::Surface& sprite, int frame, Engine::Render::DepthBand band);

        // A twister's ribbon, textured with WMIBTEX (UVs in its texels).
        void AddRibbon(const Scripts::Twister& twister, Engine::Render::DepthBand band);

        // The palette and textures an arena's effects draw with.
        ObjectView::Look LookOf(const std::string& name);
    };
}
