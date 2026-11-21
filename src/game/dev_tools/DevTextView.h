#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/DotNet.h"
#include "core/Drawing.h"
#include "core/Numerics.h"

namespace Mdk::Engine::Render
{
    class Renderer;
}

namespace Mdk::Game::Hud
{
    class FontView;
}

namespace Mdk::Game::DevTools
{
    // Lines of the developer tools in the game's small font, half size, on the canvas.
    class DevTextView
    {
    public:
        static constexpr float Scale = 0.5f;
        // Baseline to baseline, in canvas units.
        static constexpr float LineHeight = 9.f;

        DevTextView(Engine::Render::Renderer& renderer, std::shared_ptr<Hud::FontView> font)
            : _renderer(renderer), _font(std::move(font)) {}

        Engine::Render::Renderer& Renderer() const { return _renderer; }

        // A line from x along the baseline y.
        void Text(const std::string& text, float x, float y);

        float Width(const std::string& text);

        void Fill(RectangleF area, Vector4 colour);

    private:
        Engine::Render::Renderer& _renderer;
        std::shared_ptr<Hud::FontView> _font;
        // The last line's Latin-1 bytes (kept: no array per line drawn).
        std::vector<byte> _bytes;

        // The font's characters are Latin-1 (the console's line only holds those).
        std::span<const byte> Bytes(const std::string& text);
    };
}
