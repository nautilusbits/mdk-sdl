#include "Mdk.Game/Level/SkyLight.h"

#include <limits>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Game::Level
{
    SkyLight::Colours SkyLight::Of(const Formats::Dti& dti)
    {
        auto height = dti.Sky->Height;
        auto horizon = Math::Clamp(dti.SkyHorizonRow, 0, height);
        auto sky = horizon > 0 ? Mean(dti, 0, horizon) : Linear(*dti.Palette, dti.SkyTopColor);
        auto ground = horizon < height ? Mean(dti, horizon, height) : Linear(*dti.Palette, dti.SkyBottomColor);
        return Colours{sky, ground};
    }

    Vector3 SkyLight::Tint(Vector3 colour, float saturation)
    {
        auto luminance = Luminance(colour);
        if (luminance < Black)
        {
            return Vector3::One();
        }

        return Vector3::Lerp(Vector3::One(), colour / luminance, saturation);
    }

    Vector3 SkyLight::Mean(const Formats::Dti& dti, int first, int end)
    {
        const auto& sky = *dti.Sky;
        auto columns = Math::Min(dti.SkyWrapWidth, sky.Width);
        auto sum = Vector3::Zero();
        for (auto row = first; row < end; row++)
        {
            for (auto column = 0; column < columns; column++)
            {
                sum += Linear(*dti.Palette, sky.Indices[row * sky.Width + column]);
            }
        }

        return sum / static_cast<float>(Math::Max(1, (end - first) * columns));
    }

    Vector3 SkyLight::Linear(const Formats::Palette& palette, int index)
    {
        const auto& rgba = palette.Rgba;
        auto srgb = Vector3(rgba[index * 4], rgba[index * 4 + 1], rgba[index * 4 + 2]) / static_cast<float>(std::numeric_limits<byte>::max());
        return Vector3(MathF::Pow(srgb.X, Gamma), MathF::Pow(srgb.Y, Gamma), MathF::Pow(srgb.Z, Gamma));
    }
}
