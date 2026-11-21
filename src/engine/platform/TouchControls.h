#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Dictionary.h"
#include "core/Drawing.h"
#include "core/Numerics.h"
#include "engine/platform/Input.h"
#include "engine/platform/TouchLayout.h"

namespace Mdk::Engine::Platform
{
    // When the on-screen controls show: Auto hides them while a gamepad, a keyboard or a mouse
    // plays (a touch brings them back); Off never, On always.
    enum class TouchButtons { Auto, Off, On };

    // What played last: a finger on the screen, or a gamepad, a keyboard or a mouse.
    enum class InputSource { Touch, Device };

    // An on-screen button: its key and its area (window pixels).
    struct TouchButton
    {
        Platform::Key Key{};
        std::string Label;
        RectangleF Area;

        bool operator==(const TouchButton&) const = default;
    };

    // On-screen controls of a touch screen during play (Android). Each finger is a stick, a button
    // or a look: a finger landing in the stick's zone (the left part, or where the player put it;
    // TouchLayout) walks (the stick is where it lands), on a button holds its key, elsewhere drags
    // the view as the mouse does. A finger keeps its role: a look sliding over a button doesn't
    // press it, a button's finger doesn't look, except FIRE's: dragging it aims while it fires (one
    // thumb), the key held until it lifts.
    //   ┌───────────────────────────────────────────────┐
    //   │                                         [ ||] │  pause (Esc)
    //   │                                               │
    //   │   stick              look        [ - ] [ + ] [ITEM]
    //   │    ( o )                               [SNIPE] [USE ]
    //   │                                        [JUMP ] [FIRE]
    //   │                                                (radar)
    //   └───────────────────────────────────────────────┘
    // Sizes are physical (millimetres of the display), at most the parts of the screen's height
    // they were on a phone: a tablet's buttons don't grow with its pixels. The buttons keep inside
    // the window's safe area (cut-outs, rounded corners).
    class TouchControls
    {
    public:
        // The controls show (and act) by the setting and what played last.
        static bool Shown(TouchButtons mode, InputSource last)
        {
            return mode == TouchButtons::On || (mode == TouchButtons::Auto && last == InputSource::Touch);
        }

        // The controls' opacity (1: as drawn by default).
        float Opacity() const { return _opacity; }

        // Where the stick is (window pixels), or none when no finger walks.
        std::optional<Vector2> Stick() const { return _stick; }

        // The walking finger's place.
        std::optional<Vector2> StickAt() const;

        // A full push of the stick (pixels).
        float StickRadius(Vector2 screen) const { return StickReach(screen.Y, _unitsPerMm); }

        // A full push of the stick on a screen of this height (pixels).
        static float StickReach(float height, float unitsPerMm) { return Physical(StickSize, StickMm, height, unitsPerMm); }

        // The player's places (TouchLayout; missing controls keep their defaults) and the controls'
        // opacity.
        void Arrange(const TouchPlaces& places, float opacity);

        // The display the controls are on: its window units per millimetre (0: unknown) and the
        // window's safe area (empty: all of it). Set by the window every frame.
        void Measure(float unitsPerMm, RectangleF safe);

        // A size: `mm` millimetres, at most `part` of the screen's height (all of it on an unknown
        // display).
        //   phone  1080 px, 16.5 px/mm: min(0.15 × 1080, 11 × 16.5) = 162 px
        //   tablet 1800 px, 12.6 px/mm: min(0.15 × 1800, 11 × 12.6) = 139 px
        static float Physical(float part, float mm, float height, float unitsPerMm);

        // Whether a finger holds a button's key (drawn pressed).
        bool Held(Key key) const;

        // The buttons' areas on a screen of this size, in the order they're hit; the pause button
        // is at the top right.
        const std::vector<TouchButton>& Layout(Vector2 screen);

        // A finger lands: a button, the stick or a look.
        void Down(std::uint64_t id, Vector2 at, Vector2 screen, Input& input);

        // A finger moves: the stick walks, a look (or the fire button's finger) turns.
        void Move(std::uint64_t id, Vector2 at, Vector2 screen, Input& input);

        // A finger lifts: its key or the stick lets go.
        void Up(std::uint64_t id, Input& input);

        // Every finger lifts (play pauses: the menus take touches as clicks).
        void Release(Input& input);

    private:
        // The stick's reach: a part of the screen's height at most, in millimetres.
        static constexpr float StickSize = 0.12f;
        static constexpr float StickMm = 14.0f;
        // Pushes shorter than this part of the radius don't walk.
        static constexpr float DeadZone = 0.35f;
        // Pushes past this part of the radius run (turbo).
        static constexpr float RunZone = 1.5f;
        // Mouse motion per pixel dragged.
        static constexpr float LookScale = 0.5f;

        // The stick's keys: up, down, left, right, far.
        static constexpr std::array<Platform::Key, 5> StickKeys{Key::Forward, Key::Back, Key::StrafeLeft, Key::StrafeRight, Key::Turbo};

        enum class Role { Stick, Button, Look };

        struct Finger
        {
            TouchControls::Role Role;
            Vector2 At;
            Platform::Key Key{};
        };

        // A button's key: held, and pressed this frame. Pause opens the pause menu.
        static void Hold(Key key, Input& input);

        // The stick's keys for a push (1 = the radius; y down).
        static void Walk(Vector2 push, Input& input);

        Dictionary<std::uint64_t, Finger> _fingers;
        std::optional<std::uint64_t> _stickFinger;
        // The last layout and what it was made for: drawn every frame, made again only on a resize
        // or another display.
        std::vector<TouchButton> _layout;
        Vector2 _layoutScreen;
        float _layoutDensity = 0;
        RectangleF _layoutSafe;
        // Where a finger landing starts the stick.
        RectangleF _zone;

        // The player's places of the controls (none: the defaults); laid out again when they change.
        TouchPlaces _places;
        bool _placesChanged = false;

        float _opacity = 1.0f;

        // The display's window units per millimetre (0: unknown) and the window's safe area
        // (empty: all of it).
        float _unitsPerMm = 0;
        RectangleF _safe;

        std::optional<Vector2> _stick;
    };
}
