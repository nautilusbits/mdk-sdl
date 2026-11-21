#include "game/menu/TouchLayoutPage.h"

#include <format>

#include "core/Console.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"
#include "game/flow/Settings.h"
#include "game/menu/Ui.h"

namespace Mdk::Game::Menu
{
    using Engine::Platform::Input;
    using Engine::Platform::MenuKey;
    using Engine::Platform::Pointer;
    using Engine::Platform::TouchLayout;
    using Engine::Platform::TouchPart;
    using Hud::TouchView;

    namespace
    {
        // C#'s TouchPart.ToString() (the saved log line names the parts).
        std::string NameOf(TouchPart part)
        {
            static constexpr const char* Names[] = {"Fire", "Jump", "Use", "Sniper", "Item", "ZoomIn", "ZoomOut", "Pause", "Stick"};
            return Names[static_cast<int>(part)];
        }
    }

    TouchLayoutPage::TouchLayoutPage(Ui& ui, Hud::TouchView& view, std::function<void()> back)
        : _ui(ui), _view(view), _back(std::move(back))
    {
        _editor = std::make_shared<TouchEditor>(ui.Settings().TouchPlaces);
        Measure();
        std::string labels;
        for (auto part : TouchLayout::Parts())
        {
            labels += labels.empty() ? TouchLayout::LabelOf(part) : " | " + TouchLayout::LabelOf(part);
        }

        Console::WriteLine(std::format("Touch layout: {}", labels));
    }

    void TouchLayoutPage::Update()
    {
        Measure();
        auto& input = _ui.Input();
        auto outcome = Keys(input);
        auto at = Vector2(input.PointerX(), input.PointerY());
        if (input.WasClicked(Pointer::Left))
        {
            outcome = _editor->Press(at);
        }
        else if (input.IsDown(Pointer::Left))
        {
            _editor->Drag(at);
        }
        else
        {
            _editor->Release();
        }

        if (input.WasPressed(MenuKey::Back))
        {
            outcome = _editor->Cancel();
        }

        if (_editor->Confirming() && !_asked)
        {
            Console::WriteLine(std::format("Touch layout: {}", Question));
        }

        _asked = _editor->Confirming();
        if (outcome == TouchEditor::Outcome::Done)
        {
            Save();
        }

        if (outcome != TouchEditor::Outcome::Editing)
        {
            _back();
        }
    }

    TouchEditor::Outcome TouchLayoutPage::Keys(Input& input)
    {
        if (input.WasPressed(MenuKey::Up))
        {
            _editor->Select(-1);
        }

        if (input.WasPressed(MenuKey::Down))
        {
            _editor->Select(1);
        }

        if (input.WasPressed(MenuKey::Left))
        {
            _editor->Grow(-1);
        }

        if (input.WasPressed(MenuKey::Right))
        {
            _editor->Grow(1);
        }

        if (input.WasPressed(MenuKey::Delete))
        {
            _editor->Reset();
        }

        return input.WasPressed(MenuKey::Accept) ? _editor->Accept() : TouchEditor::Outcome::Editing;
    }

    void TouchLayoutPage::Save()
    {
        auto& places = _ui.Settings().TouchPlaces;
        places.clear();
        for (const auto& [part, place] : _editor->Places())
        {
            places[part] = place;
        }

        _ui.ApplySettings();
        std::string keys;
        for (const auto& [part, place] : places)
        {
            auto name = NameOf(part);
            keys += keys.empty() ? name : " | " + name;
        }

        Console::WriteLine(std::format("Touch layout saved: {}", keys));
    }

    void TouchLayoutPage::Measure()
    {
        auto& window = _ui.Window();
        auto [width, height] = window.Size();
        _editor->Measure(Vector2(static_cast<float>(width), static_cast<float>(height)), window.SafeArea(), window.UnitsPerMm());
    }

    void TouchLayoutPage::Draw()
    {
        // At full opacity: the selected and the overlapping ones must show whatever play's opacity.
        _ui.View->FillCanvas(Dim);
        // The zone is drawn as large as a default button's corners and label.
        auto& window = _ui.Window();
        auto [width, height] = window.Size();
        auto side = TouchLayout::DefaultArea(TouchPart::Fire, Vector2(static_cast<float>(width), static_cast<float>(height)), window.SafeArea(), window.UnitsPerMm()).Width;
        const auto& parts = TouchLayout::Parts();
        for (auto it = parts.rbegin(); it != parts.rend(); ++it)
        {
            auto part = *it;
            _view.Button(_editor->Areas().at(part), TouchLayout::LabelOf(part), ShadeOf(part), TouchLayout::IsButton(part) ? std::nullopt : std::optional<float>(side));
        }

        if (auto handle = _editor->Handle())
        {
            _view.Disc(*handle);
        }

        // The tools brighter than the controls (theirs are + and -); asked, over a darker screen.
        if (_editor->Confirming())
        {
            _ui.View->FillCanvas(Dim);
        }
        else
        {
            DrawLook(side);
        }

        for (const auto& [tool, area] : _editor->Tools())
        {
            _view.Button(area, TouchEditor::LabelOf(tool), TouchView::Shade::Held);
        }

        auto overlap = false;
        for (auto part : parts)
        {
            if (_editor->Overlapping(part))
            {
                overlap = true;
                break;
            }
        }

        _view.Caption(_editor->HelpArea(), _editor->Confirming() ? Question : overlap ? OverlapWarning : Help);
    }

    void TouchLayoutPage::DrawLook(float side)
    {
        auto& window = _ui.Window();
        auto safe = window.SafeArea();
        auto zone = _editor->Areas().at(TouchPart::Stick);
        auto width = (safe.Right() - zone.Right()) * LookPart;
        if (width <= 0.f)
        {
            return;
        }

        auto height = side * LookPart;
        auto x = zone.Right() + (safe.Right() - zone.Right() - width) / 2.f;
        auto y = safe.Y + (safe.Height - height) / 2.f;
        _view.Caption(RectangleF(x, y, width, height), LookLabel);
    }

    TouchView::Shade TouchLayoutPage::ShadeOf(TouchPart part) const
    {
        if (_editor->Overlapping(part))
        {
            return TouchView::Shade::Overlap;
        }

        return part == _editor->Selected() ? TouchView::Shade::Selected : TouchView::Shade::Idle;
    }
}
