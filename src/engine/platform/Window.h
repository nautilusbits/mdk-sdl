#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/DotNet.h"
#include "core/Drawing.h"
#include "engine/platform/Display.h"

struct SDL_Window;
struct SDL_Cursor;
union SDL_Event;

namespace Mdk::Engine::Platform
{
    class Input;
    class TouchControls;
    class Gamepads;
    class HeadTracker;
    enum class TouchButtons;
    enum class InputSource;
    enum class HeadTracking;
    enum class TouchPart;
    struct TouchPlace;
    enum class MouseButton;
    struct WindowProbe;

    enum class Capture { Off, On };

    // Whether typed text is read (on a touch screen: the on-screen keyboard shows).
    enum class Typing { Off, On };

    enum class Visibility { Shown, Hidden };

    // C#'s (int Width, int Height) tuple.
    struct WindowSize
    {
        int Width = 0;
        int Height = 0;
    };

    // The game window and its events (SDL3). Fills Input each frame.
    class Window
    {
    public:
        Window(const std::string& title, int width, int height, Platform::Visibility visibility = Platform::Visibility::Shown);
        ~Window();
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        SDL_Window* Handle() const { return _handle; }

        // A hidden window (tests) is never shown nor focused: frames are drawn off screen.
        Platform::Visibility Visibility() const { return _visibility; }

        // The on-screen controls while they're used (play, shown), or null.
        TouchControls* Touch() const;

        // A touch screen (Android): the on-screen controls' setting applies.
        bool HasTouchScreen() const { return _touch != nullptr; }

        // A touch screen reads text only while asked (the console): its keyboard shows then.
        // Other windows always read it.
        void SetTyping(Typing typing);

        // Windowed at a size, fullscreen on the desktop, or in a mode of the display of its own;
        // always fullscreen on a touch screen (a phone). Acts only on a change (a window resized by
        // hand keeps its size), never on a hidden window.
        void Apply(const DisplaySetup& setup);

        Platform::Fullscreen Fullscreen() const;

        // The window's display's desktop mode.
        ScreenMode Desktop() const;

        // Window sizes that fit the display (Resolutions::Windowed).
        std::vector<Resolution> WindowedSizes() const;

        // The display's modes, one per size (Resolutions::Exclusive).
        std::vector<ScreenMode> ExclusiveModes() const;

        // SDL's version, e.g. "3.4.0".
        static std::string SdlVersion();

        // The window's size in pixels.
        WindowSize Size() const;

        // Window units per millimetre of the display the window is on (0: unknown).
        float UnitsPerMm() const;

        // The part of the window clear of cut-outs and rounded corners (window units); all of it
        // when SDL doesn't know.
        RectangleF SafeArea() const;

        // The mouse cursor becomes an image (RGBA8, width x height) whose hotspot is (hotX, hotY).
        void SetCursor(std::span<const byte> rgba, int width, int height, int hotX, int hotY);

        // Mouse captured for looking around (relative motion, hidden cursor).
        void CaptureMouse(Capture capture);

        // The gyroscope turns the view in play (a phone in a VR viewer).
        void TrackHead(HeadTracking tracking);

        // When the on-screen controls show (TouchButtons).
        void ShowTouch(TouchButtons mode) { _touchMode = mode; }

        // Where the player put the on-screen controls (TouchLayout), and their opacity (0-1).
        void ArrangeTouch(const std::map<TouchPart, TouchPlace>& places, float opacity);

        // Handles pending events. Returns false when the window closes.
        bool PumpEvents(Input& input);

    private:
        // Tests reach private members through it (the C# tests used reflection).
        friend struct WindowProbe;

        // The keyboard without corrections or capitals: commands and save names aren't words.
        void StartTyping();

        // Android's Back hides the keyboard first (SDL stops the text input): that's the user's
        // Back, not ours, so it presses Back (the console closes at once).
        void WatchKeyboard(Input& input);

        // The program's icon on the window (title bar, task bar, dock). A missing icon is only
        // reported: the game runs without it.
        void SetIcon();

        // Exclusive fullscreen in the display's mode of that size at its highest refresh rate
        // (none, or none of that size: the desktop's).
        void SetExclusive(std::optional<Resolution> size);

        // A finger, or a mouse event SDL makes of one. A finger's click release passes: a menu tap
        // that starts play lets go in play, and the left button (fire) would stay held.
        static bool IsTouch(const SDL_Event& e);

        // A gamepad's, a keyboard's or a real mouse's press (not Android's back button, which
        // touch players use too).
        static bool IsDevice(const SDL_Event& e);

        // What played last (Auto); controls going hidden let go of their keys.
        void PlayedBy(InputSource source, Input& input);

        // In play on a touch screen, fingers drive the on-screen controls and the mouse events SDL
        // makes of them are dropped.
        void TouchEvent(const SDL_Event& e, Input& input);

        // MouseButton keeps SDL's numbers.
        static MouseButton Button(byte button);

        SDL_Window* _handle = nullptr;

        // The mouse cursor set by SetCursor, or null for the system's.
        SDL_Cursor* _cursor = nullptr;

        Platform::Visibility _visibility;

        // The on-screen controls of a touch screen (Android), or null.
        std::unique_ptr<TouchControls> _touch;
        // The mouse looks around (play): touches are the on-screen controls, not clicks.
        Capture _capture = Capture::Off;

        // When the on-screen controls show, and what played last (Auto).
        TouchButtons _touchMode{};
        InputSource _lastSource{};

        // The text input asked for (SetTyping).
        Typing _typing = Typing::Off;

        // The gamepads (none in a hidden window: tests aren't steered by a pad left plugged in).
        std::unique_ptr<Gamepads> _gamepads;
        // A pointer (C# held it by value) so this header needn't include HeadTracker.h.
        std::unique_ptr<HeadTracker> _head;

        // The setup last applied (none yet).
        std::optional<DisplaySetup> _setup;
    };
}
