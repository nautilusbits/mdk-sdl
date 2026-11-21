#include "game/fall/FallGround.h"

#include "game/fall/FallSim.h"

#include <algorithm>

namespace Mdk::Game::Fall
{
    FallGround::FallGround(const Formats::Texture& ground, std::shared_ptr<Formats::Texture> pod)
        : _ground(ground.Indices), _pod(std::move(pod))
    {
        Indices = _ground;
    }

    // The texel row at the view's centre after time seconds of fall.
    float FallGround::CentreRow(float time)
    {
        return StartRow - RowsOverFall * time / FallSim::EndTime;
    }

    bool FallGround::UpdateTrack(float centreRow)
    {
        auto row = Math::Clamp(ToInt(MathF::Round(centreRow - TrackAhead)), 0, Size);
        if (row >= _trackRow)
        {
            return false;
        }

        // Rows within 24 of the new start, and the ones that were within 24 of the old one.
        auto end = Math::Min(_trackRow + TrackRamp, Size);
        for (auto y = row; y < end; y++)
        {
            auto w = TrackWidth(y - row);
            auto podX = _pod->Width / 2 - w;
            std::copy_n(_pod->Indices.begin() + (y * _pod->Width + podX), 2 * w, Indices.begin() + (y * Size + static_cast<int>(CentreColumn) - w));
        }

        _trackRow = row;
        return true;
    }

    std::pair<RectangleF, RectangleF> FallGround::Visible(Vector3 camera, float centreRow, RectangleF view)
    {
        auto s = camera.Z / UnitHeight;
        auto u0 = CentreColumn + Parallax * camera.X - s * view.Width / 2.f;
        auto v0 = centreRow - Parallax * camera.Y + s * (view.Y - ViewCentreY);
        auto texels = RectangleF::Intersect(RectangleF(u0, v0, s * view.Width, s * view.Height), RectangleF(0.f, 0.f, Size, Size));
        if (texels.Width <= 0.f || texels.Height <= 0.f || s <= 0.f)
        {
            return { RectangleF::Empty(), RectangleF::Empty() };
        }

        auto screen = RectangleF(view.X + (texels.X - u0) / s, view.Y + (texels.Y - v0) / s, texels.Width / s, texels.Height / s);
        return { screen, texels };
    }

    std::pair<Vector2, float> FallGround::Crawler(Vector3 camera, float width)
    {
        auto s = camera.Z / UnitHeight;
        constexpr float CrawlerScale = 0.75f;
        return { Vector2(width / 2.f - Parallax * camera.X / s, ViewCentreY + Parallax * camera.Y / s), CrawlerScale / s };
    }
}
