#include "game/menu/MenuCursor.h"

#include <algorithm>

#include "engine/platform/Window.h"
#include "formats/BinReader.h"
#include "formats/Fti.h"
#include "formats/Palette.h"
#include "formats/SpriteAnimation.h"
#include "formats/Texture.h"
#include "game/menu/Ui.h"

namespace Mdk::Game::Menu
{
    void MenuCursor::Apply(Ui& ui)
    {
        auto [width, height] = ui.Window().Size();
        auto scale = Math::Max(ToInt(MathF::Min(width / ScreenView::Size.X, height / ScreenView::Size.Y)), 1);
        if (auto image = Build(*ui.Fti, scale))
        {
            ui.Window().SetCursor(image->Rgba, image->Width, image->Height, image->HotX, image->HotY);
        }
    }

    std::optional<MenuCursor::Image> MenuCursor::Build(const Formats::Fti& fti, int scale)
    {
        if (!fti.Has(Sprite))
        {
            return std::nullopt;
        }

        auto animation = Formats::SpriteAnimation::Parse(Sprite, Formats::Share(fti.GetBytes(Sprite)), SpriteOffset);
        if (animation->FrameCount() < 1)
        {
            return std::nullopt;
        }

        auto frame = animation->GetFrame(0);
        auto palette = Formats::Palette::FromRgb(fti.GetBytes("SYS_PAL"))->Rgba;
        auto w = frame.Image->Width * scale;
        auto h = frame.Image->Height * scale;
        std::vector<byte> rgba(static_cast<size_t>(w * h * Channels));
        for (auto y = 0; y < h; y++)
        {
            for (auto x = 0; x < w; x++)
            {
                auto index = frame.Image->Indices[y / scale * frame.Image->Width + x / scale];
                if (index == 0)
                {
                    continue;
                }

                auto at = (y * w + x) * Channels;
                std::copy_n(palette.begin() + index * Channels, Channels - 1, rgba.begin() + at);
                rgba[at + Channels - 1] = Opaque;
            }
        }

        return Image{rgba, w, h, Math::Clamp(frame.HotspotX * scale, 0, w - 1), Math::Clamp(frame.HotspotY * scale, 0, h - 1)};
    }
}
