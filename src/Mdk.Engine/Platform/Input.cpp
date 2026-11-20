#include "Mdk.Engine/Platform/Input.h"

#include <algorithm>
#include <format>
#include <vector>

#include <SDL3/SDL_keyboard.h>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Engine/Platform/GamepadMap.h"

namespace Mdk::Engine::Platform
{
    namespace
    {
        const Dictionary<SDL_Scancode, Key> DefaultKeys{
            {SDL_SCANCODE_W, Key::Forward},
            {SDL_SCANCODE_UP, Key::Forward},
            {SDL_SCANCODE_S, Key::Back},
            {SDL_SCANCODE_DOWN, Key::Back},
            {SDL_SCANCODE_A, Key::StrafeLeft},
            {SDL_SCANCODE_D, Key::StrafeRight},
            {SDL_SCANCODE_LEFT, Key::TurnLeft},
            {SDL_SCANCODE_RIGHT, Key::TurnRight},
            {SDL_SCANCODE_SPACE, Key::Jump},
            {SDL_SCANCODE_LSHIFT, Key::Turbo},
            {SDL_SCANCODE_RSHIFT, Key::Turbo},
            {SDL_SCANCODE_CAPSLOCK, Key::AlwaysRun},
            {SDL_SCANCODE_LCTRL, Key::Fire},
            {SDL_SCANCODE_RCTRL, Key::Fire},
            {SDL_SCANCODE_RETURN, Key::UseItem},
            {SDL_SCANCODE_E, Key::UseItem},
            {SDL_SCANCODE_TAB, Key::ItemNext},
            {SDL_SCANCODE_RIGHTBRACKET, Key::ItemNext},
            {SDL_SCANCODE_LEFTBRACKET, Key::ItemPrevious},
            {SDL_SCANCODE_1, Key::Item1},
            {SDL_SCANCODE_2, Key::Item2},
            {SDL_SCANCODE_3, Key::Item3},
            {SDL_SCANCODE_4, Key::Item4},
            {SDL_SCANCODE_5, Key::Item5},
            {SDL_SCANCODE_ESCAPE, Key::Escape},
            {SDL_SCANCODE_AC_BACK, Key::Escape},
            {SDL_SCANCODE_F12, Key::Screenshot},
            {SDL_SCANCODE_F1, Key::Fly},
            {SDL_SCANCODE_Z, Key::RollLeft},
            {SDL_SCANCODE_C, Key::RollRight},
            {SDL_SCANCODE_T, Key::Teleport},
            {SDL_SCANCODE_LALT, Key::Teleport},
            {SDL_SCANCODE_RALT, Key::Teleport},
            {SDL_SCANCODE_F2, Key::QuickSave},
            {SDL_SCANCODE_F9, Key::QuickLoad},
        };

        // The scope's own bindings (its zoom): they apply while Scoped, and anywhere else a key has
        // no binding of its own. A key can be bound to a scope action and to another one: the
        // scope's wins while sniping, the other one wins otherwise.
        const Dictionary<SDL_Scancode, Key> DefaultScopeKeys{
            {SDL_SCANCODE_PAGEUP, Key::ZoomIn},
            {SDL_SCANCODE_EQUALS, Key::ZoomIn},
            {SDL_SCANCODE_PAGEDOWN, Key::ZoomOut},
            {SDL_SCANCODE_MINUS, Key::ZoomOut},
        };

        // The flying camera's keys: fixed, whatever the bindings (E also uses items).
        const Dictionary<SDL_Scancode, Key> FlyKeys{
            {SDL_SCANCODE_E, Key::Up},
            {SDL_SCANCODE_Q, Key::Down},
        };

        // The digit keys, of the number row and the keypad, 0 to 9.
        constexpr std::array<std::array<SDL_Scancode, 10>, 2> DigitKeys{{
            {SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
             SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9},
            {SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4,
             SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9},
        }};

        // The keys of mouse buttons: the left one fires, the right one toggles sniper mode, the
        // wheel picks items.
        const Dictionary<MouseButton, Key> DefaultButtons{
            {MouseButton::Left, Key::Fire},
            {MouseButton::Right, Key::Sniper},
            {MouseButton::WheelUp, Key::ItemPrevious},
            {MouseButton::WheelDown, Key::ItemNext},
        };

        const Dictionary<SDL_Scancode, MenuKey> MenuKeys{
            {SDL_SCANCODE_UP, MenuKey::Up},
            {SDL_SCANCODE_DOWN, MenuKey::Down},
            {SDL_SCANCODE_LEFT, MenuKey::Left},
            {SDL_SCANCODE_RIGHT, MenuKey::Right},
            {SDL_SCANCODE_RETURN, MenuKey::Accept},
            {SDL_SCANCODE_KP_ENTER, MenuKey::Accept},
            {SDL_SCANCODE_SPACE, MenuKey::Accept},
            {SDL_SCANCODE_ESCAPE, MenuKey::Back},
            // Android's back button.
            {SDL_SCANCODE_AC_BACK, MenuKey::Back},
            {SDL_SCANCODE_BACKSPACE, MenuKey::Backspace},
            {SDL_SCANCODE_DELETE, MenuKey::Delete},
            {SDL_SCANCODE_HOME, MenuKey::Home},
            {SDL_SCANCODE_END, MenuKey::End},
        };

        const Dictionary<SDL_Scancode, RawKey> RawKeys{
            {SDL_SCANCODE_GRAVE, RawKey::Grave},
            {SDL_SCANCODE_F3, RawKey::F3},
            {SDL_SCANCODE_TAB, RawKey::Tab},
            {SDL_SCANCODE_RETURN, RawKey::Enter},
            {SDL_SCANCODE_KP_ENTER, RawKey::Enter},
            {SDL_SCANCODE_BACKSPACE, RawKey::Backspace},
            {SDL_SCANCODE_UP, RawKey::Up},
            {SDL_SCANCODE_DOWN, RawKey::Down},
            {SDL_SCANCODE_PAGEUP, RawKey::PageUp},
            {SDL_SCANCODE_PAGEDOWN, RawKey::PageDown},
            {SDL_SCANCODE_ESCAPE, RawKey::Escape},
        };

        // Names of mouse buttons as bindings (Bind); others are ButtonPrefix and their number
        // ("Mouse 4").
        const Dictionary<MouseButton, std::string> ButtonNames{
            {MouseButton::Left, "Left mouse"},
            {MouseButton::Right, "Right mouse"},
            {MouseButton::Middle, "Middle mouse"},
            {MouseButton::WheelUp, "Wheel up"},
            {MouseButton::WheelDown, "Wheel down"},
        };

        // byte.TryParse with NumberStyles.None: digits only, 0-255.
        bool TryParseByte(std::string_view text, int& value)
        {
            if (text.empty())
            {
                return false;
            }

            int result = 0;
            for (char c : text)
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }

                result = result * 10 + (c - '0');
                if (result > UINT8_MAX)
                {
                    return false;
                }
            }

            value = result;
            return true;
        }

        // The keys bound to `key` (copied first: the caller removes them).
        template <typename K>
        std::vector<K> BoundTo(const Dictionary<K, Key>& bindings, Key key)
        {
            std::vector<K> bound;
            for (const auto& [control, k] : bindings)
            {
                if (k == key)
                {
                    bound.push_back(control);
                }
            }

            return bound;
        }
    }

    Input::Input()
        : _keys(DefaultKeys), _buttonKeys(DefaultButtons), _scopeKeys(DefaultScopeKeys)
    {
        for (const auto& [scancode, key] : DefaultScopeKeys)
        {
            _scopeDefaultKeys.insert(scancode);
        }

        ResetPadBindings();
    }

    bool Input::IsDown(Key key) const
    {
        return _down.contains(key) || _buttons.contains(key) || _fly.contains(key) || _wheel.contains(key) || _held.contains(key)
               || _touched.contains(key) || _pad.contains(key)
               || (key == Key::Turbo && (_alwaysRun || CapsRuns()));
    }

    bool Input::CapsRuns() const
    {
        // GetValueOrDefault gives Key 0 (Forward) when unbound.
        return _capsLock == Lock::On && _keys.GetValueOrDefault(SDL_SCANCODE_CAPSLOCK) == Key::AlwaysRun;
    }

    void Input::Hold(Key key, State state)
    {
        if (state == State::Down)
        {
            _held.insert(key);
            return;
        }

        _held.erase(key);
    }

    void Input::Press(MenuKey key)
    {
        _menuPressed.insert(key);
        _anyPressed = true;
    }

    void Input::Press(Key key)
    {
        _pressed.insert(key);
        _anyPressed = true;
    }

    void Input::BeginFrame()
    {
        _pressed.clear();
        _menuPressed.clear();
        _rawPressed.clear();
        _clicked.clear();
        _pointerMoved = false;
        _typed.clear();
        _anyPressed = false;
        _lastControl.clear();
        _lastPadControl.clear();
        _digit = NoDigit;
    }

    bool Input::Bind(Key key, const std::string& control)
    {
        auto button = ParseButton(control);
        SDL_Scancode scancode = !button ? SDL_GetScancodeFromName(control.c_str()) : SDL_SCANCODE_UNKNOWN;
        if (!button && scancode == SDL_SCANCODE_UNKNOWN)
        {
            return false;
        }

        Unbind(key);
        bool scope = IsScopeAction(key);
        if (button)
        {
            MouseButton bound = *button;
            _wheelZoom.erase(bound);
            (scope ? _scopeButtons : _buttonKeys)[bound] = key;
            return true;
        }

        if (scope)
        {
            _scopeKeys[scancode] = key;
            _scopeDefaultKeys.erase(scancode);
            return true;
        }

        _keys[scancode] = key;
        if (_scopeDefaultKeys.erase(scancode) > 0)
        {
            _scopeKeys.Remove(scancode);
        }

        return true;
    }

    bool Input::TryResolve(SDL_Scancode scancode, Key& key) const
    {
        return Scoped
                   ? _scopeKeys.TryGetValue(scancode, key) || _keys.TryGetValue(scancode, key)
                   : _keys.TryGetValue(scancode, key) || _scopeKeys.TryGetValue(scancode, key);
    }

    bool Input::TryResolve(MouseButton button, Key& key) const
    {
        return Scoped
                   ? _scopeButtons.TryGetValue(button, key) || _buttonKeys.TryGetValue(button, key)
                   : _buttonKeys.TryGetValue(button, key) || _scopeButtons.TryGetValue(button, key);
    }

    void Input::ResetBindings()
    {
        _keys.Clear();
        _buttonKeys.Clear();
        _scopeKeys.Clear();
        _scopeButtons.Clear();
        _scopeDefaultKeys.clear();
        _wheelZoom.clear();
        _wheelZoom.insert(MouseButton::WheelUp);
        _wheelZoom.insert(MouseButton::WheelDown);
        for (const auto& [scancode, key] : DefaultScopeKeys)
        {
            _scopeKeys[scancode] = key;
            _scopeDefaultKeys.insert(scancode);
        }

        for (const auto& [scancode, key] : DefaultKeys)
        {
            _keys[scancode] = key;
        }

        for (const auto& [button, key] : DefaultButtons)
        {
            _buttonKeys[button] = key;
        }
    }

    bool Input::BindPad(Key key, const std::string& control)
    {
        auto button = GamepadMap::Parse(control);
        if (!button || *button == PadButton::Start)
        {
            return false;
        }

        for (auto bound : BoundTo(_padKeys, key))
        {
            _padKeys.Remove(bound);
        }

        _padKeys[*button] = key;
        return true;
    }

    void Input::ResetPadBindings()
    {
        _padKeys.Clear();
        for (int i = 0; i < PadButtonCount; i++)
        {
            auto button = static_cast<PadButton>(i);
            if (auto key = GamepadMap::Game(button))
            {
                _padKeys[button] = *key;
            }
        }
    }

    std::string Input::DescribePad(Key key) const
    {
        for (const auto& [button, bound] : _padKeys)
        {
            if (bound == key)
            {
                return GamepadMap::Name(button);
            }
        }

        return "-";
    }

    std::string Input::Describe(Key key) const
    {
        bool scope = IsScopeAction(key);
        for (const auto& [scancode, bound] : scope ? _scopeKeys : _keys)
        {
            if (bound == key)
            {
                const char* name = SDL_GetScancodeName(scancode);
                return name != nullptr ? name : "-";
            }
        }

        for (const auto& [button, bound] : scope ? _scopeButtons : _buttonKeys)
        {
            if (bound == key)
            {
                return ButtonName(button);
            }
        }

        return "-";
    }

    std::string Input::ButtonName(MouseButton button)
    {
        if (const std::string* name = ButtonNames.Find(button))
        {
            return *name;
        }

        return std::string(ButtonPrefix) + std::to_string(static_cast<int>(button));
    }

    std::optional<MouseButton> Input::ParseButton(const std::string& control)
    {
        for (const auto& [button, name] : ButtonNames)
        {
            if (name == control)
            {
                return button;
            }
        }

        std::string_view prefix = ButtonPrefix;
        int number = 0;
        if (!Str::StartsWith(control, prefix)
            || !TryParseByte(std::string_view(control).substr(prefix.size()), number)
            || number == 0)
        {
            return std::nullopt;
        }

        return static_cast<MouseButton>(number);
    }

    void Input::Unbind(Key key)
    {
        for (auto scancode : BoundTo(_keys, key))
        {
            _keys.Remove(scancode);
        }

        for (auto scancode : BoundTo(_scopeKeys, key))
        {
            _scopeKeys.Remove(scancode);
            _scopeDefaultKeys.erase(scancode);
        }

        for (auto button : BoundTo(_buttonKeys, key))
        {
            _buttonKeys.Remove(button);
        }

        for (auto button : BoundTo(_scopeButtons, key))
        {
            _scopeButtons.Remove(button);
        }
    }

    void Input::ClearMouse()
    {
        _mouseX = 0.0f;
        _mouseY = 0.0f;
        _wheelNotches = 0.0f;
        _zoomWheel = 0.0f;
        _wheel.clear();
    }

    float Input::TakeMouseX()
    {
        float x = _mouseX;
        _mouseX = 0.0f;
        return x;
    }

    float Input::TakeMouseY()
    {
        float y = _mouseY;
        _mouseY = 0.0f;
        return y;
    }

    void Input::Withhold()
    {
        BeginFrame();
        ClearMouse();
        _down.clear();
        _buttons.clear();
        _heldKeys.Clear();
        _heldButtons.Clear();
        _pointers.clear();
        _fly.clear();
        _menuDown.clear();
        _pad.clear();
        _padMenu.clear();
        _padHeld.clear();
    }

    void Input::SetKey(SDL_Scancode scancode, State state, Repeat repeat)
    {
        MenuKey menuKey;
        if (MenuKeys.TryGetValue(scancode, menuKey))
        {
            if (state == State::Down)
            {
                _menuPressed.insert(menuKey);
                _menuDown.insert(menuKey);
            }
            else
            {
                _menuDown.erase(menuKey);
            }
        }

        RawKey rawKey;
        if (state == State::Down && RawKeys.TryGetValue(scancode, rawKey))
        {
            _rawPressed.insert(rawKey);
        }

        if (state == State::Down && scancode == SDL_SCANCODE_BACKSPACE)
        {
            _typed += Erase;
        }

        if (state == State::Down && repeat == Repeat::No)
        {
            _anyPressed = true;
            const char* name = SDL_GetScancodeName(scancode);
            _lastControl = name != nullptr ? name : "";
            for (const auto& row : DigitKeys)
            {
                auto found = std::find(row.begin(), row.end(), scancode);
                if (found != row.end())
                {
                    _digit = static_cast<int>(found - row.begin());
                    break;
                }
            }
        }

        SetFlyKey(scancode, state);
        if (state == State::Up)
        {
            // The action it pressed, even if the bindings in force have changed since (sniping).
            Key released;
            if (_heldKeys.Remove(scancode, released))
            {
                _down.erase(released);
            }

            return;
        }

        Key key;
        if (!_heldKeys.TryGetValue(scancode, key))
        {
            if (!TryResolve(scancode, key))
            {
                return;
            }

            _heldKeys[scancode] = key;
        }

        // Caps Lock's "always run" follows the lock (LockCaps): its presses don't toggle.
        if (key == Key::AlwaysRun && scancode == SDL_SCANCODE_CAPSLOCK)
        {
            return;
        }

        if (_down.insert(key).second)
        {
            _pressed.insert(key);
        }
    }

    void Input::SetButton(MouseButton button, State state)
    {
        HoldPointer(button, state);
        if (state == State::Down)
        {
            _anyPressed = true;
            _lastControl = ButtonName(button);
            AddClick(button);
        }

        if (state == State::Up)
        {
            Key released;
            if (_heldButtons.Remove(button, released))
            {
                _buttons.erase(released);
            }

            return;
        }

        Key key;
        if (!_heldButtons.TryGetValue(button, key))
        {
            if (!TryResolve(button, key))
            {
                return;
            }

            _heldButtons[button] = key;
        }

        if (_buttons.insert(key).second)
        {
            _pressed.insert(key);
        }
    }

    void Input::AddWheel(float notches)
    {
        _wheelNotches += notches;
        if (notches == 0.0f)
        {
            return;
        }

        MouseButton button = notches > 0.0f ? MouseButton::WheelUp : MouseButton::WheelDown;
        _lastControl = ButtonName(button);
        if (Scoped)
        {
            std::optional<Key> zoom;
            Key scoped;
            if (_scopeButtons.TryGetValue(button, scoped))
            {
                zoom = scoped;
            }
            else if (_wheelZoom.contains(button))
            {
                zoom = button == MouseButton::WheelUp ? Key::ZoomIn : Key::ZoomOut;
            }

            if (zoom)
            {
                // A notch holds the zoom key for a while (Scope.Wheel): positive zooms in.
                _zoomWheel += *zoom == Key::ZoomIn ? MathF::Abs(notches) : -MathF::Abs(notches);
                return;
            }
        }

        Key key;
        if (TryResolve(button, key) && _wheel.insert(key).second)
        {
            _pressed.insert(key);
        }
    }

    void Input::SetFlyKey(SDL_Scancode scancode, State state)
    {
        Key key;
        if (!FlyKeys.TryGetValue(scancode, key))
        {
            return;
        }

        if (state == State::Up)
        {
            _fly.erase(key);
            return;
        }

        _fly.insert(key);
    }

    void Input::AddMouseMotion(float x, float y)
    {
        _mouseX += x * MouseScale;
        _mouseY += (InvertMouse ? -y : y) * MouseScale;
    }

    void Input::SetPointer(float x, float y)
    {
        _pointerX = x;
        _pointerY = y;
        _pointerMoved = true;
    }

    void Input::AddLook(Vector2 degrees)
    {
        _mouseX += degrees.X / LookDegrees;
        _mouseY += degrees.Y / LookDegrees;
    }

    void Input::AddPadLook(Vector2 degrees)
    {
        AddLook(Vector2(degrees.X, InvertPad ? -degrees.Y : degrees.Y) * PadSensitivity);
    }

    void Input::SetPadButton(PadButton button, State state)
    {
        if (state == State::Up)
        {
            _padHeld.erase(button);
        }
        else if (_padHeld.insert(button).second)
        {
            _lastPadControl = GamepadMap::Name(button);
        }

        Key key;
        if (_padKeys.TryGetValue(button, key))
        {
            SetPad(key, state);
        }

        if (auto menuKey = GamepadMap::Menu(button))
        {
            SetPadMenu(*menuKey, state);
        }
    }

    void Input::SetPad(Key key, State state)
    {
        if (state == State::Up)
        {
            _pad.erase(key);
            return;
        }

        if (_pad.insert(key).second)
        {
            _pressed.insert(key);
            _anyPressed = true;
        }
    }

    void Input::SetPadMenu(MenuKey key, State state)
    {
        if (state == State::Up)
        {
            _padMenu.erase(key);
            return;
        }

        if (_padMenu.insert(key).second)
        {
            _menuPressed.insert(key);
            _anyPressed = true;
        }
    }

    void Input::Touch(Key key, State state)
    {
        if (state == State::Up)
        {
            _touched.erase(key);
            return;
        }

        if (_touched.insert(key).second)
        {
            Press(key);
        }
    }

    void Input::HoldPointer(MouseButton button, State state)
    {
        std::optional<Pointer> pointer;
        if (button == MouseButton::Left)
        {
            pointer = Pointer::Left;
        }
        else if (button == MouseButton::Right)
        {
            pointer = Pointer::Right;
        }

        if (!pointer)
        {
            return;
        }

        if (state == State::Down)
        {
            _pointers.insert(*pointer);
            return;
        }

        _pointers.erase(*pointer);
    }

    void Input::AddClick(MouseButton button)
    {
        if (button == MouseButton::Left)
        {
            _clicked.insert(Pointer::Left);
        }
        else if (button == MouseButton::Right)
        {
            _clicked.insert(Pointer::Right);
        }
    }
}
