#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string_view>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Formats/Texture.h"

namespace Mdk::Game::Scripts
{
    // Bullet holes (special_130 0x45d140; godot-mdk script_runtime.gd stamp_bullet_hole):
    // the hole sprite (BHOLE, BHOLE2 without gore) is copied onto the texture of the face
    // a sniper round hit, centred on the hit's texel, its transparent pixels left out. Textures are
    // shared: every object using one gets the hole. The port finds the textured face of the hit part
    // nearest to the point (the original keeps the face the round's test found).
    //
    //   hit point ──► nearest face (barycentric weights) ──► UV ──► hole pixels into frame 0 (wrapping)
    struct BulletHoles
    {
        static constexpr std::string_view Hole = "BHOLE";
        static constexpr std::string_view GorelessHole = "BHOLE2";

        // A face of a part and the weights of its corners at the point.
        struct Face
        {
            int Triangle = 0;
            Vector3 Weights;

            bool operator==(const Face&) const = default;
        };

        // The face nearest to point (model space) among those
        // textured accepts, or null.
        static std::optional<Face> Nearest(const Formats::Model::Part& part, std::span<const Vector3> vertices, Vector3 point,
            const std::function<bool(int)>& textured);

        // Copies hole onto frame 0 of texture centred on
        // uv (texels), wrapping at its edges; index 0 is transparent.
        static void Stamp(Formats::Texture& texture, Vector2 uv, const Formats::Texture& hole);

    private:
        // Barycentric weights of a point of the triangle's plane, clamped into the triangle
        // (as Godot's get_triangle_barycentric_coords, clamped and normalised).
        static Vector3 Weights(Vector3 p, Vector3 a, Vector3 b, Vector3 c);

        static int Wrap(int value, int size) { return (value % size + size) % size; }
    };
}
