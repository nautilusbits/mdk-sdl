#pragma once

#include <memory>
#include <optional>

#include "Mdk.Core/Drawing.h"
#include "Mdk.Core/Numerics.h"

namespace Mdk::Formats
{
    class Palette;
}

namespace Mdk::Game::Hud
{
    class FontView;
}

namespace Mdk::Game::Menu
{
    class PalettedImage;
    class Ui;

    // The loading screen (level_load 0x41b0c0, drawn by 0x422a10) on the 600 x 360 screen:
    // MISC/LOAD_n.LBB (768-byte palette, u16 width, height, pixels; 200 x 200) at (200, 25),
    // LOAD_MSG ("Loading") centred at y 260 and a progress bar from x 10 to 590, y 290 to 310
    // (colour 3, framed by 4).
    class LoadingScreen
    {
    public:
        // The loading screen of LEVELn, made once: it's redrawn as the level loads.
        LoadingScreen(Ui& ui, int level);

        // Draws and shows one frame with the bar `progress` (0 to 1) full.
        void Show(float progress);

    private:
        // The image, the font and the bar's colours.
        struct Look
        {
            std::shared_ptr<PalettedImage> Image;
            std::shared_ptr<Hud::FontView> Font;
            Vector4 Fill;
            Vector4 Frame;
        };

        static constexpr Vector2 ImagePosition{200.f, 25.f};
        static constexpr float TextY = 260.f;
        static constexpr RectangleF Bar{10.f, 290.f, 580.f, 20.f};
        static constexpr int PaletteSize = 768;
        static constexpr int BarFill = 3;
        static constexpr int BarFrame = 4;

        Ui& _ui;
        // Null without the screen's file.
        std::optional<Look> _look;

        static Vector4 Colour(const Formats::Palette& palette, int index);
    };
}
