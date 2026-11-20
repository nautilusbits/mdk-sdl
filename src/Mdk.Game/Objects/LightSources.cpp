#include "Mdk.Game/Objects/LightSources.h"

#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/ScriptMath.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Mdk.Game/Scripts/SniperRounds.h"

namespace Mdk::Game::Objects
{
    using Engine::Render::PointLight;

    void LightSources::Gather(Engine::Render::PointLights& lights, Scripts::ScriptRuntime& scripts, Kurt::Kurt& kurt)
    {
        lights.Clear();
        if (kurt.Muzzle.has_value())
        {
            lights.Add(PointLight{kurt.Feet + Vector3::UnitZ() * MuzzleHeight, MuzzleColour(), MuzzleReach});
        }

        const auto& rounds = scripts.SniperRounds->Visuals();
        for (size_t i = 0; i < rounds.size(); i++)
        {
            lights.Add(PointLight{rounds[i]->Position, RoundColour(), RoundReach});
        }

        const auto& objects = scripts.Objects;
        for (size_t i = 0; i < objects.size(); i++)
        {
            const auto& obj = objects[i];
            if (obj->Dead || obj->EffectFrames <= 0)
            {
                continue;
            }

            auto reach = ExplosionReach * MathF::Max(obj->Scale, 1.f);
            lights.Add(PointLight{obj->Position, ExplosionColour() * Fade(obj->EffectTime, obj->EffectFrames), reach});
        }

        const auto& boxes = scripts.Boxes();
        for (size_t i = 0; i < boxes.size(); i++)
        {
            const auto& box = boxes[i];
            if (box->Dead || !box->Visible || box->Model == nullptr || box->Model->Name != FireTexture)
            {
                continue;
            }

            auto size = Scripts::ScriptMath::Size(box->Model->Bounds);
            auto reach = MathF::Max(MathF::Max(size.X, size.Y) * FireReachPerSize, FireReachMin);
            lights.Add(PointLight{box->Position, FireColour() * Flicker(scripts.TickCount(), static_cast<int>(i)), reach});
        }
    }

    float LightSources::Flicker(int tick, int seed)
    {
        auto phase = tick * FlickerSpeed + seed * FlickerPhase;
        auto wave = MathF::Sin(phase) * MathF::Sin(phase * FlickerBeat + seed);
        return 1.f - FlickerDepth * (0.5f + 0.5f * wave);
    }
}
