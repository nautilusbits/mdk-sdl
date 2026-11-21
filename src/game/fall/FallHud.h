#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/Numerics.h"
#include "engine/render/Renderer.h"
#include "formats/Bni.h"
#include "formats/Fti.h"
#include "formats/Palette.h"
#include "formats/Texture.h"
#include "game/kurt/Inventory.h"

namespace Mdk::Game::Hud
{
    class Messages;
}

namespace Mdk::Game::Mods
{
    class ModImages;
}

namespace Mdk::Game::Fall
{
    // The HUD over the fall, as in a level (0x420830, 0x46cce4) but from FALL3D.BNI's
    // copies of the images: the messages, the health panel (blinking at 20 or less), the inventory
    // (always: 0x46cce4 resets its timer every frame outside sniper mode), and the skull growing while Kurt dies.
    //   ┌─────────────────────────────┐
    //   │          message            │
    //   │          (skull)            │
    //   │ [i][i][i][i][i]   [health]  │
    //   └─────────────────────────────┘
    class FallHud
    {
    public:
        enum class Life { Alive, Dying };

        std::shared_ptr<Hud::Messages> Messages;

        // The fall's HUD; mods' images (through colours, the palette's colours before its
        // effects) replace its own, named as the level's HUD's.
        FallHud(Engine::Render::Renderer& renderer, Formats::Bni& bni, int palette, std::shared_ptr<Formats::Fti> fti,
            std::shared_ptr<Formats::Palette> colours = nullptr, std::shared_ptr<Mods::ModImages> mods = nullptr);

        // Some ticks passed: the blink.
        void Tick(const Kurt::Inventory& inventory, int ticks);

        // Draws it; red (0-1) sizes the skull while Kurt dies.
        void Draw(int health, const Kurt::Inventory& inventory, Life life, float red);

    private:
        static constexpr int BlinkMask = 31;
        static constexpr int BlinkOn = 16;
        static constexpr int LowHealth = 20;
        static constexpr int MaxNumber = 999;
        static constexpr int DigitWidth = 8;
        static constexpr float PanelRight = 16.f;
        static constexpr float PanelBottom = 10.f;
        static constexpr float SlotStep = 48.f;
        static Vector2 IconAnchor() { return Vector2(32.f, 328.f); }
        static constexpr float CountRise = 12.f;
        static constexpr float ViewCentreY = 180.f;
        // The skull grows with the red: full size at 256/255.
        static constexpr float SkullGrowth = 255.f / 256.f;
        static constexpr int FontSpaceBig = 6;
        static constexpr int FontSpaceSmall = 4;
        // The panel, the skull and the digits come before the icons in HudView.Images.
        static constexpr int HudFirstIcon = 3;

        struct Image
        {
            int Texture = 0;
            Vector2 Size{};
            int HotspotX = 0;
            int HotspotY = 0;
        };

        Engine::Render::Renderer& _renderer;
        int _palette;
        std::shared_ptr<Formats::Palette> _colours;
        std::shared_ptr<Mods::ModImages> _mods;
        Image _panel;
        Image _skull;
        Image _digits;
        std::vector<Image> _icons;
        int _blink = 0;

        Image Load(const std::string& name, const std::shared_ptr<Formats::Texture>& texture, int hotspotX, int hotspotY);

        void DrawInventory(const Kurt::Inventory& inventory);

        void DrawImage(const Image& image, Vector2 at, Vector2 size);

        // A number (at most 999) centred on at.X, 8-pixel digits (0x420bd0).
        void DrawNumber(int value, Vector2 at);
    };
}
