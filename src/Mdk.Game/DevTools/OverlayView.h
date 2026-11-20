#pragma once

#include <string>
#include <vector>

#include "Mdk.Core/Numerics.h"

namespace Mdk::Game::DevTools
{
    class DevTextView;

    // Draws the debug overlay's lines (OverlayText) at the top left (under the console when it's
    // open), on a dark box.
    class OverlayView
    {
    public:
        explicit OverlayView(DevTextView& text) : _text(text) {}

        void Draw(const std::vector<std::string>& lines, float top);

    private:
        static constexpr float Margin = 3.f;
        // The font's ascent at the view's scale: the first baseline is this far down.
        static constexpr float Ascent = 7.f;
        static constexpr Vector4 Background{0.f, 0.f, 0.f, 0.6f};

        DevTextView& _text;
    };
}
