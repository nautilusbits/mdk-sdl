#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "core/Drawing.h"
#include "core/Numerics.h"
#include "formats/SniperScreen.h"
#include "game/scripts/SniperRounds.h"

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
    // Whether Bones' air strike has a target in the crosshair (0x4641ac), checked only with
    // the strike selected and the clip ready.
    enum class StrikeAim { NotChecked, None, Target };

    // Whether the health's digits show (they blink when it's low).
    enum class Blink { Shown, Hidden };

    // What the sniper screen shows this frame.
    struct SniperHud
    {
        float Zoom = 0;
        std::vector<int> Ammo;
        int SelectedAmmo = 0;
        std::vector<Scripts::SniperRounds::RoundCamera> Cameras;
        StrikeAim Aim{};

        bool operator==(const SniperHud&) const = default;
    };

    // Sniper mode's screen (HUD 0x41e128, 0x420830; TRAVSPRT.BNI; a port of godot-mdk's
    // sniper_overlay.gd), drawn over the whole 640x480 screen (scaled to the 360-high canvas, centred,
    // black beside it): the SNIPERS1 frame around the 600x360 view at (20, 60), the round cameras'
    // fills, the air strike's iris, the SNIPERS2 mask, the CROSS crosshair, the zoom in percent with its
    // SNIP_RNG gauge, the ammo types and the health. The round cameras' and the loaded rounds' 3D views
    // are drawn by the game as renderer insets (RoundViews, View).
    //   ┌────────── SNIPERS1 ───────────┐
    //   │ [cam] [cam] [cam]             │   view (20, 60) 600x360
    //   │      ┌─ scope ─┐   zoom %     │
    //   │      │    +    │   gauge      │
    //   │ ammo └─────────┘     health   │
    //   └───────────────────────────────┘
    class SniperOverlay
    {
    public:
        static constexpr float ScreenWidth = Formats::SniperScreen::ScreenWidth;
        static constexpr float ScreenHeight = Formats::SniperScreen::ScreenHeight;
        // Where the 600x360 view sits on the screen.
        static constexpr Vector2 View{20.f, 60.f};
        // The round cameras' windows in the view (0x461d80).
        static constexpr std::array<RectangleF, 3> RoundViews{
            RectangleF(72.f, 10.f, 140.f, 70.f), RectangleF(228.f, 0.f, 140.f, 70.f), RectangleF(384.f, 10.f, 140.f, 70.f)};

        // Sniper mode's screen; `mods`' images replace its own (CanvasImages).
        SniperOverlay(Engine::Render::Renderer& renderer, Formats::Bni& sprites, std::shared_ptr<Formats::Palette> palette,
            int paletteTexture, const std::shared_ptr<Mods::ModImages>& mods = nullptr);

        // The screen's 2D images by their names in mods (CanvasImages; the exports).
        static std::vector<std::pair<std::string, std::shared_ptr<Formats::Texture>>> Images(Formats::Bni& sprites);

        // Whether sniper mode's screen replaces the HUD.
        bool Visible() const { return _shown.has_value(); }

        // Each frame: what to show (null out of sniper mode), the iris and the gauge.
        void Update(float delta, const std::optional<SniperHud>& hud);

        // The screen's scale and left edge on the canvas.
        static std::tuple<float, float> Placement(float canvasWidth);

        // Draws the sniper screen, and the health centred in the panel at `panel` (pixels of the
        // view) of `panelSize`.
        void Draw(float canvasWidth, int health, Blink blink, Vector2 panel, Vector2 panelSize);

    private:
        struct Image
        {
            int Texture = 0;
            Vector2 Size;
            Vector2 Hotspot;
        };

        // Draws in 640x480 screen pixels on the canvas.
        struct Screen
        {
            Engine::Render::Renderer* Renderer;
            int Palette;
            float Scale;
            float Left;

            void Fill(RectangleF r, Vector4 colour) const;
            void Image(const SniperOverlay::Image& image, Vector2 at) const;
            void Part(const SniperOverlay::Image& image, RectangleF source, Vector2 at) const;

        private:
            RectangleF ToCanvas(RectangleF r) const { return RectangleF(Left + r.X * Scale, r.Y * Scale, r.Width * Scale, r.Height * Scale); }
        };

        static constexpr Vector2 Crosshair{299.f, 219.f};
        static constexpr Vector2 ZoomDigits{564.f, 155.f};
        static constexpr Vector2 Gauge{552.f, 176.f};
        static constexpr int GaugeHeight = 88;
        // The gauge's shown height follows the zoom by 3 pixels per frame.
        static constexpr int GaugeSpeed = 3;
        // The zoom in percent: (1 - zoom)² x 1.05194 (59% at 4x).
        static constexpr float ZoomPercentFactor = 1.05194f;
        static constexpr float Percent = 100.f;
        static constexpr Vector2 Weapon{112.f, 304.f};
        static constexpr std::array<Vector2, 6> TypeIcons{Vector2(0, 256), Vector2(0, 280), Vector2(0, 300), Vector2(4, 320),
            Vector2(16, 336), Vector2(32, 344)};
        static constexpr std::array<Vector2, 6> TypeLabels{Vector2(12, 268), Vector2(12, 288), Vector2(12, 308), Vector2(20, 320),
            Vector2(24, 328), Vector2(36, 336)};
        static constexpr Vector2 Count{64.f, 315.f};
        static constexpr int Types = 6;
        // Round camera fills: after a hit, after a kill or an explosion; a miss shows SNIPERGA,
        // a frame every 2 ticks.
        static constexpr int HitFill = 0x3c;
        static constexpr int KillFill = 0xf4;
        static constexpr int EmptyFill = 0;
        static constexpr float MissFrameTicks = 2.f;
        // The air strike's iris (0x41ef90): with a target the scope reddens from its edge in 1 s, leaving
        // a hole of radius 384 x c² (c from 1 to 0); closed, a darker ring contracts every second. View
        // rows 80-359, x 108-492, centred on (300, 220).
        static constexpr int StrikeType = 5;
        static constexpr Vector2 IrisCentre{300.f, 220.f};
        static constexpr int IrisTop = 80;
        static constexpr int IrisBottom = 360;
        static constexpr float IrisHalfWidth = 192.f;
        static constexpr float IrisRadius = 384.f;
        static constexpr float RingOuter = 392.f;
        static constexpr float RingInner = 376.f;
        static constexpr Vector4 IrisColour{200.f / 255.f, 0.f, 0.f, 0x60 / 255.f};
        static constexpr Vector4 RingColour{200.f / 255.f, 0.f, 0.f, 0xC4 / 255.f};
        static constexpr int DigitWidth = 8;
        static constexpr int MaxNumber = 999;
        static constexpr Vector4 Black{0.f, 0.f, 0.f, 1.f};
        // The images' names in TRAVSPRT.BNI (the frame and mask: SniperScreen's).
        static constexpr const char* FrameImage = "SNIPERS1";
        static constexpr const char* MaskImage = "SNIPERS2";
        static constexpr const char* Cross = "CROSS";
        static constexpr const char* GaugeImage = "SNIP_RNG";
        static constexpr const char* WeaponImage = "SNIP_WEP";
        static constexpr const char* Digits = "SNIP_TXT";
        static constexpr const char* Icon = "SNIP_W";
        static constexpr const char* Label = "SNIP_L";
        static constexpr const char* Miss = "SNIPERGA";

        Engine::Render::Renderer& _renderer;
        int _palette = 0;
        std::shared_ptr<Formats::Palette> _colours;
        std::shared_ptr<Mods::ModImages> _mods;
        Image _frame;
        Image _mask;
        Image _cross;
        Image _gauge;
        Image _weapon;
        Image _digits;
        std::vector<Image> _icons;
        std::vector<Image> _labels;
        std::vector<Image> _miss;
        std::optional<SniperHud> _shown;
        int _gaugeShown = 0;
        // The iris: its opening (1 open, 0 closed), the pulse, and whether the strike has a target.
        float _iris = 1.f;
        float _pulse = 1.f;
        bool _target = false;

        Image Load(const std::string& name, const Formats::Texture& texture, Vector2 hotspot);

        // The frame without the view's rectangle, but for its edge under the mask: filtered (the
        // enhanced look), the frame's and the mask's edges both fade there and would let the scene through
        // as a light line.
        static std::shared_ptr<Formats::Texture> CutView(const Formats::Texture& frame, const Formats::Texture& mask);

        static float ZoomFraction(float zoom);

        // The iris closes over 1 s while the strike is selected and has a target, and opens
        // again otherwise; closed, it pulses once a second.
        void UpdateIris(float delta, const SniperHud& hud);

        // Black behind the frame (index 0 is transparent), then the frame.
        void DrawFrame(const Screen& screen);

        // Round cameras that don't watch a round: a colour, or the miss animation.
        void DrawCameras(const Screen& screen, const SniperHud& hud);

        Vector4 Colour(int index) const;

        // The iris, row by row from the scope's edges inwards: red, the darker pulse ring, red
        // again, then the clear hole.
        void DrawIris(const Screen& screen);

        // A row of the iris between two distances from its centre, on both sides.
        static void DrawSpan(const Screen& screen, int y, float near, float far, Vector4 colour);

        // SNIP_WEP, the icons of the types with rounds, the selected type's label and count.
        void DrawAmmo(const Screen& screen, const SniperHud& hud);

        // A number (at most 999) centred on `at`.X, 8-pixel digits.
        void DrawNumber(const Screen& screen, int value, Vector2 at);
    };
}
