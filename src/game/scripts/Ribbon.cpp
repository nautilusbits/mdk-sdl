#include "game/scripts/Ribbon.h"

#include <span>

namespace Mdk::Game::Scripts
{
    void Ribbon::Push(Vector3 centre)
    {
        if (static_cast<int>(_centres.size()) == Length)
        {
            _centres.pop_front();
        }

        _centres.push_back(centre);
    }

    std::vector<Ribbon::Corner> Ribbon::Triangles()
    {
        std::vector<Corner> corners;
        Triangles(corners);
        return corners;
    }

    void Ribbon::Triangles(std::vector<Corner>& corners)
    {
        corners.clear();
        auto count = static_cast<int>(_centres.size());
        if (count < 2)
        {
            return;
        }

        auto edges = std::span<Edge>(_edges.data(), count);
        auto i = 0;
        for (auto c : _centres)
        {
            edges[i] = {c - Vector3::UnitZ() * HalfHeight * ScaleOf(i, count), c + Vector3::UnitZ() * HalfHeight * ScaleOf(i, count)};
            i++;
        }

        for (auto k = 1; k < static_cast<int>(edges.size()); k++)
        {
            auto [top, bottom] = k == 1 ? Rows[0] : k == static_cast<int>(edges.size()) - 1 ? Rows[2] : Rows[1];
            auto uv0 = Vector2(Left, top);
            auto uv1 = Vector2(Right, top);
            auto uv2 = Vector2(Right, bottom);
            auto uv3 = Vector2(Left, bottom);
            auto previous = edges[k - 1];
            auto current = edges[k];
            corners.push_back({previous.Bottom, uv0});
            corners.push_back({previous.Top, uv1});
            corners.push_back({current.Top, uv2});
            corners.push_back({previous.Bottom, uv0});
            corners.push_back({current.Bottom, uv3});
            corners.push_back({current.Top, uv2});
        }
    }
}
