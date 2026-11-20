#include "Mdk.Game/Hud/BomberOverlay.h"

#include <span>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Drawing.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/Texture.h"
#include "Mdk.Game/Hud/FontView.h"
#include "Mdk.Game/Mods/CanvasImages.h"

namespace Mdk::Game::Hud
{
    BomberOverlay::BomberOverlay(Engine::Render::Renderer& renderer, Formats::Bni& sprites, int palette, std::shared_ptr<FontView> font,
        const Formats::Palette* colours, const std::shared_ptr<Mods::ModImages>& mods)
        : _renderer(renderer), _palette(palette), _font(std::move(font))
    {
        _target = Load(Target, sprites.GetAnimation(Target)->GetFrame(0), colours, mods);
        _cross = Load(Cross, sprites.GetAnimation(Cross)->GetFrame(0), colours, mods);
    }

    std::vector<std::pair<std::string, std::shared_ptr<Formats::Texture>>> BomberOverlay::Images(Formats::Bni& sprites)
    {
        std::vector<std::pair<std::string, std::shared_ptr<Formats::Texture>>> images;
        for (const auto* a : {Target, Cross})
        {
            images.emplace_back(Mods::CanvasImages::FrameName(a, 0), sprites.GetAnimation(a)->GetFrame(0).Image);
        }

        return images;
    }

    BomberOverlay::Image BomberOverlay::Load(const std::string& animation, const Formats::SpriteAnimation::Frame& frame,
        const Formats::Palette* colours, const std::shared_ptr<Mods::ModImages>& mods)
    {
        const auto& image = *frame.Image;
        auto texture = _renderer.CreateIndexTexture(image.Width, image.Height, image.Indices);
        if (colours != nullptr)
        {
            Mods::CanvasImages::Replace(_renderer, mods, Mods::CanvasImages::FrameName(animation, 0), image, *colours, texture, _palette);
        }

        return {texture, Vector2(static_cast<float>(image.Width), static_cast<float>(image.Height)),
            Vector2(static_cast<float>(frame.HotspotX), static_cast<float>(frame.HotspotY))};
    }

    void BomberOverlay::Draw(float canvasWidth, BomberSight sight)
    {
        auto origin = Vector2(canvasWidth / 2.f - ViewCentre.X, 0.f);
        DrawImage(_target, origin + ViewCentre);
        DrawImage(_cross, origin + Vector2(MathF::Round(sight.Cursor.X), MathF::Round(sight.Cursor.Y)));
        auto digits = std::to_string(sight.Bombs);
        auto text = std::span<const byte>(reinterpret_cast<const byte*>(digits.data()), digits.size());
        _font->Draw(text, origin.X + CountRight - _font->Width(text), origin.Y + CountBaseline);
    }

    void BomberOverlay::DrawImage(const Image& image, Vector2 at)
    {
        auto corner = at - image.Hotspot;
        _renderer.DrawImage(image.Texture, _palette, image.Size, RectangleF(0.f, 0.f, image.Size.X, image.Size.Y),
            RectangleF(corner.X, corner.Y, image.Size.X, image.Size.Y), Vector4::One());
    }
}
