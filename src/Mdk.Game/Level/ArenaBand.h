#pragma once

#include <string>

#include "Mdk.Engine/Render/DepthBands.h"

namespace Mdk::Game::Level
{
    // An arena's depth band: the camera's arena, its objects and effects in front, the other
    // drawn arena's behind (the original draws the camera's over the other, 0x41df35). Kurt's sprite
    // and board are drawn in front.
    struct ArenaBand
    {
        static Engine::Render::DepthBand Of(const std::string& arena, const std::string& camera)
        {
            return arena == camera ? Engine::Render::DepthBand::Front : Engine::Render::DepthBand::Back;
        }
    };
}
