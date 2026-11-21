#include "game/dev_tools/OverlayView.h"

#include "core/DotNet.h"
#include "core/Drawing.h"
#include "game/dev_tools/DevTextView.h"

namespace Mdk::Game::DevTools
{
    void OverlayView::Draw(const std::vector<std::string>& lines, float top)
    {
        auto widest = 0.f;
        for (size_t i = 0; i < lines.size(); i++)
        {
            widest = MathF::Max(widest, _text.Width(lines[i]));
        }

        auto width = widest + 2.f * Margin;
        auto height = static_cast<float>(lines.size()) * DevTextView::LineHeight + 2.f * Margin;
        _text.Fill(RectangleF(0.f, top, width, height), Background);
        auto baseline = top + Margin + Ascent;
        for (size_t i = 0; i < lines.size(); i++)
        {
            _text.Text(lines[i], Margin, baseline);
            baseline += DevTextView::LineHeight;
        }
    }
}
