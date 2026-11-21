#include "game/dev_tools/DevTextView.h"

#include <algorithm>

#include "core/Latin1.h"
#include "engine/render/Renderer.h"
#include "game/hud/FontView.h"

namespace Mdk::Game::DevTools
{
    void DevTextView::Text(const std::string& text, float x, float y)
    {
        _font->Draw(Bytes(text), x, y, Scale);
    }

    float DevTextView::Width(const std::string& text)
    {
        return _font->Width(Bytes(text)) * Scale;
    }

    void DevTextView::Fill(RectangleF area, Vector4 colour)
    {
        _renderer.FillRect(area, colour);
    }

    std::span<const byte> DevTextView::Bytes(const std::string& text)
    {
        if (_bytes.size() < text.size())
        {
            _bytes.resize(std::max(text.size(), _bytes.size() * 2));
        }

        auto count = Latin1::GetBytes(text, _bytes);
        return std::span<const byte>(_bytes.data(), count);
    }
}
