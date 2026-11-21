#include "game/fall/FallPalette.h"

#include <algorithm>

namespace Mdk::Game::Fall
{
    namespace
    {
        constexpr int ByteMax = 255;
    }

    FallPalette::FallPalette(std::vector<byte> rgba)
        : Rgba(rgba), _base(std::move(rgba))
    {
    }

    bool FallPalette::Set(float brightness, float red, Zero zero)
    {
        auto applied = Applied{ Level(brightness), ToInt(MathF::Round(red * ByteMax)), zero };
        if (applied == _applied)
        {
            return false;
        }

        _applied = applied;
        auto level = applied.Level;
        auto add = applied.Red;
        auto first = zero == Zero::Kept ? 1 : 0;
        std::copy(_base.begin(), _base.end(), Rgba.begin());
        for (auto i = first; i < static_cast<int>(Rgba.size()) / Channels; i++)
        {
            auto at = i * Channels;
            if (i > 0 && i <= LastReddened)
            {
                Rgba[at] = static_cast<byte>(Math::Min(Rgba[at] + add, ByteMax));
            }

            for (auto c = 0; c < Channels - 1; c++)
            {
                Rgba[at + c] = Whiten(Rgba[at + c], level);
            }
        }

        return true;
    }

    Vector4 FallPalette::Apply(Vector4 colour) const
    {
        auto level = _applied.Level;
        auto add = _applied.Red;
        auto red = MathF::Min(colour.X + add / static_cast<float>(ByteMax), 1.f);
        auto keep = level / static_cast<float>(Unit);
        auto white = (Unit - level) / static_cast<float>(Unit);
        return Vector4(Vector3(red, colour.Y, colour.Z) * keep + Vector3(white), colour.W);
    }
}
