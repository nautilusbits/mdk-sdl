#include "game/level/LevelLight.h"

#include <thread>
#include <vector>

#include "core/DotNet.h"

#include "game/kurt/Kurt.h"
#include "game/level/EnhancedLook.h"
#include "game/objects/LightSources.h"
#include "game/scripts/ScriptRuntime.h"

namespace Mdk::Game::Level
{
    LevelLight::LevelLight(Engine::Render::Renderer& renderer, const LevelData& level)
        : _renderer(renderer), _arenas(Light(level))
    {
    }

    std::unordered_map<std::string, Engine::Render::Lighting> LevelLight::Light(const LevelData& level)
    {
        // Parallel.For: the arenas in chunks, one thread per core (each arena's light is independent).
        std::vector<Engine::Render::Lighting> lightings(level.Arenas.size());
        auto cores = static_cast<size_t>(Math::Max(1u, std::thread::hardware_concurrency()));
        auto count = Math::Min(cores, lightings.size());
        std::vector<std::thread> threads;
        for (size_t t = 0; t < count; t++)
        {
            threads.emplace_back([&level, &lightings, t, count]
            {
                for (auto a = t; a < lightings.size(); a += count)
                {
                    lightings[a] = EnhancedLook::Lighting(*level.Dti, *level.Arenas[a]);
                }
            });
        }

        for (auto& thread : threads)
        {
            thread.join();
        }

        std::unordered_map<std::string, Engine::Render::Lighting> arenas;
        for (size_t a = 0; a < lightings.size(); a++)
        {
            if (!arenas.emplace(level.Arenas[a]->Name, lightings[a]).second)
            {
                throw ArgumentException("An item with the same key has already been added. Key: " + level.Arenas[a]->Name);
            }
        }

        return arenas;
    }

    void LevelLight::Update(Scripts::ScriptRuntime& scripts, Kurt::Kurt& kurt)
    {
        Objects::LightSources::Gather(_renderer.Lights(), scripts, kurt);
        auto it = _arenas.find(scripts.CurrentArena);
        if (it != _arenas.end())
        {
            _renderer.Lighting = it->second;
        }
    }
}
