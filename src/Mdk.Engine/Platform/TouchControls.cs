using System.Drawing;
using System.Numerics;

namespace Mdk.Engine.Platform;

/// <summary>When the on-screen controls show: Auto hides them while a gamepad, a keyboard or a
/// mouse plays (a touch brings them back); Off never, On always.</summary>
public enum TouchButtons { Auto, Off, On }

/// <summary>What played last: a finger on the screen, or a gamepad, a keyboard or a mouse.</summary>
public enum InputSource { Touch, Device }

/// <summary>An on-screen button: its key and its area (window pixels).</summary>
public readonly record struct TouchButton(Key Key, string Label, RectangleF Area);

/// <summary>On-screen controls of a touch screen during play (Android). Each finger is a stick, a
/// button or a look: a finger landing in the stick's zone (the left part, or where the player put it;
/// <see cref="TouchLayout"/>) walks (the stick is where it lands), on a button holds its key,
/// elsewhere drags the view as the mouse does. A finger keeps its role: a look sliding over a
/// button doesn't press it, a button's finger doesn't look.
/// <code>
///   ┌───────────────────────────────────────────────┐
///   │                                         [ ||] │  pause (Esc)
///   │                                               │
///   │   stick              look        [ - ] [ + ] [ITEM]
///   │    ( o )                               [SNIPE] [USE ]
///   │                                        [JUMP ] [FIRE]
///   │                                                (radar)
///   └───────────────────────────────────────────────┘
/// Sizes are physical (millimetres of the display), at most the parts of the screen's height they
/// were on a phone: a tablet's buttons don't grow with its pixels. The buttons keep inside the
/// window's safe area (cut-outs, rounded corners).
/// </code></summary>
public sealed class TouchControls
{
    /// <summary>The stick's reach: a part of the screen's height at most, in millimetres.</summary>
    private const float StickSize = 0.12f;
    private const float StickMm = 14f;
    /// <summary>Pushes shorter than this part of the radius don't walk.</summary>
    private const float DeadZone = 0.35f;
    /// <summary>Pushes past this part of the radius run (turbo).</summary>
    private const float RunZone = 1.5f;
    /// <summary>Mouse motion per pixel dragged.</summary>
    private const float LookScale = 0.5f;

    /// <summary>The stick's keys: up, down, left, right, far.</summary>
    private static readonly Key[] StickKeys = [Key.Forward, Key.Back, Key.StrafeLeft, Key.StrafeRight, Key.Turbo];

    private enum Role { Stick, Button, Look }

    /// <summary>The controls show (and act) by the setting and what played last.</summary>
    public static bool Shown(TouchButtons mode, InputSource last) =>
        mode == TouchButtons.On || (mode == TouchButtons.Auto && last == InputSource.Touch);

    private sealed class Finger(Role role, Vector2 at, Key key = default)
    {
        public Role Role { get; } = role;
        public Key Key { get; } = key;
        public Vector2 At { get; set; } = at;
    }

    private readonly Dictionary<ulong, Finger> _fingers = [];
    private ulong? _stickFinger;
    /// <summary>The last layout and what it was made for: drawn every frame, made again only on a
    /// resize or another display.</summary>
    private IReadOnlyList<TouchButton> _layout = [];
    private Vector2 _layoutScreen;
    private float _layoutDensity;
    private RectangleF _layoutSafe;
    /// <summary>Where a finger landing starts the stick.</summary>
    private RectangleF _zone;

    /// <summary>The player's places of the controls (none: the defaults); laid out again when they change.</summary>
    private Dictionary<TouchPart, TouchPlace> _places = [];
    private bool _placesChanged;

    /// <summary>The controls' opacity (1: as drawn by default).</summary>
    public float Opacity { get; private set; } = 1f;

    /// <summary>The display's window units per millimetre (0: unknown) and the window's safe area
    /// (empty: all of it).</summary>
    private float _unitsPerMm;
    private RectangleF _safe;

    /// <summary>Where the stick is (window pixels), or null when no finger walks.</summary>
    public Vector2? Stick { get; private set; }

    /// <summary>The walking finger's place.</summary>
    public Vector2? StickAt => _stickFinger is { } id ? _fingers[id].At : null;

    /// <summary>A full push of the stick (pixels).</summary>
    public float StickRadius(Vector2 screen) => StickReach(screen.Y, _unitsPerMm);

    /// <summary>A full push of the stick on a screen of this height (pixels).</summary>
    internal static float StickReach(float height, float unitsPerMm) => Physical(StickSize, StickMm, height, unitsPerMm);

    /// <summary>The player's places (<see cref="TouchLayout"/>; missing controls keep their
    /// defaults) and the controls' opacity.</summary>
    internal void Arrange(IReadOnlyDictionary<TouchPart, TouchPlace> places, float opacity)
    {
        Opacity = opacity;
        if (places.Count == _places.Count && places.All(p => _places.TryGetValue(p.Key, out var place) && place == p.Value))
        {
            return;
        }

        _places = new Dictionary<TouchPart, TouchPlace>(places);
        _placesChanged = true;
    }

    /// <summary>The display the controls are on: its window units per millimetre (0: unknown) and
    /// the window's safe area (empty: all of it). Set by the window every frame.</summary>
    internal void Measure(float unitsPerMm, RectangleF safe)
    {
        _unitsPerMm = unitsPerMm;
        _safe = safe;
    }

    /// <summary>A size: <paramref name="mm"/> millimetres, at most <paramref name="part"/> of the
    /// screen's height (all of it on an unknown display).
    /// <code>
    ///   phone  1080 px, 16.5 px/mm: min(0.15 × 1080, 11 × 16.5) = 162 px
    ///   tablet 1800 px, 12.6 px/mm: min(0.15 × 1800, 11 × 12.6) = 139 px
    /// </code></summary>
    internal static float Physical(float part, float mm, float height, float unitsPerMm) =>
        unitsPerMm > 0f ? MathF.Min(part * height, mm * unitsPerMm) : part * height;

    /// <summary>Whether a finger holds a button's key (drawn pressed).</summary>
    public bool Held(Key key)
    {
        foreach (var finger in _fingers.Values)
        {
            if (finger.Role == Role.Button && finger.Key == key)
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>The buttons' areas on a screen of this size, in the order they're hit; the pause button is at the top right.</summary>
    public IReadOnlyList<TouchButton> Layout(Vector2 screen)
    {
        if (screen == _layoutScreen && _unitsPerMm == _layoutDensity && _safe == _layoutSafe && !_placesChanged)
        {
            return _layout;
        }

        var buttons = TouchLayout.Parts
            .Where(TouchLayout.IsButton)
            .Select(p => new TouchButton(TouchLayout.KeyOf(p), TouchLayout.LabelOf(p), TouchLayout.AreaOf(p, _places, screen, _safe, _unitsPerMm)))
            .ToList();

        _zone = TouchLayout.AreaOf(TouchPart.Stick, _places, screen, _safe, _unitsPerMm);
        _layout = buttons;
        (_layoutScreen, _layoutDensity, _layoutSafe, _placesChanged) = (screen, _unitsPerMm, _safe, false);
        return buttons;
    }

    /// <summary>A finger lands: a button, the stick or a look.</summary>
    internal void Down(ulong id, Vector2 at, Vector2 screen, Input input)
    {
        var button = Layout(screen).FirstOrDefault(b => b.Area.Contains(at.X, at.Y));
        if (button.Label != null)
        {
            _fingers[id] = new Finger(Role.Button, at, button.Key);
            Hold(button.Key, input);
            return;
        }

        if (_zone.Contains(at.X, at.Y) && _stickFinger == null)
        {
            _fingers[id] = new Finger(Role.Stick, at);
            _stickFinger = id;
            Stick = at;
            return;
        }

        _fingers[id] = new Finger(Role.Look, at);
    }

    /// <summary>A finger moves: the stick walks, a look turns.</summary>
    internal void Move(ulong id, Vector2 at, Vector2 screen, Input input)
    {
        if (!_fingers.TryGetValue(id, out var finger))
        {
            return;
        }

        var delta = at - finger.At;
        finger.At = at;
        if (finger.Role == Role.Look)
        {
            input.AddMouseMotion(delta.X * LookScale, delta.Y * LookScale);
            return;
        }

        if (finger.Role == Role.Stick)
        {
            Walk((at - Stick!.Value) / StickRadius(screen), input);
        }
    }

    /// <summary>A finger lifts: its key or the stick lets go.</summary>
    internal void Up(ulong id, Input input)
    {
        if (!_fingers.Remove(id, out var finger))
        {
            return;
        }

        if (finger.Role == Role.Button)
        {
            input.Touch(finger.Key, Input.State.Up);
            return;
        }

        if (finger.Role == Role.Stick)
        {
            Walk(Vector2.Zero, input);
            _stickFinger = null;
            Stick = null;
        }
    }

    /// <summary>Every finger lifts (play pauses: the menus take touches as clicks).</summary>
    internal void Release(Input input)
    {
        foreach (var id in _fingers.Keys.ToList())
        {
            Up(id, input);
        }
    }

    /// <summary>A button's key: held, and pressed this frame. Pause opens the pause menu.</summary>
    private static void Hold(Key key, Input input)
    {
        input.Touch(key, Input.State.Down);
        if (key == Key.Escape)
        {
            input.Press(MenuKey.Back);
        }
    }

    /// <summary>The stick's keys for a push (1 = the radius; y down).</summary>
    private static void Walk(Vector2 push, Input input)
    {
        bool[] down =
        [
            push.Y < -DeadZone,
            push.Y > DeadZone,
            push.X < -DeadZone,
            push.X > DeadZone,
            push.Length() > RunZone,
        ];

        for (var i = 0; i < StickKeys.Length; i++)
        {
            input.Touch(StickKeys[i], down[i] ? Input.State.Down : Input.State.Up);
        }
    }
}
