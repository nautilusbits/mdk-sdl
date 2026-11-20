#include "Mdk.Engine/Platform/Window.h"

#include <format>

#include <SDL3/SDL.h>

#include "Mdk.Core/Console.h"
#include "Mdk.Engine/Platform/AppIcon.h"
#include "Mdk.Engine/Platform/Gamepads.h"
#include "Mdk.Engine/Platform/HeadTracker.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Engine/Platform/ScreenDensity.h"
#include "Mdk.Engine/Platform/TouchControls.h"

namespace Mdk::Engine::Platform
{
    TouchControls* Window::Touch() const
    {
        return _capture == Capture::On && TouchControls::Shown(_touchMode, _lastSource) ? _touch.get() : nullptr;
    }

    void Window::SetTyping(Typing typing)
    {
        if (_touch == nullptr)
        {
            return;
        }

        _typing = typing;
        if (typing == Typing::On)
        {
            StartTyping();
            return;
        }

        SDL_StopTextInput(_handle);
    }

    void Window::StartTyping()
    {
        SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetBooleanProperty(properties, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
        SDL_SetNumberProperty(properties, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER, SDL_CAPITALIZE_NONE);
        SDL_StartTextInputWithProperties(_handle, properties);
        SDL_DestroyProperties(properties);
    }

    void Window::WatchKeyboard(Input& input)
    {
        if (_typing == Typing::Off || SDL_TextInputActive(_handle))
        {
            return;
        }

        _typing = Typing::Off;
        input.Press(MenuKey::Back);
    }

    Window::Window(const std::string& title, int width, int height, Platform::Visibility visibility)
        : _visibility(visibility), _touchMode(TouchButtons::Auto), _lastSource(InputSource::Touch),
          _head(std::make_unique<HeadTracker>())
    {
#if defined(__ANDROID__)
        _touch = std::make_unique<TouchControls>();
#endif

        // Android's back button is Esc, not "close the app".
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
        // Phones play sideways, either way up.
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        {
            throw InvalidOperationException(std::format("SDL_Init: {}", SDL_GetError()));
        }

        // Gamepads are optional: without SDL's gamepad support the keyboard and mouse still play.
        if (visibility == Platform::Visibility::Shown && SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        {
            _gamepads = std::make_unique<Gamepads>();
        }

        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
        if (visibility == Platform::Visibility::Hidden)
        {
            flags |= SDL_WINDOW_HIDDEN;
        }

        _handle = SDL_CreateWindow(title.c_str(), width, height, flags);
        if (_handle == nullptr)
        {
            throw InvalidOperationException(std::format("SDL_CreateWindow: {}", SDL_GetError()));
        }

        SetIcon();

        // Typed text (names of saved games, cheats); not on a touch screen, where it opens the keyboard.
        if (_touch == nullptr)
        {
            SDL_StartTextInput(_handle);
        }
    }

    void Window::SetIcon()
    {
        SDL_Surface* icon = AppIcon::Load();
        if (icon == nullptr)
        {
            Console::Error().WriteLine(std::format("Window icon: {}", SDL_GetError()));
            return;
        }

        if (!SDL_SetWindowIcon(_handle, icon))
        {
            Console::Error().WriteLine(std::format("Window icon: {}", SDL_GetError()));
        }

        SDL_DestroySurface(icon);
    }

    void Window::Apply(const DisplaySetup& setup)
    {
        if (setup == _setup || _visibility == Platform::Visibility::Hidden)
        {
            return;
        }

        _setup = setup;
        if (_touch != nullptr)
        {
            SDL_SetWindowFullscreen(_handle, true);
            return;
        }

        switch (setup.Fullscreen)
        {
            case Platform::Fullscreen::Off:
                SDL_SetWindowFullscreen(_handle, false);
                SDL_SetWindowSize(_handle, setup.Window.Width, setup.Window.Height);
                break;
            case Platform::Fullscreen::Desktop:
                SDL_SetWindowFullscreenMode(_handle, nullptr);
                SDL_SetWindowFullscreen(_handle, true);
                break;
            case Platform::Fullscreen::Exclusive:
                SetExclusive(setup.Exclusive);
                break;
        }
    }

    Platform::Fullscreen Window::Fullscreen() const
    {
        return _setup ? _setup->Fullscreen : Platform::Fullscreen::Off;
    }

    void Window::SetExclusive(std::optional<Resolution> size)
    {
        SDL_DisplayID display = SDL_GetDisplayForWindow(_handle);
        const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(display);
        int count = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
        for (int i = 0; modes != nullptr && size && i < count; i++)
        {
            Resolution wanted = *size;
            const SDL_DisplayMode* candidate = modes[i];
            if (candidate->w != wanted.Width || candidate->h != wanted.Height)
            {
                continue;
            }

            if (mode->w != wanted.Width || mode->h != wanted.Height || candidate->refresh_rate > mode->refresh_rate)
            {
                mode = candidate;
            }
        }

        if (!SDL_SetWindowFullscreenMode(_handle, mode) || !SDL_SetWindowFullscreen(_handle, true))
        {
            Console::Error().WriteLine(std::format("Exclusive fullscreen: {}", SDL_GetError()));
        }

        SDL_free(modes);
    }

    ScreenMode Window::Desktop() const
    {
        const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(_handle));
        return mode == nullptr ? ScreenMode{} : ScreenMode{mode->w, mode->h, mode->refresh_rate};
    }

    std::vector<Resolution> Window::WindowedSizes() const
    {
        SDL_Rect usable{};
        WindowSize current = Size();
        if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(_handle), &usable))
        {
            return {Resolution{current.Width, current.Height}};
        }

        return Resolutions::Windowed({usable.w, usable.h}, {current.Width, current.Height});
    }

    std::vector<ScreenMode> Window::ExclusiveModes() const
    {
        int count = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(_handle), &count);
        if (modes == nullptr)
        {
            return {};
        }

        std::vector<ScreenMode> list;
        list.reserve(count);
        for (int i = 0; i < count; i++)
        {
            list.push_back({modes[i]->w, modes[i]->h, modes[i]->refresh_rate});
        }

        SDL_free(modes);
        return Resolutions::Exclusive(list);
    }

    std::string Window::SdlVersion()
    {
        constexpr int Major = 1000000;
        constexpr int Minor = 1000;
        int version = SDL_GetVersion();
        return std::format("{}.{}.{}", version / Major, version / Minor % Minor, version % Minor);
    }

    WindowSize Window::Size() const
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(_handle, &width, &height);
        return {width, height};
    }

    float Window::UnitsPerMm() const
    {
        return ScreenDensity::UnitsPerMm(SDL_GetWindowDisplayScale(_handle), SDL_GetWindowPixelDensity(_handle), ScreenDensity::BaseDpi());
    }

    RectangleF Window::SafeArea() const
    {
        SDL_Rect safe{};
        if (SDL_GetWindowSafeArea(_handle, &safe) && safe.w > 0 && safe.h > 0)
        {
            return {static_cast<float>(safe.x), static_cast<float>(safe.y), static_cast<float>(safe.w), static_cast<float>(safe.h)};
        }

        auto [width, height] = Size();
        return {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
    }

    void Window::SetCursor(std::span<const byte> rgba, int width, int height, int hotX, int hotY)
    {
        constexpr int BytesPerPixel = 4;
        SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ABGR8888, const_cast<byte*>(rgba.data()), width * BytesPerPixel);
        if (surface == nullptr)
        {
            return;
        }

        SDL_Cursor* cursor = SDL_CreateColorCursor(surface, hotX, hotY);
        SDL_DestroySurface(surface);
        if (cursor == nullptr)
        {
            return;
        }

        SDL_SetCursor(cursor);
        if (_cursor != nullptr)
        {
            SDL_DestroyCursor(_cursor);
        }

        _cursor = cursor;
    }

    void Window::CaptureMouse(Capture capture)
    {
        _capture = capture;
        if (_touch == nullptr)
        {
            SDL_SetWindowRelativeMouseMode(_handle, capture == Capture::On);
        }
    }

    void Window::TrackHead(HeadTracking tracking) { _head->Set(tracking); }

    void Window::ArrangeTouch(const std::map<TouchPart, TouchPlace>& places, float opacity)
    {
        if (_touch != nullptr)
        {
            _touch->Arrange(places, opacity);
        }
    }

    bool Window::PumpEvents(Input& input)
    {
        input.BeginFrame();
        WatchKeyboard(input);

        // Out of play, touches are clicks (SDL makes mouse events of them).
        if (_capture == Capture::Off && _touch != nullptr)
        {
            _touch->Release(input);
        }

        // The controls' physical size, on whichever display the window is.
        if (_touch != nullptr)
        {
            _touch->Measure(UnitsPerMm(), SafeArea());
        }

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            // In play, touches are the on-screen controls (dropped while they're hidden), not clicks.
            if (_touch != nullptr && _capture == Capture::On && IsTouch(e))
            {
                PlayedBy(InputSource::Touch, input);
                if (Touch() != nullptr)
                {
                    TouchEvent(e, input);
                }

                continue;
            }

            if (_touch != nullptr && IsDevice(e))
            {
                PlayedBy(InputSource::Device, input);
            }

            if ((_gamepads != nullptr && _gamepads->Event(e, input)) || _head->Event(e, input, _capture == Capture::On, _handle))
            {
                continue;
            }

            switch (static_cast<SDL_EventType>(e.type))
            {
                case SDL_EVENT_QUIT:
                    return false;
                case SDL_EVENT_KEY_DOWN:
                    input.SetKey(e.key.scancode, Input::State::Down, e.key.repeat ? Repeat::Yes : Repeat::No);
                    break;
                case SDL_EVENT_KEY_UP:
                    input.SetKey(e.key.scancode, Input::State::Up, Repeat::No);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    input.SetButton(Button(e.button.button), Input::State::Down);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    input.SetButton(Button(e.button.button), Input::State::Up);
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    input.AddMouseMotion(e.motion.xrel, e.motion.yrel);
                    input.SetPointer(e.motion.x, e.motion.y);
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    input.AddText(e.text.text != nullptr ? e.text.text : "");
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    input.AddWheel(e.wheel.y);
                    break;
                default:
                    break;
            }
        }

        if (_gamepads != nullptr)
        {
            _gamepads->Update(input);
        }

        // Caps Lock's light ("always run"); a hidden window (tests) ignores the real keyboard's.
        if (_visibility == Platform::Visibility::Shown)
        {
            bool locked = (SDL_GetModState() & SDL_KMOD_CAPS) != 0;
            input.LockCaps(locked ? Input::Lock::On : Input::Lock::Off);
        }

        return true;
    }

    bool Window::IsTouch(const SDL_Event& e)
    {
        switch (static_cast<SDL_EventType>(e.type))
        {
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_MOTION:
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                return true;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                return e.button.which == SDL_TOUCH_MOUSEID;
            case SDL_EVENT_MOUSE_MOTION:
                return e.motion.which == SDL_TOUCH_MOUSEID;
            default:
                return false;
        }
    }

    bool Window::IsDevice(const SDL_Event& e)
    {
        switch (static_cast<SDL_EventType>(e.type))
        {
            case SDL_EVENT_GAMEPAD_ADDED:
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                return true;
            case SDL_EVENT_KEY_DOWN:
                return e.key.scancode != SDL_SCANCODE_AC_BACK;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                return e.button.which != SDL_TOUCH_MOUSEID;
            default:
                return false;
        }
    }

    void Window::PlayedBy(InputSource source, Input& input)
    {
        bool shown = Touch() != nullptr;
        _lastSource = source;
        if (shown && Touch() == nullptr)
        {
            _touch->Release(input);
        }
    }

    void Window::TouchEvent(const SDL_Event& e, Input& input)
    {
        WindowSize size = Size();
        Vector2 screen(static_cast<float>(size.Width), static_cast<float>(size.Height));
        Vector2 at = Vector2(e.tfinger.x, e.tfinger.y) * screen;
        auto finger = static_cast<std::uint64_t>(e.tfinger.fingerID);
        switch (static_cast<SDL_EventType>(e.type))
        {
            case SDL_EVENT_FINGER_DOWN:
                _touch->Down(finger, at, screen, input);
                break;
            case SDL_EVENT_FINGER_MOTION:
                _touch->Move(finger, at, screen, input);
                break;
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                _touch->Up(finger, input);
                break;
            default:
                break;
        }
    }

    MouseButton Window::Button(byte button) { return static_cast<MouseButton>(button); }

    Window::~Window()
    {
        if (_cursor != nullptr)
        {
            SDL_DestroyCursor(_cursor);
        }

        _gamepads.reset();
        _head.reset();
        SDL_DestroyWindow(_handle);
        SDL_Quit();
    }
}
