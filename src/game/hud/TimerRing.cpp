#include "game/hud/TimerRing.h"

#include <algorithm>
#include <utility>

#include "formats/Texture.h"

namespace Mdk::Game::Hud
{
    void TimerRing::Wipe(std::span<byte> panel, const Formats::Texture& empty, float from, float to)
    {
        from = Wrap(from);
        to = Wrap(to);
        if (from >= to)
        {
            return;
        }

        if (to <= MathF::PI || from >= MathF::PI)
        {
            Fan(panel, empty, to, from);
            return;
        }

        Fan(panel, empty, UpperEnd, from);
        Fan(panel, empty, to, MathF::PI);
    }

    float TimerRing::Wrap(float angle)
    {
        while (angle < 0.f)
        {
            angle += MathF::Tau;
        }

        while (angle > MathF::Tau)
        {
            angle -= MathF::Tau;
        }

        return angle;
    }

    void TimerRing::Fan(std::span<byte> panel, const Formats::Texture& empty, float high, float low)
    {
        auto width = empty.Width;
        auto height = empty.Height;
        auto row = -width;

        // The lower half: the angles mirrored, rows going down.
        if (high > MathF::PI || low > MathF::PI)
        {
            std::tie(high, low) = std::pair(MathF::Tau - low, MathF::Tau - high);
            row = width;
        }

        auto leftStep = Slope(high, width);
        auto rightStep = Slope(low, width);
        auto limit = width << Fixed;
        auto left = (width >> 1) << Fixed;
        auto right = left;
        auto at = (height >> 1) * width;
        for (auto y = 1; y < height; y += 2)
        {
            left = std::max(left + leftStep, 0);
            right += rightStep;
            if (left >= limit || right < 0)
            {
                return;
            }

            right = std::min(right, limit - 1);
            for (auto x = left >> Fixed; x <= right >> Fixed; x++)
            {
                auto pixel = empty.Indices[at + x];
                if (pixel != 0)
                {
                    panel[at + x] = pixel;
                }
            }

            at += row;
        }
    }

    int TimerRing::Slope(float angle, int width)
    {
        if (angle >= NearUpLow && angle <= NearUpHigh)
        {
            return 0;
        }

        if (angle < NearZero)
        {
            return width << Fixed;
        }

        if (angle > NearPi)
        {
            return -(width << Fixed);
        }

        return ToInt(FixedOne / MathF::Tan(angle));
    }
}
