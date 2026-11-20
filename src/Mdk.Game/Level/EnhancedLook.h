#pragma once

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Formats/Arena.h"
#include "Mdk.Formats/Dti.h"
#include "Mdk.Game/Flow/Settings.h"

namespace Mdk::Game::Level
{
    // The enhanced look of a level (after the Godot port's Level._enhance): each
    // arena's light from the level's sky (SkyLight). Arenas under the sky get a sun
    // (stronger under a brighter sky) with shadows, the sky's light from above and the ground's from
    // below; covered ones (ArenaShape) no sun, more light all around. The exposure evens
    // each arena's mean light to the original's unlit textures. A light haze in the colour of the
    // sky's horizon. Arenas and objects are lit, sprites only filtered.
    //   sky panorama ─► mean above / below the horizon ─► sun, sky, ground colours ─┐
    //   arena faces ──► covered? mean up, mean facing the sun ─────────────────────┴─► Lighting
    struct EnhancedLook
    {
    private:
        // The sun's rotation as the Godot port's light (degrees; pitch about X, then yaw about
        // Y; Godot's Y up, the light shining along its -Z).
        static constexpr float SunPitch = -55.f;
        static constexpr float SunYaw = 35.f;
        static constexpr float ShadowDistance = 300.f;
        static constexpr float GlowBloom = 0.f;
        static constexpr float HazeDensity = 0.0002f;
        // The sun's strength: from the weakest under a black sky to the strongest under a sky
        // this bright (linear luminance) or brighter; its hue a little of the sky's.
        static constexpr float SunWeakest = 0.4f;
        static constexpr float SunStrongest = 1.f;
        static constexpr float BrightSky = 0.3f;
        static constexpr float SunSaturation = 0.25f;
        // Under the sky: its light from above, the ground's from below, a third of their hue.
        static constexpr float OpenAbove = 0.6f;
        static constexpr float OpenBelow = 0.35f;
        static constexpr float AmbientSaturation = 0.3f;
        // Under a roof: light from above and below, a little of the ground's hue.
        static constexpr float CoveredAbove = 1.f;
        static constexpr float CoveredBelow = 0.7f;
        static constexpr float CoveredSaturation = 0.15f;

    public:
        // The share of faces turned to the sun that it reaches (the rest is shadowed), for
        // the exposure's mean light.
        static constexpr float SunlitShare = 0.7f;
        // The mean light the exposure gives an arena's faces (1: the original's textures).
        static constexpr float MeanLight = 1.15f;

        // The way the sunlight goes (MDK coordinates, Z up).
        static Vector3 SunDirection();

        // The way a Godot light rotated by pitch and yaw degrees shines, in MDK coordinates:
        // Godot (x, y, z) is MDK (x, -z, y).
        //   Godot: Ry(yaw) · Rx(pitch) · (0, 0, -1) = (-cos p · sin y, sin p, -cos p · cos y)
        static Vector3 Sun(float pitch, float yaw);

        // An arena's light in the level of dti; the haze takes the colour
        // below the sky panorama (its horizon).
        static Engine::Render::Lighting Lighting(const Formats::Dti& dti, const Formats::Arena& arena);

        // How the level's surfaces are shaded in a look.
        static Engine::Render::Shading Surfaces(Flow::Graphics graphics)
        {
            return graphics == Flow::Graphics::Enhanced ? Engine::Render::Shading::Lit : Engine::Render::Shading::Original;
        }

        // How sprites (Kurt, effects) are shaded in a look.
        static Engine::Render::Shading Sprites(Flow::Graphics graphics)
        {
            return graphics == Flow::Graphics::Enhanced ? Engine::Render::Shading::Sprite : Engine::Render::Shading::Original;
        }

        // How the sky is sampled in a look.
        static Engine::Render::Sampling Sky(Flow::Graphics graphics)
        {
            return graphics == Flow::Graphics::Enhanced ? Engine::Render::Sampling::Linear : Engine::Render::Sampling::Nearest;
        }
    };
}
