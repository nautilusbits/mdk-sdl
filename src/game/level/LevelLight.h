#pragma once

#include <string>
#include <unordered_map>

#include "engine/render/Renderer.h"
#include "game/level/LevelData.h"

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;
}

namespace Mdk::Game::Kurt
{
    class Kurt;
}

namespace Mdk::Game::Level
{
    // The enhanced look's light as the camera goes from arena to arena: each arena's own
    // (EnhancedLook.Lighting), made once at the level's load; and the frame's point
    // lights (LightSources).
    class LevelLight
    {
    public:
        LevelLight(Engine::Render::Renderer& renderer, const LevelData& level);

        // Lights the scene as the scripts' current arena (unknown: unchanged), with this
        // frame's point lights.
        void Update(Scripts::ScriptRuntime& scripts, Kurt::Kurt& kurt);

    private:
        Engine::Render::Renderer& _renderer;
        std::unordered_map<std::string, Engine::Render::Lighting> _arenas;

        // Each arena's light, on every core: each one averages the whole sky panorama.
        static std::unordered_map<std::string, Engine::Render::Lighting> Light(const LevelData& level);
    };
}
