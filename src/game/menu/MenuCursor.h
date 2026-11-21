#pragma once

#include <optional>
#include <vector>

#include "core/DotNet.h"

namespace Mdk::Formats
{
    class Fti;
}

namespace Mdk::Game::Menu
{
    class Ui;

    // The original's mouse cursor in the menus (0x42c010): ARROW, a one-frame sprite of
    // MDKFONT.FTI (after its size) in the SYS_PAL colours, index 0 see-through, scaled by
    // whole steps to the window as the 600 x 360 view.
    struct MenuCursor
    {
        // An RGBA image of the cursor and its hotspot.
        struct Image
        {
            std::vector<byte> Rgba;
            int Width = 0;
            int Height = 0;
            int HotX = 0;
            int HotY = 0;

            bool operator==(const Image&) const = default;
        };

        // Sets the cursor of the window.
        static void Apply(Ui& ui);

        // The cursor at `scale` pixels per pixel, or null without it.
        static std::optional<Image> Build(const Formats::Fti& fti, int scale);

    private:
        static constexpr const char* Sprite = "ARROW";
        static constexpr int SpriteOffset = 4;
        static constexpr int Channels = 4;
        static constexpr byte Opaque = 255;
    };
}
