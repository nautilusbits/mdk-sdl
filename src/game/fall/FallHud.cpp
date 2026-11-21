#include "game/fall/FallHud.h"

#include <string>

#include "formats/Font.h"
#include "formats/SpriteAnimation.h"
#include "game/hud/FontView.h"
#include "game/hud/HudView.h"
#include "game/hud/Messages.h"
#include "game/mods/CanvasImages.h"

namespace Mdk::Game::Fall
{
    FallHud::FallHud(Engine::Render::Renderer& renderer, Formats::Bni& bni, int palette, std::shared_ptr<Formats::Fti> fti,
        std::shared_ptr<Formats::Palette> colours, std::shared_ptr<Mods::ModImages> mods)
        : _renderer(renderer), _palette(palette), _colours(std::move(colours)), _mods(std::move(mods))
    {
        auto images = Hud::HudView::Images(bni);
        _panel = Load(images[0].first, images[0].second, 0, 0);
        _skull = Load(images[1].first, images[1].second, 0, 0);
        _digits = Load(images[2].first, images[2].second, 0, 0);
        auto pickups = bni.GetAnimation("PICKUPS");
        for (auto i = 0; i < pickups->FrameCount(); i++)
        {
            auto frame = pickups->GetFrame(i);
            const auto& image = images[HudFirstIcon + i];
            _icons.push_back(Load(image.first, image.second, frame.HotspotX, frame.HotspotY));
        }

        // The fonts use the fall's palette and its effects (fall.gd).
        auto big = std::make_shared<Hud::FontView>(renderer, Formats::Font::Parse(fti->GetBytes("FONTBIG"), FontSpaceBig), palette);
        auto small = std::make_shared<Hud::FontView>(renderer, Formats::Font::Parse(fti->GetBytes("FONTSML"), FontSpaceSmall), palette);
        Messages = std::make_shared<Hud::Messages>(fti, big, small);
    }

    FallHud::Image FallHud::Load(const std::string& name, const std::shared_ptr<Formats::Texture>& texture, int hotspotX, int hotspotY)
    {
        auto id = _renderer.CreateIndexTexture(texture->Width, texture->Height, texture->Indices);
        if (_colours)
        {
            Mods::CanvasImages::Replace(_renderer, _mods, name, *texture, *_colours, id, _palette);
        }

        return Image{ id, Vector2(static_cast<float>(texture->Width), static_cast<float>(texture->Height)), hotspotX, hotspotY };
    }

    // Some ticks passed: the blink.
    void FallHud::Tick(const Kurt::Inventory& inventory, int ticks)
    {
        _blink = (_blink + ticks) & BlinkMask;
    }

    // Draws it; red (0-1) sizes the skull while Kurt dies.
    void FallHud::Draw(int health, const Kurt::Inventory& inventory, Life life, float red)
    {
        auto width = _renderer.CanvasWidth();
        if (life == Life::Dying)
        {
            auto size = _skull.Size * MathF::Min(red * SkullGrowth, 1.f);
            DrawImage(_skull, Vector2(width / 2.f, ViewCentreY) - size / 2.f, size);
        }

        Messages->Draw(width);

        // The health panel at the bottom right; the number blinks when health is low.
        auto panel = Vector2(width - (_panel.Size.X + PanelRight), Engine::Render::Renderer::CanvasHeight - (_panel.Size.Y + PanelBottom));
        DrawImage(_panel, panel, _panel.Size);
        if (health > LowHealth || _blink < BlinkOn)
        {
            auto offset = Vector2(static_cast<float>(ToInt(_panel.Size.X) >> 1), static_cast<float>((ToInt(_panel.Size.Y) - ToInt(_digits.Size.Y)) >> 1));
            DrawNumber(health, panel + offset);
        }

        DrawInventory(inventory);
    }

    void FallHud::DrawInventory(const Kurt::Inventory& inventory)
    {
        const auto& slots = inventory.Slots();
        for (auto i = 0; i < static_cast<int>(slots.size()); i++)
        {
            auto item = slots[i].Item;
            auto count = slots[i].Count;
            auto frame = static_cast<int>(item) - 1;
            if (frame < 0 || frame >= static_cast<int>(_icons.size()))
            {
                continue;
            }

            const auto& icon = _icons[frame];
            auto anchor = IconAnchor() + Vector2(i * SlotStep, 0.f);
            DrawImage(icon, anchor - Vector2(static_cast<float>(icon.HotspotX), static_cast<float>(icon.HotspotY)), icon.Size);
            auto shown = item == Kurt::Inventory::Item::SuperChainGun ? inventory.SuperChainGun : count;
            if (shown > 1)
            {
                DrawNumber(shown, anchor - Vector2(0.f, CountRise));
            }
        }
    }

    void FallHud::DrawImage(const Image& image, Vector2 at, Vector2 size)
    {
        _renderer.DrawImage(image.Texture, _palette, image.Size, RectangleF(0.f, 0.f, image.Size.X, image.Size.Y),
            RectangleF(at.X, at.Y, size.X, size.Y), Vector4::One());
    }

    // A number (at most 999) centred on at.X, 8-pixel digits (0x420bd0).
    void FallHud::DrawNumber(int value, Vector2 at)
    {
        auto text = std::to_string(Math::Min(value, MaxNumber));
        auto x = at.X - DigitWidth / 2.f * static_cast<int>(text.size());
        for (auto c : text)
        {
            auto source = RectangleF(static_cast<float>((c - '0') * DigitWidth), 0.f, static_cast<float>(DigitWidth), _digits.Size.Y);
            _renderer.DrawImage(_digits.Texture, _palette, _digits.Size, source, RectangleF(x, at.Y, static_cast<float>(DigitWidth), _digits.Size.Y), Vector4::One());
            x += DigitWidth;
        }
    }
}
