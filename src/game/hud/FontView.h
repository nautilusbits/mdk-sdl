#pragma once

#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/DotNet.h"
#include "core/Numerics.h"

namespace Mdk::Engine::Render
{
    class Renderer;
}

namespace Mdk::Formats
{
    class Font;
    class Palette;
    class Texture;
}

namespace Mdk::Game::Mods
{
    class ModImages;
}

namespace Mdk::Game::Hud
{
    // Draws a font of MDKFONT.FTI like the original (0x415a20): glyphs from ascent rows above the
    // baseline to descent below, scaled about the baseline (0x415d8c). The glyphs sit side by side in
    // one index texture.
    //   atlas: | A | B | C | ... |   each glyph at its x, top at (max ascent - ascent)
    class FontView
    {
    public:
        // The fonts' names in MDKFONT.FTI, and in mods (their atlas, Atlas).
        static constexpr const char* Big = "FONTBIG";
        static constexpr const char* Small = "FONTSML";

        // A font through a palette; a mod's image of its atlas (`name`) replaces it.
        FontView(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Font> font, const Formats::Palette& palette,
            const std::shared_ptr<Mods::ModImages>& mods = nullptr, const std::string& name = "");

        // A font drawn through a palette the caller owns (the fall's, with its effects).
        FontView(Engine::Render::Renderer& renderer, std::shared_ptr<Formats::Font> font, int palette);

        // The glyphs side by side, one column apart, tops at (max ascent - ascent); each
        // glyph's x into `x` when given (a mod's image of it: any size, same layout).
        static std::shared_ptr<Formats::Texture> Atlas(const Formats::Font& font, const std::string& name,
            std::span<int> x = {});

        int Width(std::span<const byte> text) const;

        // The tallest glyph's rows above the baseline (unscaled).
        int Ascent() const { return _maxAscent; }

        // Draws a text from `x` along the baseline `y`, scaled about the baseline. Returns the x after it.
        float Draw(std::span<const byte> text, float x, float y, float scale = 1.f, float alpha = 1.f);

    private:
        Engine::Render::Renderer& _renderer;
        std::shared_ptr<Formats::Font> _font;
        int _texture = 0;
        int _palette = 0;
        std::array<int, 256> _x{};
        int _maxAscent = 0;
        Vector2 _size;
    };
}
