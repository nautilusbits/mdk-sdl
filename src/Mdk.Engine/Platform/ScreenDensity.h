#pragma once

namespace Mdk::Engine::Platform
{
    // Physical sizes on a display: window units per millimetre from SDL's display scale (the OS's
    // scale over its base density) and the window's pixel density (pixels per unit).
    //   units per mm = base dpi × display scale / pixel density / 25.4
    //   Android: 160 dpi × 2.625 (420 dpi) / 1 / 25.4 = 16.5 px/mm
    struct ScreenDensity
    {
    private:
        static constexpr float MmPerInch = 25.4f;

    public:
        // Dots per inch at display scale 1: Android's density-independent pixel.
        static constexpr float AndroidDpi = 160.0f;
        // Dots per inch at display scale 1: Windows' and Linux' 100 %.
        static constexpr float DesktopDpi = 96.0f;

        // The OS's base density.
        static float BaseDpi()
        {
#if defined(__ANDROID__)
            return AndroidDpi;
#else
            return DesktopDpi;
#endif
        }

        // Window units per millimetre; 0 when SDL doesn't know the display.
        static float UnitsPerMm(float displayScale, float pixelDensity, float baseDpi)
        {
            if (displayScale <= 0.0f || pixelDensity <= 0.0f)
            {
                return 0.0f;
            }

            return baseDpi * displayScale / pixelDensity / MmPerInch;
        }
    };
}
