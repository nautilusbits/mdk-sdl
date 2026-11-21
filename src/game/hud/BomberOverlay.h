#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/Numerics.h"
#include "formats/SpriteAnimation.h"

namespace Mdk::Engine::Render
{
    class Renderer;
}

namespace Mdk::Formats
{
    class Bni;
    class Palette;
    class Texture;
}

namespace Mdk::Game::Mods
{
    class ModImages;
}

namespace Mdk::Game::Hud
{
    class FontView;

    // What the bomber's HUD shows: the cursor (pixels of the 600×360 view) and the bombs left.
    struct BomberSight
    {
        Vector2 Cursor;
        int Bombs = 0;

        bool operator==(const BomberSight&) const = default;
    };

    // The XE bomber's HUD (0x46be98; bomber_overlay.gd), while the controls are unlocked:
    // BOMBTARG at the view's centre, CROSS at the cursor and the bombs left right-aligned at x 472 on
    // the baseline y 56, in the big font. The 600×360 view is centred on the canvas.
    class BomberOverlay
    {
    public:
        // The bomber's HUD; `mods`' images (through `colours`) replace its own.
        BomberOverlay(Engine::Render::Renderer& renderer, Formats::Bni& sprites, int palette, std::shared_ptr<FontView> font,
            const Formats::Palette* colours = nullptr, const std::shared_ptr<Mods::ModImages>& mods = nullptr);

        // The HUD's 2D images by their names in mods (CanvasImages; the exports).
        static std::vector<std::pair<std::string, std::shared_ptr<Formats::Texture>>> Images(Formats::Bni& sprites);

        void Draw(float canvasWidth, BomberSight sight);

    private:
        struct Image
        {
            int Texture = 0;
            Vector2 Size;
            Vector2 Hotspot;
        };

        static constexpr const char* Target = "BOMBTARG";
        static constexpr const char* Cross = "CROSS";
        static constexpr Vector2 ViewCentre{300.f, 180.f};
        static constexpr float CountRight = 472.f;
        static constexpr float CountBaseline = 56.f;

        Engine::Render::Renderer& _renderer;
        int _palette = 0;
        std::shared_ptr<FontView> _font;
        Image _target;
        Image _cross;

        Image Load(const std::string& animation, const Formats::SpriteAnimation::Frame& frame, const Formats::Palette* colours,
            const std::shared_ptr<Mods::ModImages>& mods);

        // An image with its hotspot at `at`.
        void DrawImage(const Image& image, Vector2 at);
    };
}
