#include "game/hud/TouchView.h"

#include <algorithm>
#include <array>
#include <limits>

#include "engine/platform/TouchControls.h"
#include "engine/platform/Window.h"
#include "engine/render/Renderer.h"
#include "game/hud/FontView.h"

namespace Mdk::Game::Hud
{
    TouchView::TouchView(Engine::Render::Renderer& renderer, std::shared_ptr<FontView> font)
        : _renderer(renderer), _font(std::move(font))
    {
        _fillMask = renderer.CreateIndexTexture(MaskSide, MaskSide, MaskOf(Cover::Fill));
        _lineMask = renderer.CreateIndexTexture(MaskSide, MaskSide, MaskOf(Cover::Line));
        std::vector<byte> white(PaletteEntries * BytesPerColour, std::numeric_limits<byte>::max());
        _palette = renderer.CreatePalette(white);
    }

    std::vector<byte> TouchView::MaskOf(Cover cover)
    {
        std::vector<byte> indices(MaskSide * MaskSide);
        for (auto y = 0; y < MaskSide; y++)
        {
            for (auto x = 0; x < MaskSide; x++)
            {
                // The distance in from the edge: along the straight sides, or the corner's arc.
                auto dx = MathF::Max(MaskRadius - (x + 0.5f), x + 0.5f - (MaskSide - MaskRadius));
                auto dy = MathF::Max(MaskRadius - (y + 0.5f), y + 0.5f - (MaskSide - MaskRadius));
                auto outside = Vector2(MathF::Max(dx, 0.f), MathF::Max(dy, 0.f)).Length();
                auto depth = MaskRadius - outside;
                auto covered = cover == Cover::Fill ? depth > 0.f : depth > 0.f && depth <= MaskLine;
                indices[y * MaskSide + x] = covered ? Inside : static_cast<byte>(0);
            }
        }

        return indices;
    }

    void TouchView::Draw(Engine::Platform::Window& window)
    {
        // In a VR viewer no finger reaches the screen: a gamepad plays.
        auto* touch = window.Touch();
        if (touch == nullptr || _renderer.StereoMode == Engine::Render::Stereo::Vr)
        {
            return;
        }

        auto [width, height] = window.Size();
        auto screen = Vector2(static_cast<float>(width), static_cast<float>(height));
        Opacity = touch->Opacity();
        // The first hit is drawn on top.
        const auto& layout = touch->Layout(screen);
        for (auto i = static_cast<int>(layout.size()) - 1; i >= 0; i--)
        {
            const auto& button = layout[i];
            Button(button.Area, button.Label, touch->Held(button.Key) ? Shade::Held : Shade::Idle);
        }

        auto stick = touch->Stick();
        auto at = touch->StickAt();
        if (!stick || !at)
        {
            return;
        }

        // The stick's reach around where the finger landed, and the finger's knob.
        auto reach = Canvas(Circle(*stick, touch->StickRadius(screen)));
        Shape(_fillMask, reach, reach.Width / 2.f, StickFill);
        Shape(_lineMask, reach, reach.Width / 2.f, Outline);
        auto knob = Canvas(Circle(*at, KnobPart * touch->StickRadius(screen)));
        Shape(_fillMask, knob, knob.Width / 2.f, KnobFill);
    }

    void TouchView::Button(RectangleF windowArea, const std::string& label, Shade shade, std::optional<float> side)
    {
        auto area = Canvas(windowArea);
        auto square = Canvas(RectangleF(windowArea.Location(), SizeF{side.value_or(windowArea.Width), side.value_or(windowArea.Height)}));
        auto radius = Rounding * MathF::Min(MathF::Min(area.Width, area.Height), MathF::Min(square.Width, square.Height));
        Vector4 fill;
        switch (shade)
        {
            case Shade::Held:
                fill = HeldFill;
                break;
            case Shade::Selected:
                fill = SelectedFill;
                break;
            case Shade::Overlap:
                fill = OverlapFill;
                break;
            default:
                fill = Fill;
                break;
        }

        Shape(_fillMask, area, radius, fill);
        Shape(_lineMask, area, radius, Outline);
        Label(Bytes(label), area, MathF::Min(area.Height, square.Height));
    }

    void TouchView::Caption(RectangleF windowArea, const std::string& text)
    {
        auto area = Canvas(windowArea);
        Label(Bytes(text), area, area.Height / LabelHeight);
    }

    void TouchView::Disc(RectangleF windowArea)
    {
        auto area = Canvas(windowArea);
        Shape(_fillMask, area, MathF::Min(area.Width, area.Height) / 2.f, HeldFill);
        Shape(_lineMask, area, MathF::Min(area.Width, area.Height) / 2.f, Outline);
    }

    void TouchView::Shape(int mask, RectangleF area, float radius, Vector4 colour)
    {
        // Source and target edges, left to right (top to bottom alike).
        constexpr std::array<float, 4> source{0.f, static_cast<float>(MaskRadius), static_cast<float>(MaskSide - MaskRadius), static_cast<float>(MaskSide)};
        std::array<float, 4> xs{area.Left(), area.Left() + radius, area.Right() - radius, area.Right()};
        std::array<float, 4> ys{area.Top(), area.Top() + radius, area.Bottom() - radius, area.Bottom()};
        auto tint = colour;
        tint.W = colour.W * Opacity;
        for (auto row = 0; row < 3; row++)
        {
            for (auto column = 0; column < 3; column++)
            {
                auto from = RectangleF::FromLTRB(source[column], source[row], source[column + 1], source[row + 1]);
                auto to = RectangleF::FromLTRB(xs[column], ys[row], xs[column + 1], ys[row + 1]);
                if (to.Width > 0.f && to.Height > 0.f)
                {
                    _renderer.DrawImage(mask, _palette, MaskSize, from, to, tint);
                }
            }
        }
    }

    void TouchView::Label(std::span<const byte> text, RectangleF area, float height)
    {
        auto scale = LabelHeight * height / std::max(_font->Ascent(), 1);
        scale = MathF::Min(scale, LabelWidth * area.Width / std::max(_font->Width(text), 1));
        auto x = area.X + (area.Width - _font->Width(text) * scale) / 2.f;
        auto baseline = area.Y + (area.Height + _font->Ascent() * scale) / 2.f;
        _font->Draw(text, x, baseline, scale, LabelAlpha * Opacity);
    }

    RectangleF TouchView::Canvas(RectangleF area) const
    {
        auto from = _renderer.CanvasPoint(area.Left(), area.Top());
        auto to = _renderer.CanvasPoint(area.Right(), area.Bottom());
        return RectangleF::FromLTRB(from.X, from.Y, to.X, to.Y);
    }

    const std::vector<byte>& TouchView::Bytes(const std::string& label)
    {
        auto found = _labels.find(label);
        if (found == _labels.end())
        {
            // Encoding.ASCII: characters above 0x7F are '?'.
            std::vector<byte> bytes;
            for (auto c : label)
            {
                bytes.push_back(static_cast<byte>(c) < 0x80 ? static_cast<byte>(c) : static_cast<byte>('?'));
            }

            found = _labels.emplace(label, std::move(bytes)).first;
        }

        return found->second;
    }
}
