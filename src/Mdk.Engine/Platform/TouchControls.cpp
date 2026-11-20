#include "Mdk.Engine/Platform/TouchControls.h"

#include <vector>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Engine::Platform
{
    std::optional<Vector2> TouchControls::StickAt() const
    {
        if (!_stickFinger)
        {
            return std::nullopt;
        }

        return _fingers.At(*_stickFinger).At;
    }

    void TouchControls::Arrange(const TouchPlaces& places, float opacity)
    {
        _opacity = opacity;
        if (places == _places)
        {
            return;
        }

        _places = places;
        _placesChanged = true;
    }

    void TouchControls::Measure(float unitsPerMm, RectangleF safe)
    {
        _unitsPerMm = unitsPerMm;
        _safe = safe;
    }

    float TouchControls::Physical(float part, float mm, float height, float unitsPerMm)
    {
        return unitsPerMm > 0.0f ? MathF::Min(part * height, mm * unitsPerMm) : part * height;
    }

    bool TouchControls::Held(Key key) const
    {
        for (const auto& [id, finger] : _fingers)
        {
            if (finger.Role == Role::Button && finger.Key == key)
            {
                return true;
            }
        }

        return false;
    }

    const std::vector<TouchButton>& TouchControls::Layout(Vector2 screen)
    {
        if (screen == _layoutScreen && _unitsPerMm == _layoutDensity && _safe == _layoutSafe && !_placesChanged)
        {
            return _layout;
        }

        std::vector<TouchButton> buttons;
        for (auto part : TouchLayout::Parts())
        {
            if (!TouchLayout::IsButton(part))
            {
                continue;
            }

            buttons.push_back({TouchLayout::KeyOf(part), TouchLayout::LabelOf(part), TouchLayout::AreaOf(part, _places, screen, _safe, _unitsPerMm)});
        }

        _zone = TouchLayout::AreaOf(TouchPart::Stick, _places, screen, _safe, _unitsPerMm);
        _layout = std::move(buttons);
        _layoutScreen = screen;
        _layoutDensity = _unitsPerMm;
        _layoutSafe = _safe;
        _placesChanged = false;
        return _layout;
    }

    void TouchControls::Down(std::uint64_t id, Vector2 at, Vector2 screen, Input& input)
    {
        for (const auto& button : Layout(screen))
        {
            if (!button.Area.Contains(at.X, at.Y))
            {
                continue;
            }

            _fingers[id] = {Role::Button, at, button.Key};
            Hold(button.Key, input);
            return;
        }

        if (_zone.Contains(at.X, at.Y) && !_stickFinger)
        {
            _fingers[id] = {Role::Stick, at};
            _stickFinger = id;
            _stick = at;
            return;
        }

        _fingers[id] = {Role::Look, at};
    }

    void TouchControls::Move(std::uint64_t id, Vector2 at, Vector2 screen, Input& input)
    {
        Finger* finger = _fingers.Find(id);
        if (finger == nullptr)
        {
            return;
        }

        Vector2 delta = at - finger->At;
        finger->At = at;
        if (finger->Role == Role::Look || (finger->Role == Role::Button && finger->Key == Key::Fire))
        {
            input.AddMouseMotion(delta.X * LookScale, delta.Y * LookScale);
            return;
        }

        if (finger->Role == Role::Stick)
        {
            Walk((at - _stick.value()) / StickRadius(screen), input);
        }
    }

    void TouchControls::Up(std::uint64_t id, Input& input)
    {
        Finger finger{};
        if (!_fingers.Remove(id, finger))
        {
            return;
        }

        if (finger.Role == Role::Button)
        {
            input.Touch(finger.Key, Input::State::Up);
            return;
        }

        if (finger.Role == Role::Stick)
        {
            Walk(Vector2::Zero(), input);
            _stickFinger.reset();
            _stick.reset();
        }
    }

    void TouchControls::Release(Input& input)
    {
        std::vector<std::uint64_t> ids;
        for (const auto& [id, finger] : _fingers)
        {
            ids.push_back(id);
        }

        for (auto id : ids)
        {
            Up(id, input);
        }
    }

    void TouchControls::Hold(Key key, Input& input)
    {
        input.Touch(key, Input::State::Down);
        if (key == Key::Escape)
        {
            input.Press(MenuKey::Back);
        }
    }

    void TouchControls::Walk(Vector2 push, Input& input)
    {
        std::array<bool, StickKeys.size()> down{
            push.Y < -DeadZone,
            push.Y > DeadZone,
            push.X < -DeadZone,
            push.X > DeadZone,
            push.Length() > RunZone,
        };

        for (size_t i = 0; i < StickKeys.size(); i++)
        {
            input.Touch(StickKeys[i], down[i] ? Input::State::Down : Input::State::Up);
        }
    }
}
