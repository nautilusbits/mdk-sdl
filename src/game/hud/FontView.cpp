#include "game/hud/FontView.h"

#include <algorithm>

#include "core/Drawing.h"
#include "engine/render/Renderer.h"
#include "formats/Font.h"
#include "formats/Palette.h"
#include "formats/Texture.h"
#include "game/mods/CanvasImages.h"

namespace Mdk::Game::Hud
{
    namespace
    {
        // glyphs.Max(g => g?.Ascent ?? 0).
        int MaxAscent(const Formats::Font& font)
        {
            auto max = 0;
            for (const auto& g : font.Glyphs)
            {
                max = std::max(max, g ? g->Ascent : 0);
            }

            return max;
        }
    }

    FontView::FontView(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Font> font, const Formats::Palette& palette,
        const std::shared_ptr<Mods::ModImages>& mods, const std::string& name)
        : FontView(renderer, font, renderer.CreatePalette(palette.Rgba))
    {
        Mods::CanvasImages::Replace(renderer, mods, name, *Atlas(*font, name), palette, _texture, _palette);
    }

    FontView::FontView(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Font> font, int palette)
        : _renderer(renderer), _font(std::move(font))
    {
        auto atlas = Atlas(*_font, "", _x);
        _maxAscent = MaxAscent(*_font);
        _size = Vector2(static_cast<float>(atlas->Width), static_cast<float>(atlas->Height));
        _texture = renderer.CreateIndexTexture(atlas->Width, atlas->Height, atlas->Indices);
        _palette = palette;
    }

    std::shared_ptr<Formats::Texture> FontView::Atlas(const Formats::Font& font, const std::string& name, std::span<int> x)
    {
        const auto& glyphs = font.Glyphs;
        auto maxAscent = MaxAscent(font);
        auto maxDescent = 0;
        auto sum = 0;
        for (const auto& g : glyphs)
        {
            maxDescent = std::max(maxDescent, g ? g->Descent : 0);
            sum += !g ? 0 : g->Width + 1;
        }

        auto width = std::max(1, sum);
        auto height = maxAscent + maxDescent + 1;
        std::vector<byte> indices(static_cast<size_t>(width) * height);
        auto at = 0;
        for (size_t c = 0; c < glyphs.size(); c++)
        {
            if (!glyphs[c])
            {
                continue;
            }

            const auto& glyph = *glyphs[c];
            auto top = maxAscent - glyph.Ascent;
            for (auto row = 0; row < glyph.Ascent + glyph.Descent + 1; row++)
            {
                std::copy_n(glyph.Indices.begin() + row * glyph.Width, glyph.Width, indices.begin() + (top + row) * width + at);
            }

            if (!x.empty())
            {
                x[c] = at;
            }

            at += glyph.Width + 1;
        }

        auto texture = std::make_shared<Formats::Texture>();
        texture->Name = name;
        texture->Width = width;
        texture->Height = height;
        texture->Indices = std::move(indices);
        return texture;
    }

    int FontView::Width(std::span<const byte> text) const
    {
        return _font->Width(text);
    }

    float FontView::Draw(std::span<const byte> text, float x, float y, float scale, float alpha)
    {
        for (auto c : text)
        {
            if (!_font->Glyphs[c])
            {
                x += _font->SpaceWidth() * scale;
                continue;
            }

            const auto& glyph = *_font->Glyphs[c];
            auto height = glyph.Ascent + glyph.Descent + 1;
            auto source = RectangleF(static_cast<float>(_x[c]), static_cast<float>(_maxAscent - glyph.Ascent), static_cast<float>(glyph.Width),
                static_cast<float>(height));
            auto target = RectangleF(x, y - glyph.Ascent * scale, glyph.Width * scale, height * scale);
            _renderer.DrawImage(_texture, _palette, _size, source, target, Vector4(1.f, 1.f, 1.f, alpha));
            x += glyph.Width * scale;
        }

        return x;
    }
}
