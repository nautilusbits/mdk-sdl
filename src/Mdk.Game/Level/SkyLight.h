#pragma once

#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/Dti.h"
#include "Mdk.Formats/Palette.h"

namespace Mdk::Game::Level
{
    // The colours of a level's sky panorama, for the enhanced look's light: the mean linear
    // colour above its horizon (the sky) and from it down (the ground, mountains, haze). Only the
    // columns of the full turn count; rows beyond the panorama take its top and bottom colours.
    //   row 0 ┌──────────────┐
    //         │ sky          │ ◄ mean: Sky
    //   horizon ──────────────
    //         │ ground       │ ◄ mean: Ground
    //         └──────────────┘
    struct SkyLight
    {
        // Mean linear colours above and below the horizon.
        struct Colours
        {
            Vector3 Sky{};
            Vector3 Ground{};

            bool operator==(const Colours&) const = default;
        };

        static Colours Of(const Formats::Dti& dti);

        // The hue of colour at luminance 1: white blended towards it by saturation
        // (0 white, 1 its own hue).
        static Vector3 Tint(Vector3 colour, float saturation);

        static float Luminance(Vector3 colour) { return Vector3::Dot(colour, LuminanceWeights()); }

    private:
        // The palette's sRGB to linear light (as the shaders' GAMMA).
        static constexpr float Gamma = 2.2f;
        // Rec. 709 luminance of linear RGB.
        static Vector3 LuminanceWeights() { return Vector3(0.2126f, 0.7152f, 0.0722f); }
        // Darker colours have no hue to keep: white.
        static constexpr float Black = 1e-4f;

        // The mean linear colour of rows [first, end) of the full turn's columns.
        static Vector3 Mean(const Formats::Dti& dti, int first, int end);

        static Vector3 Linear(const Formats::Palette& palette, int index);
    };
}
