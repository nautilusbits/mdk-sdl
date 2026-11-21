#pragma once

#include <optional>
#include <string>

#include "core/Numerics.h"
#include "engine/platform/Input.h"

namespace Mdk::Engine::Platform
{
    // A gamepad's buttons by place (SDL's names: South is Xbox's A, PlayStation's cross); the
    // triggers count as buttons once pulled half way.
    enum class PadButton
    {
        South, East, West, North, Back, Start, LeftStick, RightStick, LeftShoulder, RightShoulder,
        DpadUp, DpadDown, DpadLeft, DpadRight, LeftTrigger, RightTrigger,
    };

    constexpr int PadButtonCount = 16;

    // The gamepad's default layout (the buttons rebind but Start; the sticks are fixed): the left
    // stick walks, the right one looks.
    //   LT sniper   LB item ◄            RB item ►   RT fire
    //   d-pad: ▲ zoom in, ▼ zoom out, ◄ ► items      Y item ►
    //   left stick: walk, L3 always run          X use   B turbo
    //   Start: pause                                 A jump
    // In the menus: the d-pad or the left stick, A accepts, B and Start go back.
    struct GamepadMap
    {
        // The right stick's look at full tilt (degrees a second: right, down).
        static constexpr Vector2 LookRate{180.0f, 120.0f};

        static std::string Name(PadButton button);

        // The button of a name, or none.
        static std::optional<PadButton> Parse(const std::string& name);

        // The key a button holds in play by default, or none.
        static std::optional<Key> Game(PadButton button);

        // The menu key a button presses, or none.
        static std::optional<MenuKey> Menu(PadButton button);

        // The left stick holds `key` (walking; its y is down, SDL's).
        static bool Walks(Vector2 stick, Key key);

        // The left stick holds the menus' `key`.
        static bool Steers(Vector2 stick, MenuKey key);

        // The right stick's look over `seconds` (degrees: right, down).
        static Vector2 Look(Vector2 stick, float seconds);

    private:
        // A stick past this (of its reach) holds a direction.
        static constexpr float Push = 0.5f;
        // The right stick's dead zone and its curve's power (finer aim near the centre).
        static constexpr float DeadZone = 0.15f;
        static constexpr float Curve = 2.0f;
    };
}
