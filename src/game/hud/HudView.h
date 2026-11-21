#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/DotNet.h"
#include "core/Drawing.h"
#include "core/Numerics.h"
#include "game/hd_textures/HdImage.h"
#include "game/hud/BomberOverlay.h"

namespace Mdk::Engine::Render
{
    class Renderer;
}

namespace Mdk::Formats
{
    class Bni;
    class Fti;
    class Palette;
    class Texture;
}

namespace Mdk::Game::Mods
{
    class ModImages;
}

namespace Mdk::Game::Hud
{
    class Messages;
    class SniperOverlay;

    // An inventory slot the HUD shows: the item number and its count (C#: an (Item, Count) tuple).
    struct HudSlot
    {
        int Item = 0;
        int Count = 0;

        bool operator==(const HudSlot&) const = default;
    };

    // What the HUD shows this frame: Kurt's health and flashes (0-255 like the original's
    // counters), his inventory (item numbers, 1-based icons of PICKUPS) and the health bar of
    // the object he shoots at.
    struct HudState
    {
        int Health = 0;
        float HurtFlash = 0;
        float WhiteFlash = 0;
        bool Dead = false;
        // A view of the caller's slots (kept by it: no list per frame).
        std::span<const HudSlot> Slots;
        int Selected = 0;
        int SuperChainGun = 0;
        int BarHealth = 0;
        int BarMax = 0;
        std::optional<BomberSight> Bomber;
        float TimeLeft = 1.f;
    };

    // The in-game HUD, drawn like the original (0x41e128) on the 360-high canvas: the health
    // in the SC_STAT panel (bottom right, SNIP_TXT digits, blinking at 20 or less, 0x420830), the
    // inventory outside sniper mode (5 slots at the bottom left, 0x46cce4), the health bar of the
    // object Kurt shoots at (top left, 0x41e3c8), the red and white flashes, the skull when he's dead,
    // and the messages.
    //   ┌─bar─────────────────────────┐
    //   │          message            │
    //   │ [i][i][i][i][i]   [health]  │
    //   └─────────────────────────────┘
    class HudView
    {
    public:
        // The super chain gun's item number: its slot shows the ticks left, never selected.
        static constexpr int SuperChainGun = 6;

        std::shared_ptr<Hud::Messages> Messages;
        // Sniper mode's screen, which replaces the HUD while it shows.
        std::shared_ptr<SniperOverlay> Sniper;

        // The HUD; `mods`' images replace its own (CanvasImages).
        HudView(Engine::Render::Renderer& renderer, Formats::Bni& sprites, std::shared_ptr<Formats::Palette> palette,
            std::shared_ptr<Formats::Fti> fti, const std::shared_ptr<Mods::ModImages>& mods = nullptr);

        // The HUD's 2D images by their names in mods (CanvasImages; the exports).
        static std::vector<std::pair<std::string, std::shared_ptr<Formats::Texture>>> Images(Formats::Bni& sprites);

        // Kurt's arena's palette: the HUD's sprites take its colours (0x4709d8 sets them in the
        // table 0x581c28 they're drawn through), as the dog's icon needs (its 64-175 are magenta in the DTI).
        void Enter(std::shared_ptr<Formats::Palette> arena);

        // Once a tick: the blink.
        void Tick(const HudState& state);

        void Draw(const HudState& state);

    private:
        struct Image
        {
            int Texture = 0;
            Vector2 Size;
            int HotspotX = 0;
            int HotspotY = 0;
        };

        // Spaces of the fonts' missing characters.
        struct FontSpace
        {
            static constexpr int Big = 6;
            static constexpr int Small = 4;
        };

        static constexpr int BlinkMask = 31;
        static constexpr int BlinkOn = 16;
        static constexpr int LowHealth = 20;
        static constexpr int MaxNumber = 999;
        static constexpr int DigitWidth = 8;
        static constexpr float PanelRight = 16.f;
        static constexpr float PanelBottom = 10.f;
        // The health bar: 500 pixels for 900 hit points, y 4 to 10, colour 3 framed by 4.
        static constexpr float BarScale = 500.f / 900.f;
        static constexpr int BarWidth = 500;
        static constexpr float BarTop = 4.f;
        static constexpr float BarHeight = 7.f;
        static constexpr int BarFill = 3;
        static constexpr int BarFrame = 4;
        // Inventory slots: 48 apart, icons anchored at (32, 328), the selection box at (8, 304), 47 wide.
        static constexpr float SlotStep = 48.f;
        static constexpr Vector2 IconAnchor{32.f, 328.f};
        static constexpr RectangleF SelectionBox{8.f, 304.f, 47.f, 47.f};
        static constexpr float CountRise = 12.f;
        static constexpr float FlashMax = 255.f;
        static constexpr float HurtAlpha = 0.4f;
        static constexpr Vector4 SelectionFill{0.f, 0.f, 0.f, 0.5f};
        static constexpr Vector4 SelectionFrame{0.75f, 0.75f, 0.75f, 1.f};
        // The ring is stamped again once the time gone moved this far (0x420830: a degree).
        static constexpr float TimerStep = MathF::PI / 180.f;
        static constexpr const char* Panel = "SC_STAT";
        static constexpr const char* EmptyPanel = "SC_BSTAT";
        static constexpr const char* Skull = "SKULL";
        static constexpr const char* Digits = "SNIP_TXT";
        static constexpr const char* Pickups = "PICKUPS";

        Engine::Render::Renderer& _renderer;
        int _palette = 0;
        std::shared_ptr<Formats::Palette> _colours;
        std::shared_ptr<Formats::Palette> _arenaColours;
        std::shared_ptr<Mods::ModImages> _mods;
        Image _panel;
        Image _skull;
        Image _digits;
        std::vector<Image> _icons;
        Vector4 _barFill;
        Vector4 _barFrame;
        int _blink = 0;
        // The mission timer: the panel's indices with the time gone stamped in, up to this angle.
        std::shared_ptr<Formats::Texture> _fullPanel;
        std::shared_ptr<Formats::Texture> _emptyPanel;
        std::vector<byte> _timerPanel;
        float _timerAngle = 0;
        // A mod's panel (RGBA, any size): the stamped pixels take SC_BSTAT's colours over it.
        std::optional<HdTextures::HdImage> _modPanel;
        std::vector<byte> _modTimerPanel;
        std::shared_ptr<BomberOverlay> _bomber;

        Image Load(const std::string& name, const Formats::Texture& texture, int hotspotX, int hotspotY);

        static Vector4 Colour(const Formats::Palette& palette, int index);

        // Hits flash the screen red; once Kurt is dead the skull fades in instead. The nuke
        // flashes it white.
        void DrawFlashes(const HudState& state, float width, float height);

        // The mission timer's ring empties as the time runs out (0x420830): the time gone is
        // stamped in once it moved a degree. Less gone than stamped (a game loaded): the full ring again.
        void UpdateTimer(float timeLeft);

        // The stamped panel to the GPU, and over a mod's panel: each of its pixels shows the
        // panel pixel it covers, SC_BSTAT's colour where the ring was stamped.
        void UploadTimer();

        void DrawImage(const Image& image, Vector2 at, Vector4 tint);

        // A number (at most 999) centred on `at`.X, 8-pixel digits (0x420bd0).
        void DrawNumber(int value, Vector2 at);

        void DrawInventory(const HudState& state);

        void DrawBar(int health, int max);
    };
}
