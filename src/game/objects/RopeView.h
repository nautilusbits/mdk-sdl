#pragma once

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Numerics.h"
#include "engine/render/DepthBands.h"
#include "engine/render/Renderer.h"
#include "formats/Palette.h"
#include "game/level/LevelData.h"

namespace Mdk::Game
{
    class FrameBlend;
}

namespace Mdk::Game::Objects
{
    class MdkObject;

    // Objects' rope lines (obj+0x2d0, drawn as lines by arena_build_drawlist 0x4185f0;
    // godot-mdk mdk_object.gd update_ropes), in a colour of their arena's palette, rebuilt every
    // frame into one dynamic mesh.
    //   mask 0xFF:  reference point i+1 ──── rope point i    (each non-zero point, opcode 242)
    //   bit 0, 1:   rope point 0 (+5 up) ──── rope point 1    (a swing's rope: object to pivot)
    //               rope point 2 (+5 up) ──── rope point 3
    class RopeView
    {
    public:
        static constexpr int AllLines = 0xFF;

        RopeView(Engine::Render::Renderer& renderer, Level::LevelData& level);

        // The line ends of an object's ropes, two points each.
        static std::vector<Vector3> LinesOf(MdkObject& obj);

        // The line ends as blend's frame draws them: the ends on the object move with it.
        static std::vector<Vector3> LinesOf(MdkObject& obj, FrameBlend& blend);

        // Queues the ropes of the drawn objects, as blend's frame draws them, in their arenas'
        // bands (camera: the camera's arena).
        void Draw(std::span<const std::shared_ptr<MdkObject>> objects, FrameBlend& blend, const std::string& camera);

    private:
        static constexpr int Pairs = 2;
        static constexpr float PairLift = 5.f;
        // Line vertices drawn at most per frame (the rest is dropped).
        static constexpr int MaxVertices = 1 << 12;
        static constexpr float ByteToUnit = 1.f / 255.f;

        struct Queued
        {
            int First = 0;
            int Count = 0;
            Engine::Render::Material Material;
            Engine::Render::DepthBand Band = Engine::Render::DepthBand::Front;
        };

        Engine::Render::Renderer& _renderer;
        Level::LevelData& _level;
        int _mesh;
        std::unordered_map<std::string, std::shared_ptr<Formats::Palette>> _palettes;
        std::vector<Engine::Render::Vertex> _vertices;
        std::vector<Queued> _draws;
        // An object's line ends (kept: no list per frame).
        std::vector<Vector3> _lines;

        // The steps' frames: ropes where the tick left them.
        static FrameBlend& Steps();

        // LinesOf added to lines.
        static void AddLines(MdkObject& obj, FrameBlend& blend, std::vector<Vector3>& lines);

        // A colour of an arena's palette (the level's for an unknown arena).
        Vector4 Colour(const std::string& arena, int index);
    };
}
