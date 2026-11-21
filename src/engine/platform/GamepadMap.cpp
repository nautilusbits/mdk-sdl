#include "engine/platform/GamepadMap.h"

#include <array>
#include <utility>

#include "core/DotNet.h"

namespace Mdk::Engine::Platform
{
    namespace
    {
        // The buttons' names (bindings, the gamepad's options), by Xbox's labels.
        constexpr std::array<std::pair<PadButton, const char*>, PadButtonCount> Names{{
            {PadButton::South, "A"}, {PadButton::East, "B"}, {PadButton::West, "X"}, {PadButton::North, "Y"},
            {PadButton::Back, "Back"}, {PadButton::Start, "Start"}, {PadButton::LeftStick, "L3"}, {PadButton::RightStick, "R3"},
            {PadButton::LeftShoulder, "LB"}, {PadButton::RightShoulder, "RB"},
            {PadButton::DpadUp, "D-pad up"}, {PadButton::DpadDown, "D-pad down"},
            {PadButton::DpadLeft, "D-pad left"}, {PadButton::DpadRight, "D-pad right"},
            {PadButton::LeftTrigger, "LT"}, {PadButton::RightTrigger, "RT"},
        }};
    }

    std::string GamepadMap::Name(PadButton button)
    {
        for (const auto& [b, text] : Names)
        {
            if (b == button)
            {
                return text;
            }
        }

        throw std::out_of_range("The given key was not present in the dictionary.");
    }

    std::optional<PadButton> GamepadMap::Parse(const std::string& name)
    {
        for (const auto& [button, text] : Names)
        {
            if (text == name)
            {
                return button;
            }
        }

        return std::nullopt;
    }

    std::optional<Key> GamepadMap::Game(PadButton button)
    {
        switch (button)
        {
            case PadButton::RightTrigger: return Key::Fire;
            case PadButton::LeftTrigger: return Key::Sniper;
            case PadButton::South: return Key::Jump;
            case PadButton::East: return Key::Turbo;
            case PadButton::LeftStick: return Key::AlwaysRun;
            case PadButton::West: return Key::UseItem;
            case PadButton::North:
            case PadButton::RightShoulder:
            case PadButton::DpadRight: return Key::ItemNext;
            case PadButton::LeftShoulder:
            case PadButton::DpadLeft: return Key::ItemPrevious;
            case PadButton::DpadUp: return Key::ZoomIn;
            case PadButton::DpadDown: return Key::ZoomOut;
            case PadButton::Start: return Key::Escape;
            default: return std::nullopt;
        }
    }

    std::optional<MenuKey> GamepadMap::Menu(PadButton button)
    {
        switch (button)
        {
            case PadButton::South: return MenuKey::Accept;
            case PadButton::East:
            case PadButton::Start: return MenuKey::Back;
            case PadButton::DpadUp: return MenuKey::Up;
            case PadButton::DpadDown: return MenuKey::Down;
            case PadButton::DpadLeft: return MenuKey::Left;
            case PadButton::DpadRight: return MenuKey::Right;
            default: return std::nullopt;
        }
    }

    bool GamepadMap::Walks(Vector2 stick, Key key)
    {
        switch (key)
        {
            case Key::Forward: return stick.Y < -Push;
            case Key::Back: return stick.Y > Push;
            case Key::StrafeLeft: return stick.X < -Push;
            case Key::StrafeRight: return stick.X > Push;
            default: return false;
        }
    }

    bool GamepadMap::Steers(Vector2 stick, MenuKey key)
    {
        switch (key)
        {
            case MenuKey::Up: return stick.Y < -Push;
            case MenuKey::Down: return stick.Y > Push;
            case MenuKey::Left: return stick.X < -Push;
            case MenuKey::Right: return stick.X > Push;
            default: return false;
        }
    }

    Vector2 GamepadMap::Look(Vector2 stick, float seconds)
    {
        float tilt = MathF::Min(stick.Length(), 1.0f);
        if (tilt < DeadZone)
        {
            return Vector2::Zero();
        }

        // Past the dead zone, from 0 to 1 along a curve.
        float strength = MathF::Pow((tilt - DeadZone) / (1.0f - DeadZone), Curve);
        return Vector2::Normalize(stick) * strength * LookRate * seconds;
    }
}
