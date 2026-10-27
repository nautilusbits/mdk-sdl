using System.Globalization;
using SDL;

namespace Mdk.Engine.Platform;

/// <summary>Keys the game uses, independent of SDL.</summary>
public enum Key
{
    Forward, Back, StrafeLeft, StrafeRight, TurnLeft, TurnRight, Jump, Turbo, Fire,
    /// <summary>Kurt's items: use the selected one, select the next, previous, or slot 1-5.</summary>
    UseItem, ItemNext, ItemPrevious, Item1, Item2, Item3, Item4, Item5,
    /// <summary>Flying camera: up and down.</summary>
    Up, Down,
    Escape, Screenshot,
    /// <summary>Switches between Kurt and the flying camera.</summary>
    Fly,
    /// <summary>Sniper mode on or off, and its zoom.</summary>
    Sniper, ZoomIn, ZoomOut,
    /// <summary>The 1996 demo's levels: rolls, and teleports (held with a digit).</summary>
    RollLeft, RollRight, Teleport,
    /// <summary>The level's full save (the original's F2), and loading the latest save.</summary>
    QuickSave, QuickLoad,
    /// <summary>Turbo on until pressed again; on Caps Lock, while it is locked (the original's).</summary>
    AlwaysRun,
}

/// <summary>Keys of the menus and prompts: fixed, whatever the game's bindings.</summary>
public enum MenuKey { Up, Down, Left, Right, Accept, Back, Backspace, Delete, Home, End }

/// <summary>Keys by their place on the keyboard, whatever the layout and the bindings (the console,
/// the debug overlay); the keyboard's repeat presses them again. <see cref="Grave"/> is the key left
/// of 1.</summary>
public enum RawKey { Grave, F3, Tab, Enter, Backspace, Up, Down, PageUp, PageDown, Escape }

/// <summary>Mouse buttons that click menu items.</summary>
public enum Pointer { Left, Right }

/// <summary>Keyboard and mouse state of the current frame.</summary>
public sealed class Input
{
    public enum State { Up, Down }
    /// <summary>A lock key's state (Caps Lock: its light).</summary>
    public enum Lock { Off, On }

    private static readonly Dictionary<SDL_Scancode, Key> DefaultKeys = new()
    {
        [SDL_Scancode.SDL_SCANCODE_W] = Key.Forward,
        [SDL_Scancode.SDL_SCANCODE_UP] = Key.Forward,
        [SDL_Scancode.SDL_SCANCODE_S] = Key.Back,
        [SDL_Scancode.SDL_SCANCODE_DOWN] = Key.Back,
        [SDL_Scancode.SDL_SCANCODE_A] = Key.StrafeLeft,
        [SDL_Scancode.SDL_SCANCODE_D] = Key.StrafeRight,
        [SDL_Scancode.SDL_SCANCODE_LEFT] = Key.TurnLeft,
        [SDL_Scancode.SDL_SCANCODE_RIGHT] = Key.TurnRight,
        [SDL_Scancode.SDL_SCANCODE_SPACE] = Key.Jump,
        [SDL_Scancode.SDL_SCANCODE_LSHIFT] = Key.Turbo,
        [SDL_Scancode.SDL_SCANCODE_RSHIFT] = Key.Turbo,
        [SDL_Scancode.SDL_SCANCODE_CAPSLOCK] = Key.AlwaysRun,
        [SDL_Scancode.SDL_SCANCODE_LCTRL] = Key.Fire,
        [SDL_Scancode.SDL_SCANCODE_RCTRL] = Key.Fire,
        [SDL_Scancode.SDL_SCANCODE_RETURN] = Key.UseItem,
        [SDL_Scancode.SDL_SCANCODE_E] = Key.UseItem,
        [SDL_Scancode.SDL_SCANCODE_TAB] = Key.ItemNext,
        [SDL_Scancode.SDL_SCANCODE_RIGHTBRACKET] = Key.ItemNext,
        [SDL_Scancode.SDL_SCANCODE_LEFTBRACKET] = Key.ItemPrevious,
        [SDL_Scancode.SDL_SCANCODE_1] = Key.Item1,
        [SDL_Scancode.SDL_SCANCODE_2] = Key.Item2,
        [SDL_Scancode.SDL_SCANCODE_3] = Key.Item3,
        [SDL_Scancode.SDL_SCANCODE_4] = Key.Item4,
        [SDL_Scancode.SDL_SCANCODE_5] = Key.Item5,
        [SDL_Scancode.SDL_SCANCODE_ESCAPE] = Key.Escape,
        [SDL_Scancode.SDL_SCANCODE_AC_BACK] = Key.Escape,
        [SDL_Scancode.SDL_SCANCODE_F12] = Key.Screenshot,
        [SDL_Scancode.SDL_SCANCODE_F1] = Key.Fly,
        [SDL_Scancode.SDL_SCANCODE_Z] = Key.RollLeft,
        [SDL_Scancode.SDL_SCANCODE_C] = Key.RollRight,
        [SDL_Scancode.SDL_SCANCODE_T] = Key.Teleport,
        [SDL_Scancode.SDL_SCANCODE_LALT] = Key.Teleport,
        [SDL_Scancode.SDL_SCANCODE_RALT] = Key.Teleport,
        [SDL_Scancode.SDL_SCANCODE_F2] = Key.QuickSave,
        [SDL_Scancode.SDL_SCANCODE_F9] = Key.QuickLoad,
    };

    /// <summary>The scope's own bindings (its zoom): they apply while <see cref="Scoped"/>, and anywhere
    /// else a key has no binding of its own. A key can be bound to a scope action and to another one:
    /// the scope's wins while sniping, the other one wins otherwise.</summary>
    private static readonly Dictionary<SDL_Scancode, Key> DefaultScopeKeys = new()
    {
        [SDL_Scancode.SDL_SCANCODE_PAGEUP] = Key.ZoomIn,
        [SDL_Scancode.SDL_SCANCODE_EQUALS] = Key.ZoomIn,
        [SDL_Scancode.SDL_SCANCODE_PAGEDOWN] = Key.ZoomOut,
        [SDL_Scancode.SDL_SCANCODE_MINUS] = Key.ZoomOut,
    };

    /// <summary>The flying camera's keys: fixed, whatever the bindings (E also uses items).</summary>
    private static readonly Dictionary<SDL_Scancode, Key> FlyKeys = new()
    {
        [SDL_Scancode.SDL_SCANCODE_E] = Key.Up,
        [SDL_Scancode.SDL_SCANCODE_Q] = Key.Down,
    };

    /// <summary>The digit keys, of the number row and the keypad, 0 to 9.</summary>
    private static readonly SDL_Scancode[][] DigitKeys =
    [
        [SDL_Scancode.SDL_SCANCODE_0, SDL_Scancode.SDL_SCANCODE_1, SDL_Scancode.SDL_SCANCODE_2, SDL_Scancode.SDL_SCANCODE_3,
            SDL_Scancode.SDL_SCANCODE_4, SDL_Scancode.SDL_SCANCODE_5, SDL_Scancode.SDL_SCANCODE_6, SDL_Scancode.SDL_SCANCODE_7,
            SDL_Scancode.SDL_SCANCODE_8, SDL_Scancode.SDL_SCANCODE_9],
        [SDL_Scancode.SDL_SCANCODE_KP_0, SDL_Scancode.SDL_SCANCODE_KP_1, SDL_Scancode.SDL_SCANCODE_KP_2, SDL_Scancode.SDL_SCANCODE_KP_3,
            SDL_Scancode.SDL_SCANCODE_KP_4, SDL_Scancode.SDL_SCANCODE_KP_5, SDL_Scancode.SDL_SCANCODE_KP_6, SDL_Scancode.SDL_SCANCODE_KP_7,
            SDL_Scancode.SDL_SCANCODE_KP_8, SDL_Scancode.SDL_SCANCODE_KP_9],
    ];

    /// <summary>No digit pressed (<see cref="Digit"/>).</summary>
    public const int NoDigit = -1;

    /// <summary>The keys of mouse buttons: the left one fires, the right one toggles sniper mode,
    /// the wheel picks items.</summary>
    private static readonly Dictionary<MouseButton, Key> DefaultButtons = new()
    {
        [MouseButton.Left] = Key.Fire,
        [MouseButton.Right] = Key.Sniper,
        [MouseButton.WheelUp] = Key.ItemPrevious,
        [MouseButton.WheelDown] = Key.ItemNext,
    };

    private static readonly Dictionary<SDL_Scancode, MenuKey> MenuKeys = new()
    {
        [SDL_Scancode.SDL_SCANCODE_UP] = MenuKey.Up,
        [SDL_Scancode.SDL_SCANCODE_DOWN] = MenuKey.Down,
        [SDL_Scancode.SDL_SCANCODE_LEFT] = MenuKey.Left,
        [SDL_Scancode.SDL_SCANCODE_RIGHT] = MenuKey.Right,
        [SDL_Scancode.SDL_SCANCODE_RETURN] = MenuKey.Accept,
        [SDL_Scancode.SDL_SCANCODE_KP_ENTER] = MenuKey.Accept,
        [SDL_Scancode.SDL_SCANCODE_SPACE] = MenuKey.Accept,
        [SDL_Scancode.SDL_SCANCODE_ESCAPE] = MenuKey.Back,
        // Android's back button.
        [SDL_Scancode.SDL_SCANCODE_AC_BACK] = MenuKey.Back,
        [SDL_Scancode.SDL_SCANCODE_BACKSPACE] = MenuKey.Backspace,
        [SDL_Scancode.SDL_SCANCODE_DELETE] = MenuKey.Delete,
        [SDL_Scancode.SDL_SCANCODE_HOME] = MenuKey.Home,
        [SDL_Scancode.SDL_SCANCODE_END] = MenuKey.End,
    };

    private static readonly Dictionary<SDL_Scancode, RawKey> RawKeys = new()
    {
        [SDL_Scancode.SDL_SCANCODE_GRAVE] = RawKey.Grave,
        [SDL_Scancode.SDL_SCANCODE_F3] = RawKey.F3,
        [SDL_Scancode.SDL_SCANCODE_TAB] = RawKey.Tab,
        [SDL_Scancode.SDL_SCANCODE_RETURN] = RawKey.Enter,
        [SDL_Scancode.SDL_SCANCODE_KP_ENTER] = RawKey.Enter,
        [SDL_Scancode.SDL_SCANCODE_BACKSPACE] = RawKey.Backspace,
        [SDL_Scancode.SDL_SCANCODE_UP] = RawKey.Up,
        [SDL_Scancode.SDL_SCANCODE_DOWN] = RawKey.Down,
        [SDL_Scancode.SDL_SCANCODE_PAGEUP] = RawKey.PageUp,
        [SDL_Scancode.SDL_SCANCODE_PAGEDOWN] = RawKey.PageDown,
        [SDL_Scancode.SDL_SCANCODE_ESCAPE] = RawKey.Escape,
    };

    /// <summary>Names of mouse buttons as bindings (<see cref="Bind"/>); others are
    /// <see cref="ButtonPrefix"/> and their number ("Mouse 4").</summary>
    private static readonly Dictionary<MouseButton, string> ButtonNames = new()
    {
        [MouseButton.Left] = "Left mouse",
        [MouseButton.Right] = "Right mouse",
        [MouseButton.Middle] = "Middle mouse",
        [MouseButton.WheelUp] = "Wheel up",
        [MouseButton.WheelDown] = "Wheel down",
    };

    private const string ButtonPrefix = "Mouse ";

    /// <summary>The bindings: the defaults, changed by <see cref="Bind"/>.</summary>
    private readonly Dictionary<SDL_Scancode, Key> _keys = new(DefaultKeys);
    private readonly Dictionary<MouseButton, Key> _buttonKeys = new(DefaultButtons);
    /// <summary>The scope's bindings (<see cref="DefaultScopeKeys"/>): zoom.</summary>
    private readonly Dictionary<SDL_Scancode, Key> _scopeKeys = new(DefaultScopeKeys);
    private readonly Dictionary<MouseButton, Key> _scopeButtons = [];
    /// <summary>Scope keys that are still the built-in ones: binding the key to another action
    /// replaces them (an explicit scope binding is kept).</summary>
    private readonly HashSet<SDL_Scancode> _scopeDefaultKeys = new(DefaultScopeKeys.Keys);
    /// <summary>Wheel directions that still zoom while sniping, as they do by default: binding the
    /// wheel to any action takes the zoom away.</summary>
    private readonly HashSet<MouseButton> _wheelZoom = [MouseButton.WheelUp, MouseButton.WheelDown];
    /// <summary>The action each held key or mouse button pressed, so that it is the one released.</summary>
    private readonly Dictionary<SDL_Scancode, Key> _heldKeys = [];
    private readonly Dictionary<MouseButton, Key> _heldButtons = [];
    private readonly HashSet<MenuKey> _menuPressed = [];
    /// <summary>The menu keys held (the sliders step again while one is held).</summary>
    private readonly HashSet<MenuKey> _menuDown = [];
    private readonly HashSet<RawKey> _rawPressed = [];
    private readonly HashSet<Pointer> _clicked = [];
    /// <summary>The pointer's buttons held (a finger on a touch screen's menus: the left).</summary>
    private readonly HashSet<Pointer> _pointers = [];

    private readonly HashSet<Key> _down = [];
    /// <summary>Keys held by mouse buttons, apart from the keyboard's.</summary>
    private readonly HashSet<Key> _buttons = [];
    /// <summary>Keys held by the fixed flying camera keys.</summary>
    private readonly HashSet<Key> _fly = [];
    /// <summary>Keys the wheel holds until a game step uses them (<see cref="ClearMouse"/>).</summary>
    private readonly HashSet<Key> _wheel = [];
    private readonly HashSet<Key> _pressed = [];
    /// <summary>Keys held by the program (tests), whatever the keyboard does.</summary>
    private readonly HashSet<Key> _held = [];
    /// <summary>Keys held by the on-screen controls of a touch screen (<see cref="TouchControls"/>).</summary>
    private readonly HashSet<Key> _touched = [];
    /// <summary>Keys and menu keys held by a gamepad's buttons and left stick (<see cref="GamepadMap"/>).</summary>
    private readonly HashSet<Key> _pad = [];
    private readonly HashSet<MenuKey> _padMenu = [];
    /// <summary>The gamepad's bindings: <see cref="GamepadMap"/>'s defaults, changed by <see cref="BindPad"/>.</summary>
    private readonly Dictionary<PadButton, Key> _padKeys = [];
    /// <summary>The buttons held: a trigger, whose axis reports every move, names itself once a pull.</summary>
    private readonly HashSet<PadButton> _padHeld = [];
    /// <summary>Turbo is held until "always run" is pressed again (<see cref="ToggleRun"/>).</summary>
    private bool _alwaysRun;
    /// <summary>Caps Lock's state: "always run" bound to it follows the lock, not its presses.</summary>
    private Lock _capsLock;

    /// <summary>The game's degrees a mouse count turns (Kurt, the cameras): the gamepad's and the
    /// gyroscope's look in degrees becomes mouse counts.</summary>
    private const float LookDegrees = 0.15f;

    public float MouseX { get; private set; }
    public float MouseY { get; private set; }
    /// <summary>Mouse wheel notches since the last game step: positive away from the user.</summary>
    public float Wheel { get; private set; }
    /// <summary>The wheel notches that zoom the scope since the last game step (positive: zoom in): the
    /// wheel only zooms while <see cref="Scoped"/>, and unless it is bound to something else.</summary>
    public float ZoomWheel { get; private set; }
    /// <summary>Kurt is sniping: the scope's bindings win over the others (set by the game each step).</summary>
    public bool Scoped { get; set; }

    /// <summary>Mouse motion is scaled by this (the mouse sensitivity).</summary>
    public float MouseScale { get; set; } = 1f;
    /// <summary>Vertical mouse motion is inverted.</summary>
    public bool InvertMouse { get; set; }
    /// <summary>The gamepad's look is scaled by this; its vertical look is inverted.</summary>
    public float PadSensitivity { get; set; } = 1f;
    public bool InvertPad { get; set; }
    /// <summary>The pointer in the window (pixels).</summary>
    public float PointerX { get; private set; }
    public float PointerY { get; private set; }
    /// <summary>The pointer moved this frame.</summary>
    public bool PointerMoved { get; private set; }
    /// <summary>Text typed this frame, with an <see cref="Erase"/> for each Backspace where it came: a
    /// phone keyboard's correction of "fps" to "gps" is "\b\b\bgps" in one frame.</summary>
    public string Typed { get; private set; } = "";
    public const char Erase = '\b';
    /// <summary>Any key or mouse button was pressed this frame.</summary>
    public bool AnyPressed { get; private set; }
    /// <summary>The name of the key or mouse button pressed this frame (for <see cref="Bind"/>), or "".</summary>
    public string LastControl { get; private set; } = "";
    /// <summary>The name of the gamepad button pressed this frame (for <see cref="BindPad"/>), or "".</summary>
    public string LastPadControl { get; private set; } = "";
    /// <summary>The digit (0-9) pressed this frame, or <see cref="NoDigit"/>.</summary>
    public int Digit { get; private set; } = NoDigit;

    public bool IsDown(Key key) =>
        _down.Contains(key) || _buttons.Contains(key) || _fly.Contains(key) || _wheel.Contains(key) || _held.Contains(key) || _touched.Contains(key) || _pad.Contains(key)
        || (key == Key.Turbo && (_alwaysRun || CapsRuns));

    /// <summary>"Always run" is Caps Lock's and the lock is on.</summary>
    private bool CapsRuns =>
        _capsLock == Lock.On && _keys.GetValueOrDefault(SDL_Scancode.SDL_SCANCODE_CAPSLOCK) == Key.AlwaysRun;

    /// <summary>Turbo stays on, or no longer ("always run" pressed in play).</summary>
    public void ToggleRun() => _alwaysRun = !_alwaysRun;

    /// <summary>Caps Lock's state this frame (the window's keyboard).</summary>
    internal void LockCaps(Lock state) => _capsLock = state;

    /// <summary>Holds or releases a key from the program (automated tests).</summary>
    public void Hold(Key key, State state)
    {
        if (state == State.Down)
        {
            _held.Add(key);
            return;
        }

        _held.Remove(key);
    }

    /// <summary>Presses a menu key for this frame from the program (automated tests).</summary>
    public void Press(MenuKey key)
    {
        _menuPressed.Add(key);
        AnyPressed = true;
    }

    /// <summary>Presses a game key for this frame from the program (automated tests).</summary>
    public void Press(Key key)
    {
        _pressed.Add(key);
        AnyPressed = true;
    }

    /// <summary>Pressed this frame.</summary>
    public bool WasPressed(Key key) => _pressed.Contains(key);

    public bool WasPressed(MenuKey key) => _menuPressed.Contains(key);

    /// <summary>Held (the menus' sliders step again while a direction is held).</summary>
    public bool IsDown(MenuKey key) => _menuDown.Contains(key) || _padMenu.Contains(key);

    public bool WasPressed(RawKey key) => _rawPressed.Contains(key);

    public bool WasClicked(Pointer button) => _clicked.Contains(button);

    public bool IsDown(Pointer button) => _pointers.Contains(button);

    internal void BeginFrame()
    {
        _pressed.Clear();
        _menuPressed.Clear();
        _rawPressed.Clear();
        _clicked.Clear();
        PointerMoved = false;
        Typed = "";
        AnyPressed = false;
        LastControl = "";
        LastPadControl = "";
        Digit = NoDigit;
    }

    public Input() => ResetPadBindings();

    /// <summary>Binds a key or mouse button (by its name, as <see cref="LastControl"/> gives it) to
    /// <paramref name="key"/>, instead of its other bindings. Returns false for an unknown name.</summary>
    public bool Bind(Key key, string control)
    {
        var button = ParseButton(control);
        var scancode = button == null ? SDL3.SDL_GetScancodeFromName(control) : SDL_Scancode.SDL_SCANCODE_UNKNOWN;
        if (button == null && scancode == SDL_Scancode.SDL_SCANCODE_UNKNOWN)
        {
            return false;
        }

        Unbind(key);
        var scope = IsScopeAction(key);
        if (button is { } bound)
        {
            _wheelZoom.Remove(bound);
            (scope ? _scopeButtons : _buttonKeys)[bound] = key;
            return true;
        }

        if (scope)
        {
            _scopeKeys[scancode] = key;
            _scopeDefaultKeys.Remove(scancode);
            return true;
        }

        _keys[scancode] = key;
        if (_scopeDefaultKeys.Remove(scancode))
        {
            _scopeKeys.Remove(scancode);
        }

        return true;
    }

    /// <summary>The actions only the scope uses: its bindings are a layer of their own, so a key can
    /// zoom while sniping and do something else (walk) otherwise.</summary>
    private static bool IsScopeAction(Key key) => key is Key.ZoomIn or Key.ZoomOut;

    /// <summary>The action of a key: the scope's binding first while sniping, else the other one first.</summary>
    private bool TryResolve(SDL_Scancode scancode, out Key key) =>
        Scoped
            ? _scopeKeys.TryGetValue(scancode, out key) || _keys.TryGetValue(scancode, out key)
            : _keys.TryGetValue(scancode, out key) || _scopeKeys.TryGetValue(scancode, out key);

    /// <summary>The action of a mouse button, as <see cref="TryResolve(SDL_Scancode, out Key)"/>.</summary>
    private bool TryResolve(MouseButton button, out Key key) =>
        Scoped
            ? _scopeButtons.TryGetValue(button, out key) || _buttonKeys.TryGetValue(button, out key)
            : _buttonKeys.TryGetValue(button, out key) || _scopeButtons.TryGetValue(button, out key);

    /// <summary>Back to the default bindings.</summary>
    public void ResetBindings()
    {
        _keys.Clear();
        _buttonKeys.Clear();
        _scopeKeys.Clear();
        _scopeButtons.Clear();
        _scopeDefaultKeys.Clear();
        _wheelZoom.Clear();
        _wheelZoom.Add(MouseButton.WheelUp);
        _wheelZoom.Add(MouseButton.WheelDown);
        foreach (var (scancode, key) in DefaultScopeKeys)
        {
            _scopeKeys[scancode] = key;
            _scopeDefaultKeys.Add(scancode);
        }

        foreach (var (scancode, key) in DefaultKeys)
        {
            _keys[scancode] = key;
        }

        foreach (var (button, key) in DefaultButtons)
        {
            _buttonKeys[button] = key;
        }
    }

    /// <summary>Binds a gamepad button (by its name, <see cref="GamepadMap.Name"/>) to <paramref name="key"/>,
    /// instead of its other buttons. Returns false for an unknown name, and for Start (pause).</summary>
    public bool BindPad(Key key, string control)
    {
        if (GamepadMap.Parse(control) is not { } button || button == PadButton.Start)
        {
            return false;
        }

        foreach (var bound in _padKeys.Where(b => b.Value == key).Select(b => b.Key).ToList())
        {
            _padKeys.Remove(bound);
        }

        _padKeys[button] = key;
        return true;
    }

    /// <summary>Back to the gamepad's default buttons.</summary>
    public void ResetPadBindings()
    {
        _padKeys.Clear();
        foreach (var button in Enum.GetValues<PadButton>())
        {
            if (GamepadMap.Game(button) is { } key)
            {
                _padKeys[button] = key;
            }
        }
    }

    /// <summary>The name of a key's first gamepad button, or "-".</summary>
    public string DescribePad(Key key)
    {
        foreach (var (button, bound) in _padKeys)
        {
            if (bound == key)
            {
                return GamepadMap.Name(button);
            }
        }

        return "-";
    }

    /// <summary>The name of a key's first binding, or "-".</summary>
    public string Describe(Key key)
    {
        var scope = IsScopeAction(key);
        foreach (var (scancode, bound) in scope ? _scopeKeys : _keys)
        {
            if (bound == key)
            {
                return SDL3.SDL_GetScancodeName(scancode) ?? "-";
            }
        }

        foreach (var (button, bound) in scope ? _scopeButtons : _buttonKeys)
        {
            if (bound == key)
            {
                return ButtonName(button);
            }
        }

        return "-";
    }

    /// <summary>A mouse button's binding name: "Left mouse", "Mouse 8".</summary>
    private static string ButtonName(MouseButton button) =>
        ButtonNames.GetValueOrDefault(button) ?? ButtonPrefix + ((int)button).ToString(CultureInfo.InvariantCulture);

    /// <summary>The mouse button of a binding name, or null.</summary>
    private static MouseButton? ParseButton(string control)
    {
        var named = ButtonNames.FirstOrDefault(b => b.Value == control);
        if (named.Value != null)
        {
            return named.Key;
        }

        if (!control.StartsWith(ButtonPrefix, StringComparison.Ordinal)
            || !byte.TryParse(control[ButtonPrefix.Length..], NumberStyles.None, CultureInfo.InvariantCulture, out var number)
            || number == 0)
        {
            return null;
        }

        return (MouseButton)number;
    }

    private void Unbind(Key key)
    {
        foreach (var scancode in _keys.Where(k => k.Value == key).Select(k => k.Key).ToList())
        {
            _keys.Remove(scancode);
        }

        foreach (var scancode in _scopeKeys.Where(k => k.Value == key).Select(k => k.Key).ToList())
        {
            _scopeKeys.Remove(scancode);
            _scopeDefaultKeys.Remove(scancode);
        }

        foreach (var button in _buttonKeys.Where(b => b.Value == key).Select(b => b.Key).ToList())
        {
            _buttonKeys.Remove(button);
        }

        foreach (var button in _scopeButtons.Where(b => b.Value == key).Select(b => b.Key).ToList())
        {
            _scopeButtons.Remove(button);
        }
    }

    /// <summary>The mouse motion was used (by a game step).</summary>
    public void ClearMouse()
    {
        MouseX = 0f;
        MouseY = 0f;
        Wheel = 0f;
        ZoomWheel = 0f;
        _wheel.Clear();
    }

    /// <summary>The horizontal or vertical mouse motion, used at once (a frame's look): the next
    /// game step sees none.</summary>
    public float TakeMouseX()
    {
        var x = MouseX;
        MouseX = 0f;
        return x;
    }

    public float TakeMouseY()
    {
        var y = MouseY;
        MouseY = 0f;
        return y;
    }

    /// <summary>Someone else has this frame's input (the console): the game sees no presses, typing,
    /// clicks or mouse, and no keyboard or mouse keys held. Keys held by the program (tests) stay.</summary>
    public void Withhold()
    {
        BeginFrame();
        ClearMouse();
        _down.Clear();
        _buttons.Clear();
        _heldKeys.Clear();
        _heldButtons.Clear();
        _pointers.Clear();
        _fly.Clear();
        _menuDown.Clear();
        _pad.Clear();
        _padMenu.Clear();
        _padHeld.Clear();
    }

    internal void SetKey(SDL_Scancode scancode, State state, Repeat repeat)
    {
        if (MenuKeys.TryGetValue(scancode, out var menuKey))
        {
            if (state == State.Down)
            {
                _menuPressed.Add(menuKey);
                _menuDown.Add(menuKey);
            }
            else
            {
                _menuDown.Remove(menuKey);
            }
        }

        if (state == State.Down && RawKeys.TryGetValue(scancode, out var rawKey))
        {
            _rawPressed.Add(rawKey);
        }

        if (state == State.Down && scancode == SDL_Scancode.SDL_SCANCODE_BACKSPACE)
        {
            Typed += Erase;
        }

        if (state == State.Down && repeat == Repeat.No)
        {
            AnyPressed = true;
            LastControl = SDL3.SDL_GetScancodeName(scancode) ?? "";
            Digit = DigitKeys.Select(row => Array.IndexOf(row, scancode)).FirstOrDefault(d => d != NoDigit, Digit);
        }

        SetFlyKey(scancode, state);
        if (state == State.Up)
        {
            // The action it pressed, even if the bindings in force have changed since (sniping).
            if (_heldKeys.Remove(scancode, out var released))
            {
                _down.Remove(released);
            }

            return;
        }

        if (!_heldKeys.TryGetValue(scancode, out var key))
        {
            if (!TryResolve(scancode, out key))
            {
                return;
            }

            _heldKeys[scancode] = key;
        }

        // Caps Lock's "always run" follows the lock (LockCaps): its presses don't toggle.
        if (key == Key.AlwaysRun && scancode == SDL_Scancode.SDL_SCANCODE_CAPSLOCK)
        {
            return;
        }

        if (_down.Add(key))
        {
            _pressed.Add(key);
        }
    }

    /// <summary>A mouse button: see <see cref="DefaultButtons"/>.</summary>
    internal void SetButton(MouseButton button, State state)
    {
        HoldPointer(button, state);
        if (state == State.Down)
        {
            AnyPressed = true;
            LastControl = ButtonName(button);
            AddClick(button);
        }

        if (state == State.Up)
        {
            if (_heldButtons.Remove(button, out var released))
            {
                _buttons.Remove(released);
            }

            return;
        }

        if (!_heldButtons.TryGetValue(button, out var key))
        {
            if (!TryResolve(button, out key))
            {
                return;
            }

            _heldButtons[button] = key;
        }

        if (_buttons.Add(key))
        {
            _pressed.Add(key);
        }
    }

    /// <summary>Wheel notches: while sniping they zoom (<see cref="ZoomWheel"/>) if the wheel still has
    /// its zoom, or is bound to a zoom action; otherwise they press the wheel's bindings until the next
    /// game step.</summary>
    internal void AddWheel(float notches)
    {
        Wheel += notches;
        if (notches == 0f)
        {
            return;
        }

        var button = notches > 0f ? MouseButton.WheelUp : MouseButton.WheelDown;
        LastControl = ButtonName(button);
        if (Scoped)
        {
            var zoom = _scopeButtons.TryGetValue(button, out var scoped)
                ? scoped
                : _wheelZoom.Contains(button) ? (button == MouseButton.WheelUp ? Key.ZoomIn : Key.ZoomOut) : (Key?)null;
            if (zoom is { } direction)
            {
                // A notch holds the zoom key for a while (Scope.Wheel): positive zooms in.
                ZoomWheel += direction == Key.ZoomIn ? MathF.Abs(notches) : -MathF.Abs(notches);
                return;
            }
        }

        if (TryResolve(button, out var key) && _wheel.Add(key))
        {
            _pressed.Add(key);
        }
    }

    private void SetFlyKey(SDL_Scancode scancode, State state)
    {
        if (!FlyKeys.TryGetValue(scancode, out var key))
        {
            return;
        }

        if (state == State.Up)
        {
            _fly.Remove(key);
            return;
        }

        _fly.Add(key);
    }

    internal void AddMouseMotion(float x, float y)
    {
        MouseX += x * MouseScale;
        MouseY += (InvertMouse ? -y : y) * MouseScale;
    }

    internal void SetPointer(float x, float y)
    {
        PointerX = x;
        PointerY = y;
        PointerMoved = true;
    }

    internal void AddText(string text) => Typed += text;

    /// <summary>A look in degrees (right, down; a gamepad's stick, the gyroscope): as the mouse's, but
    /// neither scaled by its sensitivity nor inverted.</summary>
    internal void AddLook(System.Numerics.Vector2 degrees)
    {
        MouseX += degrees.X / LookDegrees;
        MouseY += degrees.Y / LookDegrees;
    }

    /// <summary>The gamepad's look (degrees: right, down; <see cref="GamepadMap.Look"/>), by its
    /// sensitivity, inverted or not.</summary>
    internal void AddPadLook(System.Numerics.Vector2 degrees) =>
        AddLook(new System.Numerics.Vector2(degrees.X, InvertPad ? -degrees.Y : degrees.Y) * PadSensitivity);

    /// <summary>A gamepad button: its bound key in play, its fixed menu key (<see cref="GamepadMap.Menu"/>).</summary>
    internal void SetPadButton(PadButton button, State state)
    {
        if (state == State.Up)
        {
            _padHeld.Remove(button);
        }
        else if (_padHeld.Add(button))
        {
            LastPadControl = GamepadMap.Name(button);
        }

        if (_padKeys.TryGetValue(button, out var key))
        {
            SetPad(key, state);
        }

        if (GamepadMap.Menu(button) is { } menuKey)
        {
            SetPadMenu(menuKey, state);
        }
    }

    /// <summary>A gamepad holds or releases a key; holding presses it this frame.</summary>
    internal void SetPad(Key key, State state)
    {
        if (state == State.Up)
        {
            _pad.Remove(key);
            return;
        }

        if (_pad.Add(key))
        {
            _pressed.Add(key);
            AnyPressed = true;
        }
    }

    /// <summary>A gamepad holds or releases a menu key; holding presses it this frame.</summary>
    internal void SetPadMenu(MenuKey key, State state)
    {
        if (state == State.Up)
        {
            _padMenu.Remove(key);
            return;
        }

        if (_padMenu.Add(key))
        {
            _menuPressed.Add(key);
            AnyPressed = true;
        }
    }

    /// <summary>An on-screen control holds or releases a key; holding presses it this frame.</summary>
    internal void Touch(Key key, State state)
    {
        if (state == State.Up)
        {
            _touched.Remove(key);
            return;
        }

        if (_touched.Add(key))
        {
            Press(key);
        }
    }

    /// <summary>The left or right button held or let go (<see cref="IsDown(Pointer)"/>).</summary>
    private void HoldPointer(MouseButton button, State state)
    {
        Pointer? pointer = button == MouseButton.Left ? Pointer.Left : button == MouseButton.Right ? Pointer.Right : null;
        if (pointer is not { } held)
        {
            return;
        }

        if (state == State.Down)
        {
            _pointers.Add(held);
            return;
        }

        _pointers.Remove(held);
    }

    private void AddClick(MouseButton button)
    {
        if (button == MouseButton.Left)
        {
            _clicked.Add(Pointer.Left);
        }
        else if (button == MouseButton.Right)
        {
            _clicked.Add(Pointer.Right);
        }
    }
}

/// <summary>Mouse buttons by SDL's numbers (any number is a button); the wheel's two directions
/// are buttons too, out of SDL's byte range.</summary>
internal enum MouseButton
{
    Left = 1, Middle = 2, Right = 3, Back = 4, Forward = 5,
    WheelUp = byte.MaxValue + 1, WheelDown = byte.MaxValue + 2,
}

/// <summary>Whether a key event is the keyboard's auto-repeat.</summary>
internal enum Repeat { No, Yes }
