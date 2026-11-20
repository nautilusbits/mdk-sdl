#pragma once

namespace Mdk::Engine::Render
{
    // A draw's share of the depth range: the front's draws cover the back's whatever their
    // depth. The original has no depth buffer: it draws the other arena first and the camera's over
    // it (0x41df35), so the camera's arena (front) covers the other one (back).
    enum class DepthBand { Front, Back };

    // The bands' depth ranges, set as the viewport's (the depth tested within a band). The
    // screen passes map them back to the whole range (screen.hlsli's BAND_SPLIT).
    //   0 ──── front ──── Split ──── back ──── 1
    struct DepthBands
    {
        static constexpr float Split = 0.5f;

        // C#'s (float Min, float Max) tuple.
        struct Range
        {
            float Min = 0;
            float Max = 0;
        };

        static constexpr Range Of(DepthBand band) { return band == DepthBand::Front ? Range{0.0f, Split} : Range{Split, 1.0f}; }
    };
}
