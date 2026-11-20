#include "Mdk.Engine/Render/PointLights.h"

#include <algorithm>
#include <limits>

namespace Mdk::Engine::Render
{
    void PointLights::Add(const PointLight& light)
    {
        if (_count == Gathered || light.Radius <= 0.0f || light.Colour == Vector3::Zero())
        {
            return;
        }

        _lights[_count++] = light;
    }

    int PointLights::Pack(Vector3 camera, std::span<PointLight> into)
    {
        std::fill_n(_taken.begin(), _count, false);
        auto packed = 0;
        while (packed < static_cast<int>(into.size()) && packed < _count)
        {
            const auto best = Nearest(camera);
            _taken[best] = true;
            into[packed++] = _lights[best];
        }

        return packed;
    }

    int PointLights::Nearest(Vector3 camera)
    {
        auto best = -1;
        auto bestGap = std::numeric_limits<float>::max();
        for (int i = 0; i < _count; i++)
        {
            if (_taken[i])
            {
                continue;
            }

            const auto gap = Vector3::Distance(camera, _lights[i].Position) - _lights[i].Radius;
            if (gap < bestGap)
            {
                best = i;
                bestGap = gap;
            }
        }

        return best;
    }
}
