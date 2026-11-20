#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>

#include <SDL3/SDL.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/GamepadMap.h"
#include "Mdk.Engine/Platform/Input.h"

namespace Mdk::Engine::Platform
{
    // The gamepads plugged in (SDL's gamepad API: Xbox, PlayStation, Switch and Android's Bluetooth
    // pads by the same buttons), driving Input through GamepadMap.
    class Gamepads
    {
    public:
        Gamepads() = default;
        ~Gamepads();
        Gamepads(const Gamepads&) = delete;
        Gamepads& operator=(const Gamepads&) = delete;

        // Handles a gamepad event; false for the others.
        bool Event(const SDL_Event& e, Input& input);

        // Once a frame, after the events: the left stick walks or steers the menus, the right one
        // looks (by the time since the last frame).
        void Update(Input& input);

    private:
        // An axis' reach (SDL's sticks and triggers run to ±32767).
        static constexpr float AxisReach = INT16_MAX;
        // A trigger pulled past this (of its reach) is pressed.
        static constexpr float TriggerPull = 0.5f;
        static constexpr float NanosecondsPerSecond = 1e9f;
        // A longer gap between frames (a pause, a load) looks no farther.
        static constexpr float MaxStep = 0.1f;

        static constexpr std::array<Key, 4> WalkKeys{Key::Forward, Key::Back, Key::StrafeLeft, Key::StrafeRight};
        static constexpr std::array<MenuKey, 4> SteerKeys{MenuKey::Up, MenuKey::Down, MenuKey::Left, MenuKey::Right};

        void Open(SDL_JoystickID id);

        // A gamepad unplugged: it lets go of everything it held.
        void Close(SDL_JoystickID id, Input& input);

        void Axis(SDL_GamepadAxis axis, float value, Input& input);

        static std::optional<PadButton> Button(SDL_GamepadButton button);

        std::unordered_map<SDL_JoystickID, SDL_Gamepad*> _open;
        Vector2 _left;
        Vector2 _right;
        std::uint64_t _lastTicks = 0;
    };
}
