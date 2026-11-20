#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Drawing.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/Texture.h"

namespace Mdk::Game::Fall
{
    // The fall's ground (0x41357c): not 3D, the texture LEVELn (1024²) mapped affinely
    // onto the view, zooming in as the camera drops and scrolling down the screen, with the
    // minecrawler's track (PODn) written into it under and behind the crawler.
    //   u = 512 + 0.36 cx + S (sx - w/2)      S = cz / 5280 texels per pixel
    //   v = A   - 0.36 cy + S (sy - 180)      A = 824 - 624 t / 33 (the crawler's row)
    //
    //   track rows from R = round(A - 33.75):   ┌──┐  16 + 2j/3 texels each side of 512 (row R + j)
    //                                          ┌┘  └┐ 32 from 24 rows on
    class FallGround
    {
    public:
        static constexpr int Size = 1024;
        // The camera height at which one texel is one pixel.
        static constexpr float UnitHeight = 5280.f;

        // The ground with the track as written so far.
        std::vector<byte> Indices;

        FallGround(const Formats::Texture& ground, std::shared_ptr<Formats::Texture> pod);

        // The texel row at the view's centre after time seconds of fall.
        static float CentreRow(float time);

        // Half the track's width at row j from its start.
        static int TrackWidth(int j) { return j < TrackRamp ? ToInt(MathF::Round(TrackNarrow + TrackWiden * j)) : TrackWide; }

        // Writes the track from the crawler's row down; false when nothing changed.
        bool UpdateTrack(float centreRow);

        // The part of the view (view pixels, centred across, 0-360 at 4:3, taller on a narrower
        // screen) the texture covers, and its texels; empty when the view is off the texture.
        static std::pair<RectangleF, RectangleF> Visible(Vector3 camera, float centreRow, RectangleF view);

        // The minecrawler on the screen: its centre (at texel (512, A)) and its scale.
        static std::pair<Vector2, float> Crawler(Vector3 camera, float width);

    private:
        static constexpr float CentreColumn = 512.f;
        static constexpr float Parallax = 0.36f;
        static constexpr float StartRow = 824.f;
        static constexpr float RowsOverFall = 624.f;
        // The track starts this far ahead of the crawler's row (108 × 80 / 256).
        static constexpr float TrackAhead = 108.f * 80.f / 256.f;
        // The track widens over 24 rows from 16 texels each side to 32.
        static constexpr int TrackRamp = 24;
        static constexpr float TrackNarrow = 16.f;
        static constexpr float TrackWiden = 2.f / 3.f;
        static constexpr int TrackWide = 32;
        static constexpr float ViewCentreY = 180.f;

        std::vector<byte> _ground;
        std::shared_ptr<Formats::Texture> _pod;
        // The first track row written so far (none yet: the bottom).
        int _trackRow = Size;
    };
}
