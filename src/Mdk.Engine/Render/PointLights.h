#pragma once

#include "Mdk.Core/Numerics.h"

#include <array>
#include <span>

namespace Mdk::Engine::Render
{
    // A point light of the enhanced look: where it is, its linear colour (its strength in
    // it), and how far it reaches (it fades to nothing there).
    struct PointLight
    {
        Vector3 Position;
        Vector3 Colour;
        float Radius = 0;

        bool operator==(const PointLight&) const = default;
    };

    // The frame's point lights (muzzle flashes, explosions, fires): the game adds up to
    // Gathered, the GPU lights with the Shown reaching nearest the camera
    // (distance minus radius). Fixed arrays: no allocation per frame.
    //   Add ×64 ──► Pack(camera): nearest reach first ──► 16 ──► shaders/enhanced.hlsl
    class PointLights
    {
    public:
        static constexpr int Gathered = 64;
        static constexpr int Shown = 16;

        int Count() const { return _count; }

        void Clear() { _count = 0; }

        // Adds a light that shines (a colour, a reach); beyond the limit it's dropped.
        void Add(const PointLight& light);

        // Copies the lights reaching nearest camera into
        // into (at most its length); returns how many.
        int Pack(Vector3 camera, std::span<PointLight> into);

    private:
        std::array<PointLight, Gathered> _lights{};
        // Pack's scratch: whether a light is packed already.
        std::array<bool, Gathered> _taken{};
        int _count = 0;

        // The untaken light whose reach comes nearest the camera.
        int Nearest(Vector3 camera);
    };
}
