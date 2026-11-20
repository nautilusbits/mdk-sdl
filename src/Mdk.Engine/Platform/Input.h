#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_set>

#include <SDL3/SDL_scancode.h>

#include "Mdk.Core/Dictionary.h"
#include "Mdk.Core/Numerics.h"

namespace Mdk::Engine::Platform
{
    enum class PadButton;

    // Keys the game uses, independent of SDL.
    enum class Key
    {
        Forward, Back, StrafeLeft, StrafeRight, TurnLeft, TurnRight, Jump, Turbo, Fire,
        // Kurt's items: use the selected one, select the next, previous, or slot 1-5.
        UseItem, ItemNext, ItemPrevious, Item1, Item2, Item3, Item4, Item5,
        // Flying camera: up and down.
        Up, Down,
        Escape, Screenshot,
        // Switches between Kurt and the flying camera.
        Fly,
        // Sniper mode on or off, and its zoom.
        Sniper, ZoomIn, ZoomOut,
        // The 1996 demo's levels: rolls, and teleports (held with a digit).
        RollLeft, RollRight, Teleport,
        // The level's full save (the original's F2), and loading the latest save.
        QuickSave, QuickLoad,
        // Turbo on until pressed again; on Caps Lock, while it is locked (the original's).
        AlwaysRun,
    };

    // Keys of the menus and prompts: fixed, whatever the game's bindings.
    enum class MenuKey { Up, Down, Left, Right, Accept, Back, Backspace, Delete, Home, End };

    // Keys by their place on the keyboard, whatever the layout and the bindings (the console, the
    // debug overlay); the keyboard's repeat presses them again. Grave is the key left of 1.
    enum class RawKey { Grave, F3, Tab, Enter, Backspace, Up, Down, PageUp, PageDown, Escape };

    // Mouse buttons that click menu items.
    enum class Pointer { Left, Right };

    // Mouse buttons by SDL's numbers (any number is a button); the wheel's two directions are
    // buttons too, out of SDL's byte range.
    enum class MouseButton
    {
        Left = 1, Middle = 2, Right = 3, Back = 4, Forward = 5,
        WheelUp = 255 + 1, WheelDown = 255 + 2,
    };

    // Whether a key event is the keyboard's auto-repeat.
    enum class Repeat { No, Yes };

    // Keyboard and mouse state of the current frame.
    class Input
    {
    public:
        enum class State { Up, Down };
        // A lock key's state (Caps Lock: its light).
        enum class Lock { Off, On };

        // No digit pressed (Digit).
        static constexpr int NoDigit = -1;

        static constexpr char Erase = '\b';

        Input();

        float MouseX() const { return _mouseX; }
        float MouseY() const { return _mouseY; }
        // Mouse wheel notches since the last game step: positive away from the user.
        float Wheel() const { return _wheelNotches; }
        // The wheel notches that zoom the scope since the last game step (positive: zoom in): the
        // wheel only zooms while Scoped, and unless it is bound to something else.
        float ZoomWheel() const { return _zoomWheel; }
        // Kurt is sniping: the scope's bindings win over the others (set by the game each step).
        bool Scoped = false;

        // Mouse motion is scaled by this (the mouse sensitivity).
        float MouseScale = 1.0f;
        // Vertical mouse motion is inverted.
        bool InvertMouse = false;
        // The gamepad's look is scaled by this; its vertical look is inverted.
        float PadSensitivity = 1.0f;
        bool InvertPad = false;
        // The pointer in the window (pixels).
        float PointerX() const { return _pointerX; }
        float PointerY() const { return _pointerY; }
        // The pointer moved this frame.
        bool PointerMoved() const { return _pointerMoved; }
        // Text typed this frame, with an Erase for each Backspace where it came: a phone keyboard's
        // correction of "fps" to "gps" is "\b\b\bgps" in one frame.
        const std::string& Typed() const { return _typed; }
        // Any key or mouse button was pressed this frame.
        bool AnyPressed() const { return _anyPressed; }
        // The name of the key or mouse button pressed this frame (for Bind), or "".
        const std::string& LastControl() const { return _lastControl; }
        // The name of the gamepad button pressed this frame (for BindPad), or "".
        const std::string& LastPadControl() const { return _lastPadControl; }
        // The digit (0-9) pressed this frame, or NoDigit.
        int Digit() const { return _digit; }

        bool IsDown(Key key) const;

        // Turbo stays on, or no longer ("always run" pressed in play).
        void ToggleRun() { _alwaysRun = !_alwaysRun; }

        // Caps Lock's state this frame (the window's keyboard).
        void LockCaps(Lock state) { _capsLock = state; }

        // Holds or releases a key from the program (automated tests).
        void Hold(Key key, State state);

        // Presses a menu key for this frame from the program (automated tests).
        void Press(MenuKey key);

        // Presses a game key for this frame from the program (automated tests).
        void Press(Key key);

        // Pressed this frame.
        bool WasPressed(Key key) const { return _pressed.contains(key); }

        bool WasPressed(MenuKey key) const { return _menuPressed.contains(key); }

        // Held (the menus' sliders step again while a direction is held).
        bool IsDown(MenuKey key) const { return _menuDown.contains(key) || _padMenu.contains(key); }

        bool WasPressed(RawKey key) const { return _rawPressed.contains(key); }

        bool WasClicked(Pointer button) const { return _clicked.contains(button); }

        bool IsDown(Pointer button) const { return _pointers.contains(button); }

        void BeginFrame();

        // Binds a key or mouse button (by its name, as LastControl gives it) to `key`, instead of
        // its other bindings. Returns false for an unknown name.
        bool Bind(Key key, const std::string& control);

        // Back to the default bindings.
        void ResetBindings();

        // Binds a gamepad button (by its name, GamepadMap::Name) to `key`, instead of its other
        // buttons. Returns false for an unknown name, and for Start (pause).
        bool BindPad(Key key, const std::string& control);

        // Back to the gamepad's default buttons.
        void ResetPadBindings();

        // The name of a key's first gamepad button, or "-".
        std::string DescribePad(Key key) const;

        // The name of a key's first binding, or "-".
        std::string Describe(Key key) const;

        // The mouse motion was used (by a game step).
        void ClearMouse();

        // The horizontal or vertical mouse motion, used at once (a frame's look): the next game
        // step sees none.
        float TakeMouseX();
        float TakeMouseY();

        // Someone else has this frame's input (the console): the game sees no presses, typing,
        // clicks or mouse, and no keyboard or mouse keys held. Keys held by the program (tests) stay.
        void Withhold();

        void SetKey(SDL_Scancode scancode, State state, Repeat repeat);

        // A mouse button: see DefaultButtons.
        void SetButton(MouseButton button, State state);

        // Wheel notches: while sniping they zoom (ZoomWheel) if the wheel still has its zoom, or is
        // bound to a zoom action; otherwise they press the wheel's bindings until the next game step.
        void AddWheel(float notches);

        void AddMouseMotion(float x, float y);

        void SetPointer(float x, float y);

        void AddText(const std::string& text) { _typed += text; }

        // A look in degrees (right, down; a gamepad's stick, the gyroscope): as the mouse's, but
        // neither scaled by its sensitivity nor inverted.
        void AddLook(Vector2 degrees);

        // The gamepad's look (degrees: right, down; GamepadMap::Look), by its sensitivity, inverted
        // or not.
        void AddPadLook(Vector2 degrees);

        // A gamepad button: its bound key in play, its fixed menu key (GamepadMap::Menu).
        void SetPadButton(PadButton button, State state);

        // A gamepad holds or releases a key; holding presses it this frame.
        void SetPad(Key key, State state);

        // A gamepad holds or releases a menu key; holding presses it this frame.
        void SetPadMenu(MenuKey key, State state);

        // An on-screen control holds or releases a key; holding presses it this frame.
        void Touch(Key key, State state);

    private:
        // The game's degrees a mouse count turns (Kurt, the cameras): the gamepad's and the
        // gyroscope's look in degrees becomes mouse counts.
        static constexpr float LookDegrees = 0.15f;

        static constexpr const char* ButtonPrefix = "Mouse ";

        // "Always run" is Caps Lock's and the lock is on.
        bool CapsRuns() const;

        // The actions only the scope uses: its bindings are a layer of their own, so a key can zoom
        // while sniping and do something else (walk) otherwise.
        static bool IsScopeAction(Key key) { return key == Key::ZoomIn || key == Key::ZoomOut; }

        // The action of a key: the scope's binding first while sniping, else the other one first.
        bool TryResolve(SDL_Scancode scancode, Key& key) const;

        // The action of a mouse button, as TryResolve(SDL_Scancode, Key&).
        bool TryResolve(MouseButton button, Key& key) const;

        // A mouse button's binding name: "Left mouse", "Mouse 8".
        static std::string ButtonName(MouseButton button);

        // The mouse button of a binding name, or none.
        static std::optional<MouseButton> ParseButton(const std::string& control);

        void Unbind(Key key);

        void SetFlyKey(SDL_Scancode scancode, State state);

        // The left or right button held or let go (IsDown(Pointer)).
        void HoldPointer(MouseButton button, State state);

        void AddClick(MouseButton button);

        // The bindings: the defaults, changed by Bind.
        Dictionary<SDL_Scancode, Key> _keys;
        Dictionary<MouseButton, Key> _buttonKeys;
        // The scope's bindings (DefaultScopeKeys): zoom.
        Dictionary<SDL_Scancode, Key> _scopeKeys;
        Dictionary<MouseButton, Key> _scopeButtons;
        // Scope keys that are still the built-in ones: binding the key to another action replaces
        // them (an explicit scope binding is kept).
        std::unordered_set<SDL_Scancode> _scopeDefaultKeys;
        // Wheel directions that still zoom while sniping, as they do by default: binding the wheel
        // to any action takes the zoom away.
        std::unordered_set<MouseButton> _wheelZoom{MouseButton::WheelUp, MouseButton::WheelDown};
        // The action each held key or mouse button pressed, so that it is the one released.
        Dictionary<SDL_Scancode, Key> _heldKeys;
        Dictionary<MouseButton, Key> _heldButtons;
        std::unordered_set<MenuKey> _menuPressed;
        // The menu keys held (the sliders step again while one is held).
        std::unordered_set<MenuKey> _menuDown;
        std::unordered_set<RawKey> _rawPressed;
        std::unordered_set<Pointer> _clicked;
        // The pointer's buttons held (a finger on a touch screen's menus: the left).
        std::unordered_set<Pointer> _pointers;

        std::unordered_set<Key> _down;
        // Keys held by mouse buttons, apart from the keyboard's.
        std::unordered_set<Key> _buttons;
        // Keys held by the fixed flying camera keys.
        std::unordered_set<Key> _fly;
        // Keys the wheel holds until a game step uses them (ClearMouse).
        std::unordered_set<Key> _wheel;
        std::unordered_set<Key> _pressed;
        // Keys held by the program (tests), whatever the keyboard does.
        std::unordered_set<Key> _held;
        // Keys held by the on-screen controls of a touch screen (TouchControls).
        std::unordered_set<Key> _touched;
        // Keys and menu keys held by a gamepad's buttons and left stick (GamepadMap).
        std::unordered_set<Key> _pad;
        std::unordered_set<MenuKey> _padMenu;
        // The gamepad's bindings: GamepadMap's defaults, changed by BindPad.
        Dictionary<PadButton, Key> _padKeys;
        // The buttons held: a trigger, whose axis reports every move, names itself once a pull.
        std::unordered_set<PadButton> _padHeld;
        // Turbo is held until "always run" is pressed again (ToggleRun).
        bool _alwaysRun = false;
        // Caps Lock's state: "always run" bound to it follows the lock, not its presses.
        Lock _capsLock = Lock::Off;

        float _mouseX = 0;
        float _mouseY = 0;
        float _wheelNotches = 0;
        float _zoomWheel = 0;
        float _pointerX = 0;
        float _pointerY = 0;
        bool _pointerMoved = false;
        std::string _typed;
        bool _anyPressed = false;
        std::string _lastControl;
        std::string _lastPadControl;
        int _digit = NoDigit;
    };
}
