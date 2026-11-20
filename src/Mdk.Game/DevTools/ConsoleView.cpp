#include "Mdk.Game/DevTools/ConsoleView.h"

#include <algorithm>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Engine/Diagnostics/LogRing.h"
#include "Mdk.Game/DevTools/DevConsole.h"
#include "Mdk.Game/DevTools/TechInfo.h"

namespace Mdk::Game::DevTools
{
    const std::string& ConsoleView::Version()
    {
        static const std::string version = "MDK SDL " + BuildInfo::Current().Version;
        return version;
    }

    float ConsoleView::Bottom(const DevConsole& console)
    {
        return Height() * console.Slide();
    }

    RectangleF ConsoleView::CloseArea(float bottom, RectangleF safe)
    {
        auto top = MathF::Max(bottom - Height(), safe.Top()) + Margin;
        return RectangleF(safe.Right() - Margin - CloseSize, top, CloseSize, CloseSize);
    }

    ConsoleSpot ConsoleView::Spot(Vector2 point, float bottom, RectangleF close)
    {
        if (point.Y > bottom)
        {
            return ConsoleSpot::Below;
        }

        if (close.Contains(point.X, point.Y))
        {
            return ConsoleSpot::Close;
        }

        return point.Y >= bottom - LineArea ? ConsoleSpot::Line : ConsoleSpot::Log;
    }

    ConsoleSpot ConsoleView::Spot(const DevConsole& console, Vector2 point)
    {
        auto bottom = Bottom(console);
        return Spot(point, bottom, CloseArea(bottom, _text.Renderer().CanvasSafeArea()));
    }

    void ConsoleView::Draw(const DevConsole& console, Engine::Diagnostics::LogRing& log)
    {
        if (console.Slide() <= 0.f)
        {
            return;
        }

        auto width = _text.Renderer().CanvasWidth();
        auto height = Height();
        auto bottom = Bottom(console);
        _text.Fill(RectangleF(0.f, bottom - height, width, height), Background);
        _text.Fill(RectangleF(0.f, bottom, width, EdgeHeight), Edge);

        // The line typed, then the log upwards from the newest line not scrolled away.
        auto baseline = bottom - Margin;
        if (_typed != console.Line())
        {
            _typed = console.Line();
            _prompt = DevConsole::Prompt + console.Line() + Cursor;
        }

        _text.Text(_prompt, Margin, baseline);
        DrawClose(bottom);
        _text.Text(Version(), width - Margin - _text.Width(Version()), baseline);
        auto rows = ToInt((height - Margin) / DevTextView::LineHeight) - 1;
        log.CopyLast(rows + console.Scroll(), _lines);
        auto newest = std::max(static_cast<int>(_lines.size()) - console.Scroll(), 0);
        for (auto i = newest - 1; i >= std::max(newest - rows, 0); i--)
        {
            baseline -= DevTextView::LineHeight;
            _text.Text(_lines[i], Margin, baseline);
        }
    }

    void ConsoleView::DrawClose(float bottom)
    {
        if (CloseButton == CloseButton::Hidden)
        {
            return;
        }

        auto area = CloseArea(bottom, _text.Renderer().CanvasSafeArea());
        _text.Fill(area, CloseFill);
        auto span = CloseSize - CrossInset * 2.f;
        auto square = span / CrossSquares;
        for (auto i = 0; i < CrossSquares; i++)
        {
            auto x = area.X + CrossInset + square * i;
            _text.Fill(RectangleF(x, area.Y + CrossInset + square * i, square, square), Edge);
            _text.Fill(RectangleF(x, area.Y + CrossInset + span - square * (i + 1), square, square), Edge);
        }
    }
}
