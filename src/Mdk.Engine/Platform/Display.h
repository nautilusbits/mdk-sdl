#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Mdk.Core/TimeSpan.h"

namespace Mdk::Engine::Platform
{
    // Windowed, fullscreen on the desktop (borderless), or exclusive (a mode of the display's own).
    enum class Fullscreen { Off, Desktop, Exclusive };

    // The operating system, for what the game offers on it (GPU backends, window options).
    enum class Os { Windows, Linux, MacOs, Android };

    struct OsInfo
    {
        static Os Current();
    };

    // A size in pixels, written `1280x960`.
    struct Resolution
    {
        int Width = 0;
        int Height = 0;

        int Area() const { return Width * Height; }

        std::string ToString() const;

        // "1280x960"; both sides positive.
        static bool TryParse(const std::string& text, Resolution& size);

        bool operator==(const Resolution&) const = default;

    private:
        static constexpr char Separator = 'x';
    };

    // A mode of a display: its size and refresh rate (Hz).
    struct ScreenMode
    {
        int Width = 0;
        int Height = 0;
        float Refresh = 0;

        Resolution Size() const { return {Width, Height}; }

        bool operator==(const ScreenMode&) const = default;
    };

    // How the window shows: its fullscreen mode, its size when windowed, and the display's mode in
    // exclusive fullscreen (none: the desktop's).
    struct DisplaySetup
    {
        Platform::Fullscreen Fullscreen = Platform::Fullscreen::Off;
        Resolution Window;
        std::optional<Resolution> Exclusive;

        bool operator==(const DisplaySetup&) const = default;
    };

    // The sizes the Display options step through.
    //   windowed:  presets that fit the display's usable area (no task bar), plus the window's size
    //   exclusive: the display's modes, one per size (its highest refresh rate)
    struct Resolutions
    {
        static constexpr std::array<Resolution, 11> Presets{{
            {640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 960}, {1600, 900},
            {1600, 1200}, {1920, 1080}, {1920, 1440}, {2560, 1440}, {3840, 2160},
        }};

        // The presets fitting `usable`, and `current`, by area.
        static std::vector<Resolution> Windowed(Resolution usable, Resolution current);

        // One mode per size, the highest refresh rate, by area.
        static std::vector<ScreenMode> Exclusive(std::span<const ScreenMode> modes);

        // The size `step` places from `current` (wrapping); from outside the list, the first.
        static Resolution Step(std::span<const Resolution> list, Resolution current, int step);
    };

    // Keeps frames at most `limit` a second (VSync off).
    struct FrameLimiter
    {
        static constexpr int Off = 0;
        static constexpr std::array<int, 4> Limits{Off, 60, 120, 144};

        // What's left of a frame's time after `elapsed`.
        static TimeSpan Remaining(TimeSpan elapsed, int limit);

        // Sleeps the rest of the frame (precisely: SDL_DelayPrecise).
        static void Wait(TimeSpan elapsed, int limit);
    };
}
