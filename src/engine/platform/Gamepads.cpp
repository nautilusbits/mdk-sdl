#include "engine/platform/Gamepads.h"

#include "core/DotNet.h"

namespace Mdk::Engine::Platform
{
    bool Gamepads::Event(const SDL_Event& e, Input& input)
    {
        switch (static_cast<SDL_EventType>(e.type))
        {
            case SDL_EVENT_GAMEPAD_ADDED:
                Open(e.gdevice.which);
                return true;
            case SDL_EVENT_GAMEPAD_REMOVED:
                Close(e.gdevice.which, input);
                return true;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if (auto button = Button(static_cast<SDL_GamepadButton>(e.gbutton.button)))
                {
                    input.SetPadButton(*button, e.gbutton.down ? Input::State::Down : Input::State::Up);
                }

                return true;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                Axis(static_cast<SDL_GamepadAxis>(e.gaxis.axis), e.gaxis.value / AxisReach, input);
                return true;
            default:
                return false;
        }
    }

    void Gamepads::Update(Input& input)
    {
        std::uint64_t now = SDL_GetTicksNS();
        float seconds = _lastTicks == 0 ? 0.0f : MathF::Min(static_cast<float>(now - _lastTicks) / NanosecondsPerSecond, MaxStep);
        _lastTicks = now;
        if (_open.empty())
        {
            return;
        }

        for (auto key : WalkKeys)
        {
            input.SetPad(key, GamepadMap::Walks(_left, key) ? Input::State::Down : Input::State::Up);
        }

        for (auto key : SteerKeys)
        {
            input.SetPadMenu(key, GamepadMap::Steers(_left, key) ? Input::State::Down : Input::State::Up);
        }

        input.AddPadLook(GamepadMap::Look(_right, seconds));
    }

    void Gamepads::Open(SDL_JoystickID id)
    {
        if (_open.contains(id))
        {
            return;
        }

        SDL_Gamepad* pad = SDL_OpenGamepad(id);
        if (pad != nullptr)
        {
            _open[id] = pad;
        }
    }

    void Gamepads::Close(SDL_JoystickID id, Input& input)
    {
        auto found = _open.find(id);
        if (found == _open.end())
        {
            return;
        }

        SDL_Gamepad* pad = found->second;
        _open.erase(found);
        SDL_CloseGamepad(pad);
        _left = _right = Vector2::Zero();
        for (int i = 0; i < PadButtonCount; i++)
        {
            input.SetPadButton(static_cast<PadButton>(i), Input::State::Up);
        }
    }

    void Gamepads::Axis(SDL_GamepadAxis axis, float value, Input& input)
    {
        switch (axis)
        {
            case SDL_GAMEPAD_AXIS_LEFTX:
                _left.X = value;
                break;
            case SDL_GAMEPAD_AXIS_LEFTY:
                _left.Y = value;
                break;
            case SDL_GAMEPAD_AXIS_RIGHTX:
                _right.X = value;
                break;
            case SDL_GAMEPAD_AXIS_RIGHTY:
                _right.Y = value;
                break;
            case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
                input.SetPadButton(PadButton::LeftTrigger, value > TriggerPull ? Input::State::Down : Input::State::Up);
                break;
            case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
                input.SetPadButton(PadButton::RightTrigger, value > TriggerPull ? Input::State::Down : Input::State::Up);
                break;
            default:
                break;
        }
    }

    std::optional<PadButton> Gamepads::Button(SDL_GamepadButton button)
    {
        switch (button)
        {
            case SDL_GAMEPAD_BUTTON_SOUTH: return PadButton::South;
            case SDL_GAMEPAD_BUTTON_EAST: return PadButton::East;
            case SDL_GAMEPAD_BUTTON_WEST: return PadButton::West;
            case SDL_GAMEPAD_BUTTON_NORTH: return PadButton::North;
            case SDL_GAMEPAD_BUTTON_BACK: return PadButton::Back;
            case SDL_GAMEPAD_BUTTON_START: return PadButton::Start;
            case SDL_GAMEPAD_BUTTON_LEFT_STICK: return PadButton::LeftStick;
            case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return PadButton::RightStick;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return PadButton::LeftShoulder;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return PadButton::RightShoulder;
            case SDL_GAMEPAD_BUTTON_DPAD_UP: return PadButton::DpadUp;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return PadButton::DpadDown;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return PadButton::DpadLeft;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return PadButton::DpadRight;
            default: return std::nullopt;
        }
    }

    Gamepads::~Gamepads()
    {
        for (const auto& [id, pad] : _open)
        {
            SDL_CloseGamepad(pad);
        }

        _open.clear();
    }
}
