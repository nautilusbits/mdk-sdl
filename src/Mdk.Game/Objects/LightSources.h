#pragma once

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/PointLights.h"

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;
}

namespace Mdk::Game::Kurt
{
    class Kurt;
}

namespace Mdk::Game::Objects
{
    // The game's point lights for the enhanced look, gathered each frame: Kurt's muzzle flash
    // (it flickers with the flash sprite), sniper rounds in flight, explosions (fading as their
    // animation plays) and spawn_box fires (flickering).
    //   Kurt.Muzzle ──────────────┐
    //   SniperRounds.Visuals ─────┤
    //   explosions (EffectFrames) ┼──► PointLights ──► Renderer
    //   Boxes "FIRE" ─────────────┘
    struct LightSources
    {
        // A fire dims by up to this share as it flickers.
        static constexpr float FlickerDepth = 0.3f;

        // Fills lights with this frame's lights.
        static void Gather(Engine::Render::PointLights& lights, Scripts::ScriptRuntime& scripts, Kurt::Kurt& kurt);

        // An explosion's strength: 1 as it starts, 0 once its frames have played.
        static float Fade(float time, int frames) { return Math::Clamp(1.f - time / frames, 0.f, 1.f); }

        // A fire's strength at a tick: 1 - depth to 1, beating differently per seed.
        static float Flicker(int tick, int seed);

    private:
        static constexpr const char* FireTexture = "FIRE";
        // Colours (linear, strength included) and reaches (units; Kurt is 5 tall).
        static Vector3 MuzzleColour() { return Vector3(1.f, 0.75f, 0.4f) * 6.f; }
        static constexpr float MuzzleReach = 30.f;
        // The chain gun is about this high above Kurt's feet.
        static constexpr float MuzzleHeight = 3.5f;
        static Vector3 RoundColour() { return Vector3(1.f, 0.9f, 0.6f) * 3.f; }
        static constexpr float RoundReach = 15.f;
        static Vector3 ExplosionColour() { return Vector3(1.f, 0.55f, 0.2f) * 20.f; }
        static constexpr float ExplosionReach = 60.f;
        static Vector3 FireColour() { return Vector3(1.f, 0.5f, 0.15f) * 3.f; }
        // A fire reaches this many times its box's widest side, at least the minimum.
        static constexpr float FireReachPerSize = 4.f;
        static constexpr float FireReachMin = 20.f;
        // Flicker: radians per tick, and between fires (pseudo-random phases).
        static constexpr float FlickerSpeed = 1.3f;
        static constexpr float FlickerPhase = 2.1f;
        static constexpr float FlickerBeat = 0.37f;
    };
}
