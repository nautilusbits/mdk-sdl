#include "game/hud/HudView.h"

#include <algorithm>
#include <limits>

#include "engine/render/Renderer.h"
#include "formats/Bni.h"
#include "formats/Font.h"
#include "formats/Fti.h"
#include "formats/Palette.h"
#include "formats/SniperScreen.h"
#include "formats/SpriteAnimation.h"
#include "formats/Texture.h"
#include "game/hud/FontView.h"
#include "game/hud/Messages.h"
#include "game/hud/SniperOverlay.h"
#include "game/hud/TimerRing.h"
#include "game/mods/CanvasImages.h"
#include "game/mods/ModImages.h"

namespace Mdk::Game::Hud
{
    using Engine::Render::Renderer;

    HudView::HudView(Renderer& renderer, Formats::Bni& sprites, std::shared_ptr<Formats::Palette> palette,
        std::shared_ptr<Formats::Fti> fti, const std::shared_ptr<Mods::ModImages>& mods)
        : _renderer(renderer)
    {
        _palette = renderer.CreatePalette(palette->Rgba);
        _colours = palette;
        _mods = mods;
        _panel = Load(Panel, *sprites.GetImage(Panel), 0, 0);
        _fullPanel = sprites.GetImage(Panel);
        _emptyPanel = sprites.GetImage(EmptyPanel);
        _timerPanel = _fullPanel->Indices;
        _modPanel = mods ? mods->Image(Panel, *_fullPanel, *palette) : std::nullopt;
        _modTimerPanel = _modPanel ? _modPanel->Rgba : std::vector<byte>{};
        _skull = Load(Skull, *sprites.GetImage(Skull), 0, 0);
        _digits = Load(Digits, *sprites.GetImage(Digits), 0, 0);
        auto pickups = sprites.GetAnimation(Pickups);
        for (auto i = 0; i < pickups->FrameCount(); i++)
        {
            auto frame = pickups->GetFrame(i);
            _icons.push_back(Load(Mods::CanvasImages::FrameName(Pickups, i), *frame.Image, frame.HotspotX, frame.HotspotY));
        }

        _barFill = Colour(*palette, BarFill);
        _barFrame = Colour(*palette, BarFrame);

        // The fonts use the interface's colours (SYS_PAL, the first 64).
        auto system = Formats::Palette::FromRgb(fti->GetBytes("SYS_PAL"));
        auto big = std::make_shared<FontView>(renderer, Formats::Font::Parse(fti->GetBytes(FontView::Big), FontSpace::Big), *system, mods, FontView::Big);
        auto small = std::make_shared<FontView>(renderer, Formats::Font::Parse(fti->GetBytes(FontView::Small), FontSpace::Small), *system, mods, FontView::Small);
        Messages = std::make_shared<Hud::Messages>(fti, big, small);
        Sniper = std::make_shared<SniperOverlay>(renderer, sprites, palette, _palette, mods);
        _bomber = std::make_shared<BomberOverlay>(renderer, sprites, _palette, big, palette.get(), mods);
    }

    HudView::Image HudView::Load(const std::string& name, const Formats::Texture& texture, int hotspotX, int hotspotY)
    {
        return {Mods::CanvasImages::Create(_renderer, _mods, name, texture, *_colours, _palette),
            Vector2(static_cast<float>(texture.Width), static_cast<float>(texture.Height)), hotspotX, hotspotY};
    }

    std::vector<std::pair<std::string, std::shared_ptr<Formats::Texture>>> HudView::Images(Formats::Bni& sprites)
    {
        std::vector<std::pair<std::string, std::shared_ptr<Formats::Texture>>> images;
        for (const auto* name : {Panel, Skull, Digits})
        {
            images.emplace_back(name, sprites.GetImage(name));
        }

        auto pickups = sprites.GetAnimation(Pickups);
        for (auto i = 0; i < pickups->FrameCount(); i++)
        {
            images.emplace_back(Mods::CanvasImages::FrameName(Pickups, i), pickups->GetFrame(i).Image);
        }

        return images;
    }

    Vector4 HudView::Colour(const Formats::Palette& palette, int index)
    {
        constexpr float Max = std::numeric_limits<byte>::max();
        return Vector4(palette.Rgba[index * 4], palette.Rgba[index * 4 + 1], palette.Rgba[index * 4 + 2], Max) / Max;
    }

    void HudView::Enter(std::shared_ptr<Formats::Palette> arena)
    {
        if (arena == _arenaColours)
        {
            return;
        }

        _arenaColours = arena;
        _renderer.UpdateTexture(_palette, Formats::Palette::Size, 1, arena->Rgba);
    }

    void HudView::Tick(const HudState& state)
    {
        _blink = (_blink + 1) & BlinkMask;
    }

    void HudView::Draw(const HudState& state)
    {
        auto width = _renderer.CanvasWidth();
        auto height = Renderer::CanvasHeight;
        DrawFlashes(state, width, height);
        if (Sniper->Visible())
        {
            auto blink = state.Health > LowHealth || _blink < BlinkOn ? Blink::Shown : Blink::Hidden;
            auto panelAt = Vector2(Formats::SniperScreen::ViewWidth - (_panel.Size.X + PanelRight), Formats::SniperScreen::ViewHeight - (_panel.Size.Y + PanelBottom));
            Sniper->Draw(width, state.Health, blink, panelAt, _panel.Size);
            Messages->Draw(width);
            return;
        }

        // The bomber's sight first (0x41e3c8).
        if (state.Bomber)
        {
            _bomber->Draw(width, *state.Bomber);
        }

        // Health panel at the bottom right; the number blinks when health is low.
        auto panel = Vector2(width - (_panel.Size.X + PanelRight), height - (_panel.Size.Y + PanelBottom));
        UpdateTimer(state.TimeLeft);
        DrawImage(_panel, panel, Vector4::One());
        if (state.Health > LowHealth || _blink < BlinkOn)
        {
            auto centre = panel + Vector2(static_cast<float>(ToInt(_panel.Size.X) >> 1), static_cast<float>((ToInt(_panel.Size.Y) - ToInt(_digits.Size.Y)) >> 1));
            DrawNumber(state.Health, centre);
        }

        // Always shown: 0x46cce4 resets its timer (0x574328) to 60 every frame outside sniper mode,
        // so the super chain gun's ticks count down on screen while Kurt fires.
        DrawInventory(state);

        DrawBar(state.BarHealth, state.BarMax);
        Messages->Draw(width);
    }

    void HudView::DrawFlashes(const HudState& state, float width, float height)
    {
        if (state.Dead)
        {
            auto alpha = Math::Clamp(state.HurtFlash / FlashMax, 0.f, 1.f);
            DrawImage(_skull, Vector2(width, height) / 2.f - _skull.Size / 2.f, Vector4(1.f, 1.f, 1.f, alpha));
        }
        else if (state.HurtFlash > 0.f)
        {
            _renderer.FillRect(RectangleF(0.f, 0.f, width, height), Vector4(1.f, 0.f, 0.f, state.HurtFlash / FlashMax * HurtAlpha));
        }

        if (state.WhiteFlash > 0.f)
        {
            _renderer.FillRect(RectangleF(0.f, 0.f, width, height), Vector4(1.f, 1.f, 1.f, MathF::Min(state.WhiteFlash / FlashMax, 1.f)));
        }
    }

    void HudView::UpdateTimer(float timeLeft)
    {
        auto angle = (1.f - timeLeft) * MathF::Tau;
        if (angle < _timerAngle)
        {
            std::copy(_fullPanel->Indices.begin(), _fullPanel->Indices.end(), _timerPanel.begin());
            _timerAngle = 0.f;
            UploadTimer();
        }

        if (angle <= _timerAngle + TimerStep)
        {
            return;
        }

        TimerRing::Wipe(_timerPanel, *_emptyPanel, _timerAngle, angle);
        _timerAngle = angle;
        UploadTimer();
    }

    void HudView::UploadTimer()
    {
        auto width = _fullPanel->Width;
        auto height = _fullPanel->Height;
        _renderer.UpdateTexture(_panel.Texture, width, height, _timerPanel);
        if (!_modPanel)
        {
            return;
        }

        const auto& mod = *_modPanel;
        const auto& colours = (_arenaColours ? _arenaColours : _colours)->Rgba;
        for (auto y = 0; y < mod.Height; y++)
        {
            auto row = y * height / mod.Height * width;
            for (auto x = 0; x < mod.Width; x++)
            {
                auto at = row + x * width / mod.Width;
                auto pixel = (y * mod.Width + x) * 4;
                if (_timerPanel[at] == _fullPanel->Indices[at])
                {
                    std::copy_n(mod.Rgba.begin() + pixel, 4, _modTimerPanel.begin() + pixel);
                    continue;
                }

                std::copy_n(colours.begin() + _timerPanel[at] * 4, 4, _modTimerPanel.begin() + pixel);
            }
        }

        _renderer.ReplaceImage(_panel.Texture, _palette, mod.Width, mod.Height, _modTimerPanel);
    }

    void HudView::DrawImage(const Image& image, Vector2 at, Vector4 tint)
    {
        _renderer.DrawImage(image.Texture, _palette, image.Size, RectangleF(0.f, 0.f, image.Size.X, image.Size.Y),
            RectangleF(at.X, at.Y, image.Size.X, image.Size.Y), tint);
    }

    void HudView::DrawNumber(int value, Vector2 at)
    {
        auto text = std::to_string(std::min(value, MaxNumber));
        auto x = at.X - DigitWidth / 2.f * static_cast<float>(text.size());
        for (auto c : text)
        {
            auto digit = c - '0';
            auto source = RectangleF(static_cast<float>(digit * DigitWidth), 0.f, static_cast<float>(DigitWidth), _digits.Size.Y);
            _renderer.DrawImage(_digits.Texture, _palette, _digits.Size, source, RectangleF(x, at.Y, static_cast<float>(DigitWidth), _digits.Size.Y),
                Vector4::One());
            x += DigitWidth;
        }
    }

    void HudView::DrawInventory(const HudState& state)
    {
        auto slots = static_cast<int>(state.Slots.size());
        if (state.Selected < slots && state.Slots[state.Selected].Item != SuperChainGun)
        {
            auto box = SelectionBox;
            box.Offset(state.Selected * SlotStep, 0.f);
            _renderer.FillRect(RectangleF::Inflate(box, -1.f, -1.f), SelectionFill);
            _renderer.FrameRect(box, 1.f, SelectionFrame);
        }

        for (auto i = 0; i < slots; i++)
        {
            auto [item, count] = state.Slots[i];
            auto frame = item - 1;
            if (frame < 0 || frame >= static_cast<int>(_icons.size()))
            {
                continue;
            }

            const auto& icon = _icons[frame];
            auto anchor = IconAnchor + Vector2(i * SlotStep, 0.f);
            DrawImage(icon, anchor - Vector2(static_cast<float>(icon.HotspotX), static_cast<float>(icon.HotspotY)), Vector4::One());
            auto shown = item == SuperChainGun ? state.SuperChainGun : count;
            if (shown > 1)
            {
                DrawNumber(shown, anchor - Vector2(0.f, CountRise));
            }
        }
    }

    void HudView::DrawBar(int health, int max)
    {
        if (max <= 0)
        {
            return;
        }

        auto fill = Math::Clamp(MathF::Round(health * BarScale), 0.f, static_cast<float>(BarWidth));
        auto frame = Math::Clamp(MathF::Round(max * BarScale), 0.f, static_cast<float>(BarWidth));
        _renderer.FillRect(RectangleF(0.f, BarTop, fill + 1.f, BarHeight), _barFill);
        _renderer.FrameRect(RectangleF(0.f, BarTop, frame + 1.f, BarHeight), 1.f, _barFrame);
    }
}
