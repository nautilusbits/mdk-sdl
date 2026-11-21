#include "game/menu/TouchEditor.h"

#include <algorithm>
#include <span>

#include "core/DotNet.h"

namespace Mdk::Game::Menu
{
    using Engine::Platform::TouchLayout;
    using DevTools::Switch;

    TouchEditor::TouchEditor(const std::map<TouchPart, TouchPlace>& saved)
        : _places(saved), _saved(saved)
    {
    }

    std::string TouchEditor::LabelOf(Tool tool)
    {
        for (const auto& slot : ToolSlots)
        {
            if (slot.Tool == tool)
            {
                return slot.Label;
            }
        }

        for (const auto& slot : PromptSlots)
        {
            if (slot.Tool == tool)
            {
                return slot.Label;
            }
        }

        throw InvalidOperationException("Sequence contains no matching element");
    }

    bool TouchEditor::Changed() const
    {
        if (_places.size() != _saved.size())
        {
            return true;
        }

        for (const auto& [part, place] : _places)
        {
            auto saved = _saved.find(part);
            if (saved == _saved.end() || saved->second != place)
            {
                return true;
            }
        }

        return false;
    }

    std::optional<RectangleF> TouchEditor::Handle() const
    {
        if (!_selected)
        {
            return std::nullopt;
        }

        return HandleOf(_areas.at(*_selected));
    }

    void TouchEditor::Measure(Vector2 screen, RectangleF safe, float unitsPerMm)
    {
        if (_measured && screen == _screen && safe == _safe && unitsPerMm == _unitsPerMm)
        {
            return;
        }

        _screen = screen;
        _safe = safe;
        _unitsPerMm = unitsPerMm;
        _measured = true;
        _grab = Grab::None;
        for (auto part : TouchLayout::Parts())
        {
            _areas[part] = TouchLayout::AreaOf(part, _places, screen, safe, unitsPerMm);
        }

        LayTools();
        AvoidButtons();
    }

    TouchEditor::Outcome TouchEditor::Press(Vector2 at)
    {
        for (const auto& [tool, area] : _tools)
        {
            if (area.Contains(at.X, at.Y))
            {
                return Use(tool);
            }
        }

        if (_confirming)
        {
            return Outcome::Editing;
        }

        auto handle = Handle();
        if (handle && handle->Contains(at.X, at.Y))
        {
            auto area = _areas.at(*_selected);
            auto [left, top] = HandleCorner(area);
            _anchor = Vector2(left ? area.Right() : area.Left(), top ? area.Bottom() : area.Top());
            _offset = Vector2(left ? area.Left() : area.Right(), top ? area.Top() : area.Bottom()) - at;
            _grab = Grab::Resize;
            return Outcome::Editing;
        }

        _selected = std::nullopt;
        _grab = Grab::None;
        for (auto part : TouchLayout::Parts())
        {
            auto area = _areas.at(part);
            if (area.Contains(at.X, at.Y))
            {
                _selected = part;
                _anchor = at - Vector2(area.X, area.Y);
                _grab = Grab::Move;
                break;
            }
        }

        return Outcome::Editing;
    }

    void TouchEditor::Drag(Vector2 at)
    {
        if (_grab == Grab::None || !_selected)
        {
            return;
        }

        auto part = *_selected;
        auto area = _areas.at(part);
        if (_grab == Grab::Move)
        {
            auto corner = at - _anchor;
            Change(part, RectangleF(corner.X, corner.Y, area.Width, area.Height));
        }
        else
        {
            Change(part, Resized(part, at + _offset));
        }

        // The row gives way: to the edge away from the dragged control.
        auto bounds = Bounds();
        auto moved = _areas.at(part);
        MoveTools(moved.Y + moved.Height / 2.f < bounds.Y + bounds.Height / 2.f ? Edge::Bottom : Edge::Top);
    }

    void TouchEditor::Release()
    {
        if (_grab != Grab::None)
        {
            AvoidButtons();
        }

        _grab = Grab::None;
    }

    void TouchEditor::MoveTools(Edge edge)
    {
        if (edge == _edge && !_rowTop && _rowAcross == Across::Centre)
        {
            return;
        }

        _edge = edge;
        _rowTop = std::nullopt;
        _rowAcross = Across::Centre;
        LayTools();
    }

    void TouchEditor::AvoidButtons()
    {
        if (_confirming || !CoversButton())
        {
            return;
        }

        auto edge = _edge;
        auto top = ToolsArea().Top();
        MoveTools(edge == Edge::Top ? Edge::Bottom : Edge::Top);
        if (!CoversButton())
        {
            return;
        }

        // Every height a gap apart, the nearest to where the row was first.
        auto bounds = Bounds();
        auto height = ToolsArea().Height;
        auto step = GapMm * TouchLayout::UnitsPerMm(_unitsPerMm, _screen);
        std::vector<float> tries;
        for (auto y = bounds.Top() + step; y + height <= bounds.Bottom() - step && step > 0.f; y += step)
        {
            tries.push_back(y);
        }

        std::stable_sort(tries.begin(), tries.end(), [top](float a, float b) { return MathF::Abs(a - top) < MathF::Abs(b - top); });
        for (auto y : tries)
        {
            for (auto across : {Across::Centre, Across::Left, Across::Right})
            {
                _rowTop = y;
                _rowAcross = across;
                LayTools();
                if (!CoversButton())
                {
                    return;
                }
            }
        }

        MoveTools(edge);
    }

    bool TouchEditor::CoversButton() const
    {
        auto tools = ToolsArea();
        for (auto p : TouchLayout::Parts())
        {
            if (TouchLayout::IsButton(p) && _areas.at(p).IntersectsWith(tools))
            {
                return true;
            }
        }

        return false;
    }

    void TouchEditor::Grow(int step)
    {
        if (!_selected)
        {
            return;
        }

        auto part = *_selected;
        auto area = _areas.at(part);
        auto by = step * MathF::Max(StepMm * TouchLayout::UnitsPerMm(_unitsPerMm, _screen), StepPart * MathF::Max(area.Width, area.Height));
        area.Inflate(by / 2.f, by / 2.f);
        Change(part, area);
    }

    void TouchEditor::Select(int step)
    {
        const auto& parts = TouchLayout::Parts();
        auto count = static_cast<int>(parts.size());
        int index;
        if (_selected)
        {
            auto found = std::find(parts.begin(), parts.end(), *_selected);
            auto at = found == parts.end() ? -1 : static_cast<int>(found - parts.begin());
            index = at + step;
        }
        else
        {
            index = step > 0 ? 0 : count - 1;
        }

        _selected = parts[(index % count + count) % count];
    }

    void TouchEditor::Reset()
    {
        _places.clear();
        _measured = false;
        Measure(_screen, _safe, _unitsPerMm);
    }

    TouchEditor::Outcome TouchEditor::Cancel()
    {
        if (_confirming)
        {
            Ask(Switch::Off);
            return Outcome::Editing;
        }

        if (!Changed())
        {
            return Outcome::Cancelled;
        }

        Ask(Switch::On);
        return Outcome::Editing;
    }

    void TouchEditor::Ask(Switch asking)
    {
        _confirming = asking == Switch::On;
        _grab = Grab::None;
        LayTools();
    }

    TouchEditor::Outcome TouchEditor::Use(Tool tool)
    {
        switch (tool)
        {
            case Tool::Reset:
                Reset();
                break;
            case Tool::Shrink:
                Grow(-1);
                break;
            case Tool::Grow:
                Grow(1);
                break;
            case Tool::Cancel:
                return Cancel();
            case Tool::Done:
                return Outcome::Done;
            case Tool::Discard:
                return Outcome::Cancelled;
            case Tool::Keep:
                Ask(Switch::Off);
                break;
        }

        return Outcome::Editing;
    }

    void TouchEditor::Change(TouchPart part, RectangleF area)
    {
        if (Bounds().Width <= 0.f || Bounds().Height <= 0.f)
        {
            return;
        }

        auto fitted = TouchLayout::Fit(part, area, _screen, _safe, _unitsPerMm);
        _areas[part] = fitted;
        _places[part] = TouchLayout::PlaceOf(fitted, _screen, _safe, _unitsPerMm);
    }

    RectangleF TouchEditor::Resized(TouchPart part, Vector2 at) const
    {
        auto width = MathF::Abs(at.X - _anchor.X);
        auto height = MathF::Abs(at.Y - _anchor.Y);
        if (TouchLayout::IsButton(part))
        {
            width = height = MathF::Max(width, height);
        }

        // Fitted first, so the staying corner stays where the size is kept.
        auto size = TouchLayout::Fit(part, RectangleF(0.f, 0.f, width, height), _screen, _safe, _unitsPerMm).Size();
        auto x = at.X < _anchor.X ? _anchor.X - size.Width : _anchor.X;
        auto y = at.Y < _anchor.Y ? _anchor.Y - size.Height : _anchor.Y;
        return RectangleF(PointF{x, y}, size);
    }

    std::tuple<bool, bool> TouchEditor::HandleCorner(RectangleF area) const
    {
        auto bounds = Bounds();
        return {area.X + area.Width / 2.f > bounds.X + bounds.Width / 2.f, area.Y + area.Height / 2.f > bounds.Y + bounds.Height / 2.f};
    }

    RectangleF TouchEditor::HandleOf(RectangleF area) const
    {
        auto side = HandleMm * TouchLayout::UnitsPerMm(_unitsPerMm, _screen);
        auto [left, top] = HandleCorner(area);
        auto corner = Vector2(left ? area.Left() : area.Right(), top ? area.Top() : area.Bottom());

        // Inside the safe area, half a handle off its edges (a zone's corner is on the screen's
        // edge, where a phone's gestures are).
        auto bounds = Bounds();
        auto x = Math::Clamp(corner.X - side / 2.f, bounds.Left() + side / 2.f, MathF::Max(bounds.Right() - 1.5f * side, bounds.Left()));
        auto y = Math::Clamp(corner.Y - side / 2.f, bounds.Top() + side / 2.f, MathF::Max(bounds.Bottom() - 1.5f * side, bounds.Top()));
        return RectangleF(x, y, side, side);
    }

    RectangleF TouchEditor::ToolsArea() const
    {
        if (_tools.empty())
        {
            return RectangleF::Empty();
        }

        return RectangleF::FromLTRB(_tools.front().Area.Left(), _tools.front().Area.Top(), _tools.back().Area.Right(), _tools.back().Area.Bottom());
    }

    RectangleF TouchEditor::Bounds() const
    {
        return _safe.Width > 0.f && _safe.Height > 0.f ? _safe : RectangleF(0.f, 0.f, _screen.X, _screen.Y);
    }

    void TouchEditor::LayTools()
    {
        _tools.clear();
        auto bounds = Bounds();
        auto units = TouchLayout::UnitsPerMm(_unitsPerMm, _screen);
        std::span<const ToolSlot> slots = _confirming ? std::span<const ToolSlot>(PromptSlots) : std::span<const ToolSlot>(ToolSlots);
        auto sum = 0.f;
        for (const auto& slot : slots)
        {
            sum += slot.Width;
        }

        auto widths = sum * ToolMm + static_cast<float>(slots.size() - 1) * GapMm;
        auto scale = MathF::Min(units, ToolsPart * bounds.Width / widths);
        auto height = ToolMm * scale;
        auto gap = GapMm * scale;
        float x;
        if (_confirming)
        {
            x = bounds.X + (bounds.Width - widths * scale) / 2.f;
        }
        else if (_rowAcross == Across::Left)
        {
            x = bounds.X + gap;
        }
        else if (_rowAcross == Across::Right)
        {
            x = bounds.Right() - gap - widths * scale;
        }
        else
        {
            x = bounds.X + (bounds.Width - widths * scale) / 2.f;
        }

        auto y = _confirming ? bounds.Y + (bounds.Height - height) / 2.f
            : _rowTop.value_or(_edge == Edge::Top ? bounds.Y + gap : bounds.Bottom() - gap - height);
        for (const auto& slot : slots)
        {
            auto area = RectangleF(x, y, slot.Width * ToolMm * scale, height);
            _tools.push_back({slot.Tool, area});
            x = area.Right() + gap;
        }

        // The help line (the question's, asked): under (over) the row, as wide, towards the middle.
        auto line = height * HelpPart;
        auto helpY = y + height / 2.f > bounds.Y + bounds.Height / 2.f || _confirming ? y - gap - line : y + height + gap;
        _helpArea = RectangleF(_tools.front().Area.X, helpY, _tools.back().Area.Right() - _tools.front().Area.X, line);
    }
}
