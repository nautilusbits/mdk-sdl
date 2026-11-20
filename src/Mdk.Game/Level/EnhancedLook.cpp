#include "Mdk.Game/Level/EnhancedLook.h"

#include <limits>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/Angles.h"
#include "Mdk.Game/Level/ArenaShape.h"
#include "Mdk.Game/Level/SkyLight.h"

namespace Mdk::Game::Level
{
    using Engine::Render::Shadows;

    Vector3 EnhancedLook::SunDirection()
    {
        static const Vector3 sunDirection = Sun(SunPitch, SunYaw);
        return sunDirection;
    }

    Vector3 EnhancedLook::Sun(float pitch, float yaw)
    {
        auto p = DegreesToRadians(pitch);
        auto y = DegreesToRadians(yaw);
        auto godot = Vector3(-MathF::Cos(p) * MathF::Sin(y), MathF::Sin(p), -MathF::Cos(p) * MathF::Cos(y));
        return Vector3(godot.X, -godot.Z, godot.Y);
    }

    Engine::Render::Lighting EnhancedLook::Lighting(const Formats::Dti& dti, const Formats::Arena& arena)
    {
        auto colours = SkyLight::Of(dti);
        auto cover = ArenaShape::CoverOf(arena);
        Vector3 sun, sky, ground;
        if (cover == Cover::Open)
        {
            auto strength = MathF::Lerp(SunWeakest, SunStrongest, Math::Clamp(SkyLight::Luminance(colours.Sky) / BrightSky, 0.f, 1.f));
            sun = SkyLight::Tint(colours.Sky, SunSaturation) * strength;
            sky = SkyLight::Tint(colours.Sky, AmbientSaturation) * OpenAbove;
            ground = SkyLight::Tint(colours.Ground, AmbientSaturation) * OpenBelow;
        }
        else
        {
            auto tint = SkyLight::Tint(colours.Ground, CoveredSaturation);
            sun = Vector3::Zero();
            sky = tint * CoveredAbove;
            ground = tint * CoveredBelow;
        }

        // The hemisphere is linear in the up component: its mean is at the faces' mean up.
        auto up = ArenaShape::MeanUp(arena) * 0.5f + 0.5f;
        auto mean = Vector3::Lerp(ground, sky, up) + sun * ArenaShape::MeanFacing(arena, SunDirection()) * SunlitShare;
        auto exposure = MeanLight / MathF::Max(SkyLight::Luminance(mean), std::numeric_limits<float>::denorm_min());

        constexpr auto byteMax = static_cast<float>(std::numeric_limits<byte>::max());
        auto at = dti.SkyBottomColor * 4;
        const auto& rgba = dti.Palette->Rgba;
        auto horizon = Vector4(rgba[at], rgba[at + 1], rgba[at + 2], byteMax) / byteMax;
        auto shadows = cover == Cover::Open ? Shadows::On : Shadows::Off;
        return Engine::Render::Lighting{SunDirection(), sun, sky, ground, exposure, shadows, ShadowDistance, GlowBloom, horizon, HazeDensity};
    }
}
