#include "Mdk.Game/Menu/LoadingScreen.h"

#include <format>

#include "Mdk.Core/Files.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Formats/BetaDemo.h"
#include "Mdk.Formats/Fti.h"
#include "Mdk.Formats/MdkData.h"
#include "Mdk.Formats/Palette.h"
#include "Mdk.Formats/Texture.h"
#include "Mdk.Game/Hud/FontView.h"
#include "Mdk.Game/Menu/Ui.h"

namespace Mdk::Game::Menu
{
    LoadingScreen::LoadingScreen(Ui& ui, int level) : _ui(ui)
    {
        auto path = ui.Data()->PathOf(std::format("MISC/LOAD_{}.LBB", level));
        auto bytes = File::Exists(path) ? File::ReadAllBytes(path) : std::vector<byte>{};

        // The 1996 demo's screens have the same layout.
        if (Formats::BetaDemo::IsBeta(level) && ui.Beta != nullptr)
        {
            bytes = ui.Beta->LoadingScreen(Formats::BetaDemo::LevelOf(level));
        }

        if (bytes.size() <= PaletteSize)
        {
            return;
        }

        std::vector<byte> rgb(bytes.begin(), bytes.begin() + PaletteSize);
        auto palette = Formats::Palette::FromRgb(rgb);
        auto image = PalettedImage::Of(ui.Renderer(), *Formats::Texture::Parse(std::format("LOAD_{}", level), bytes, PaletteSize),
            rgb, ui.CanvasMods());
        auto font = Fonts(ui.Renderer(), *ui.Fti, palette.get(), ui.CanvasMods()).Big;
        _look = Look{image, font, Colour(*palette, BarFill), Colour(*palette, BarFrame)};
    }

    void LoadingScreen::Show(float progress)
    {
        auto& view = *_ui.View;
        view.Layout(ScreenView::Fit::Height);
        if (_look.has_value())
        {
            auto& [image, font, fill, frame] = *_look;
            image->Draw(view, ImagePosition);
            auto text = _ui.Fti->GetTextBytes("LOAD_MSG");
            view.Text(*font, text, (ScreenView::Size.X - font->Width(text)) / 2.f, TextY);
            view.Fill(RectangleF(Bar.X, Bar.Y, Bar.Width * Math::Clamp(progress, 0.f, 1.f), Bar.Height), fill);
            _ui.Renderer().FrameRect(view.ToCanvas(Bar), view.Scale(), frame);
        }

        // The level being loaded has set its sky and light already: the loading screen shows neither.
        auto& renderer = _ui.Renderer();
        auto sky = renderer.Panorama;
        auto light = renderer.Lighting;
        renderer.Panorama = std::nullopt;
        renderer.Lighting = std::nullopt;
        _ui.Present(std::nullopt);
        renderer.Panorama = sky;
        renderer.Lighting = light;
    }

    Vector4 LoadingScreen::Colour(const Formats::Palette& palette, int index)
    {
        constexpr float ByteMax = 255.f;
        return Vector4(palette.Rgba[index * 4], palette.Rgba[index * 4 + 1], palette.Rgba[index * 4 + 2], ByteMax) / ByteMax;
    }
}
