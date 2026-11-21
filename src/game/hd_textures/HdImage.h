#pragma once

#include <vector>

#include "core/DotNet.h"

namespace Mdk::Game::HdTextures
{
    // An image replacing a texture for the renderer: Frames frames of Width x Height stacked
    // downwards, RGBA8 premultiplied (as its colour textures). Made from a mod's files (Mods.ModImages).
    struct HdImage
    {
        int Width = 0;
        int Height = 0;
        int Frames = 0;
        std::vector<byte> Rgba;

        bool operator==(const HdImage&) const = default;
    };
}
