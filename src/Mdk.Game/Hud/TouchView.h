#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Drawing.h"
#include "Mdk.Core/Numerics.h"

namespace Mdk::Engine::Platform
{
    class Window;
}

namespace Mdk::Engine::Render
{
    class Renderer;
}

namespace Mdk::Game::Hud
{
    class FontView;

    // Draws the on-screen controls of a touch screen (TouchControls) in play: rounded buttons (a
    // see-through fill, a thin outline, the label; brighter while held), the stick's ring and knob
    // where a finger walks.
    //   a shape: 9 parts of a rounded square's mask (corners kept, edges and centre stretched)
    //   ┌──┬────┬──┐
    //   │ ╭┼────┼╮ │   r = corner radius on the canvas
    //   ├──┼────┼──┤
    //   │ ╰┼────┼╯ │
    //   └──┴────┴──┘
    class TouchView
    {
    public:
        // How a control is drawn: as it is, held (pressed), selected or overlapping (the editor).
        enum class Shade { Idle, Held, Selected, Overlap };

        // The controls' opacity (1: as drawn by default); play takes the controls'.
        float Opacity = 1.f;

        TouchView(Engine::Render::Renderer& renderer, std::shared_ptr<FontView> font);

        void Draw(Engine::Platform::Window& window);

        // A rounded button (window pixels): its fill by `shade`, an outline, its label. Its corners
        // and label are as a square button's of `side` (window pixels; null: its shorter side), so a
        // large one (the stick's zone) isn't drawn heavier.
        void Button(RectangleF windowArea, const std::string& label, Shade shade, std::optional<float> side = std::nullopt);

        // A line of text centred in `windowArea` (window pixels), as high as it, narrower when it
        // doesn't fit (the editor's help).
        void Caption(RectangleF windowArea, const std::string& text);

        // A filled disc in `windowArea` (the editor's handle).
        void Disc(RectangleF windowArea);

    private:
        // What a mask covers.
        enum class Cover { Fill, Line };

        // The masks' corner radius and outline width (texels); a mask is 2 × radius + 1 wide.
        static constexpr int MaskRadius = 32;
        static constexpr float MaskLine = 4.f;
        static constexpr int MaskSide = 2 * MaskRadius + 1;
        static constexpr int PaletteEntries = 256;
        static constexpr int BytesPerColour = 4;
        static constexpr byte Inside = 1;
        // A button's corner radius, a part of its side.
        static constexpr float Rounding = 0.25f;
        // The knob's radius, a part of the stick's.
        static constexpr float KnobPart = 0.4f;
        // A label's height and widest width, parts of its button.
        static constexpr float LabelHeight = 0.3f;
        static constexpr float LabelWidth = 0.7f;
        static constexpr Vector4 Fill{1.f, 1.f, 1.f, 0.18f};
        static constexpr Vector4 HeldFill{1.f, 1.f, 1.f, 0.45f};
        static constexpr Vector4 Outline{1.f, 1.f, 1.f, 0.55f};
        static constexpr Vector4 StickFill{1.f, 1.f, 1.f, 0.1f};
        static constexpr Vector4 KnobFill{1.f, 1.f, 1.f, 0.4f};
        // The editor's selected control, and buttons covering each other.
        static constexpr Vector4 SelectedFill{1.f, 0.8f, 0.2f, 0.45f};
        static constexpr Vector4 OverlapFill{1.f, 0.2f, 0.15f, 0.55f};
        static constexpr float LabelAlpha = 0.85f;
        static constexpr Vector2 MaskSize{static_cast<float>(MaskSide), static_cast<float>(MaskSide)};

        Engine::Render::Renderer& _renderer;
        std::shared_ptr<FontView> _font;
        // The rounded square's inside and its outline (index 1 through a white palette).
        int _fillMask = 0;
        int _lineMask = 0;
        int _palette = 0;
        // The labels' bytes, made once (frames don't allocate).
        std::unordered_map<std::string, std::vector<byte>> _labels;

        // A rounded square's mask: the corners are quarter circles of the mask's radius about
        // (radius, radius); the outline is the inside's last MaskLine texels.
        static std::vector<byte> MaskOf(Cover cover);

        static RectangleF Circle(Vector2 centre, float radius)
        {
            return RectangleF(centre.X - radius, centre.Y - radius, 2.f * radius, 2.f * radius);
        }

        // A mask over `area` with corners of `radius`: its corners as they are, its middle row and
        // column stretched.
        void Shape(int mask, RectangleF area, float radius, Vector4 colour);

        // A label centred on its button, scaled to its size.
        void Label(std::span<const byte> text, RectangleF area, float height);

        // A window rectangle on the canvas.
        RectangleF Canvas(RectangleF area) const;

        const std::vector<byte>& Bytes(const std::string& label);
    };
}
