#include "game/hud/SniperOverlay.h"

#include <algorithm>
#include <format>
#include <limits>

#include "core/DotNet.h"
#include "engine/render/Renderer.h"
#include "formats/Bni.h"
#include "formats/Palette.h"
#include "formats/SpriteAnimation.h"
#include "formats/Texture.h"
#include "game/mods/CanvasImages.h"

namespace Mdk::Game::Hud
{
    using Engine::Render::Renderer;
    using Scripts::SniperRounds;

    SniperOverlay::SniperOverlay(Renderer& renderer, Formats::Bni& sprites, std::shared_ptr<Formats::Palette> palette, int paletteTexture,
        const std::shared_ptr<Mods::ModImages>& mods)
        : _renderer(renderer)
    {
        _colours = std::move(palette);
        _palette = paletteTexture;
        _mods = mods;
        auto mask = Formats::SniperScreen::Mask(sprites);
        _frame = Load(FrameImage, *CutView(*Formats::SniperScreen::Frame(sprites), *mask), Vector2::Zero());
        _mask = Load(MaskImage, *mask, Vector2::Zero());
        auto cross = sprites.GetAnimation(Cross)->GetFrame(0);
        _cross = Load(Mods::CanvasImages::FrameName(Cross, 0), *cross.Image,
            Vector2(static_cast<float>(cross.HotspotX), static_cast<float>(cross.HotspotY)));
        _gauge = Load(GaugeImage, *sprites.GetImage(GaugeImage), Vector2::Zero());
        _weapon = Load(WeaponImage, *sprites.GetImage(WeaponImage), Vector2::Zero());
        _digits = Load(Digits, *sprites.GetImage(Digits), Vector2::Zero());
        for (auto i = 1; i <= Types; i++)
        {
            _icons.push_back(Load(std::format("{}{}", Icon, i), *sprites.GetImage(std::format("{}{}", Icon, i)), Vector2::Zero()));
            _labels.push_back(Load(std::format("{}{}", Label, i), *sprites.GetImage(std::format("{}{}", Label, i)), Vector2::Zero()));
        }

        auto miss = sprites.GetAnimation(Miss);
        for (auto i = 0; i < miss->FrameCount(); i++)
        {
            _miss.push_back(Load(Mods::CanvasImages::FrameName(Miss, i), *miss->GetFrame(i).Image, Vector2::Zero()));
        }
    }

    std::vector<std::pair<std::string, std::shared_ptr<Formats::Texture>>> SniperOverlay::Images(Formats::Bni& sprites)
    {
        std::vector<std::pair<std::string, std::shared_ptr<Formats::Texture>>> images;
        auto mask = Formats::SniperScreen::Mask(sprites);
        images.emplace_back(FrameImage, CutView(*Formats::SniperScreen::Frame(sprites), *mask));
        images.emplace_back(MaskImage, mask);
        images.emplace_back(Mods::CanvasImages::FrameName(Cross, 0), sprites.GetAnimation(Cross)->GetFrame(0).Image);
        for (const auto* name : {GaugeImage, WeaponImage, Digits})
        {
            images.emplace_back(name, sprites.GetImage(name));
        }

        for (auto i = 1; i <= Types; i++)
        {
            images.emplace_back(std::format("{}{}", Icon, i), sprites.GetImage(std::format("{}{}", Icon, i)));
            images.emplace_back(std::format("{}{}", Label, i), sprites.GetImage(std::format("{}{}", Label, i)));
        }

        auto miss = sprites.GetAnimation(Miss);
        for (auto i = 0; i < miss->FrameCount(); i++)
        {
            images.emplace_back(Mods::CanvasImages::FrameName(Miss, i), miss->GetFrame(i).Image);
        }

        return images;
    }

    SniperOverlay::Image SniperOverlay::Load(const std::string& name, const Formats::Texture& texture, Vector2 hotspot)
    {
        return {Mods::CanvasImages::Create(_renderer, _mods, name, texture, *_colours, _palette),
            Vector2(static_cast<float>(texture.Width), static_cast<float>(texture.Height)), hotspot};
    }

    std::shared_ptr<Formats::Texture> SniperOverlay::CutView(const Formats::Texture& frame, const Formats::Texture& mask)
    {
        auto indices = frame.Indices;
        auto left = ToInt(View.X);
        auto top = ToInt(View.Y);
        for (auto y = 0; y < Formats::SniperScreen::ViewHeight; y++)
        {
            for (auto x = 0; x < Formats::SniperScreen::ViewWidth; x++)
            {
                auto edge = x == 0 || y == 0 || x == Formats::SniperScreen::ViewWidth - 1 || y == Formats::SniperScreen::ViewHeight - 1;
                if (edge && mask.Indices[y * mask.Width + x] != 0)
                {
                    continue;
                }

                indices[(top + y) * frame.Width + left + x] = 0;
            }
        }

        auto texture = std::make_shared<Formats::Texture>();
        texture->Name = frame.Name;
        texture->Width = frame.Width;
        texture->Height = frame.Height;
        texture->Indices = std::move(indices);
        return texture;
    }

    void SniperOverlay::Update(float delta, const std::optional<SniperHud>& hud)
    {
        _shown = hud;
        if (!hud)
        {
            _iris = 1.f;
            _pulse = 1.f;
            return;
        }

        const auto& shown = *hud;
        UpdateIris(delta, shown);
        auto goal = ToInt(MathF::Round(GaugeHeight * ZoomFraction(shown.Zoom)));
        _gaugeShown = Math::Clamp(goal, _gaugeShown - GaugeSpeed, _gaugeShown + GaugeSpeed);
    }

    float SniperOverlay::ZoomFraction(float zoom)
    {
        return Math::Clamp((1.f - zoom) * (1.f - zoom) * ZoomPercentFactor, 0.f, 1.f);
    }

    void SniperOverlay::UpdateIris(float delta, const SniperHud& hud)
    {
        auto strike = hud.SelectedAmmo == StrikeType;
        if (strike && hud.Aim != StrikeAim::NotChecked)
        {
            _target = hud.Aim == StrikeAim::Target;
            if (_target)
            {
                _iris = MathF::Max(_iris - delta, 0.f);
            }
        }

        if (!strike || !_target)
        {
            _iris += delta;
            if (_iris >= 1.f)
            {
                _iris = 1.f;
                _pulse = 1.f;
                return;
            }
        }

        if (_iris == 0.f || _pulse != 1.f)
        {
            _pulse -= delta;
            if (_pulse < 0.f)
            {
                _pulse = 1.f;
            }
        }
    }

    std::tuple<float, float> SniperOverlay::Placement(float canvasWidth)
    {
        auto scale = Renderer::CanvasHeight / ScreenHeight;
        return {scale, (canvasWidth - ScreenWidth * scale) / 2.f};
    }

    void SniperOverlay::Draw(float canvasWidth, int health, Blink blink, Vector2 panel, Vector2 panelSize)
    {
        if (!_shown)
        {
            return;
        }

        const auto& hud = *_shown;
        auto [scale, left] = Placement(canvasWidth);
        if (left > 0.f)
        {
            _renderer.FillRect(RectangleF(0.f, 0.f, MathF::Ceiling(left), Renderer::CanvasHeight), Black);
            _renderer.FillRect(RectangleF(MathF::Floor(canvasWidth - left), 0.f, MathF::Ceiling(left), Renderer::CanvasHeight), Black);
        }

        auto screen = Screen{&_renderer, _palette, scale, left};
        DrawFrame(screen);
        DrawCameras(screen, hud);
        DrawIris(screen);
        screen.Image(_mask, View);
        screen.Image(_cross, View + Crosshair - _cross.Hotspot);

        auto fraction = ZoomFraction(hud.Zoom);
        DrawNumber(screen, ToInt(MathF::Round(Percent * fraction)), View + ZoomDigits);
        if (_gaugeShown > 0)
        {
            auto top = static_cast<float>(GaugeHeight - _gaugeShown);
            screen.Part(_gauge, RectangleF(0.f, top, _gauge.Size.X, static_cast<float>(_gaugeShown)), View + Gauge + Vector2(0.f, top));
        }

        DrawAmmo(screen, hud);

        // Health in the frame's panel, where the normal view draws SC_STAT.
        if (blink == Blink::Shown)
        {
            auto centre = Vector2(static_cast<float>(ToInt(panelSize.X) >> 1), static_cast<float>((ToInt(panelSize.Y) - ToInt(_digits.Size.Y)) >> 1));
            DrawNumber(screen, health, View + panel + centre);
        }
    }

    void SniperOverlay::DrawFrame(const Screen& screen)
    {
        constexpr auto viewWidth = static_cast<float>(Formats::SniperScreen::ViewWidth);
        constexpr auto viewHeight = static_cast<float>(Formats::SniperScreen::ViewHeight);
        auto viewBottom = View.Y + viewHeight;
        screen.Fill(RectangleF(0.f, 0.f, ScreenWidth, View.Y), Black);
        screen.Fill(RectangleF(0.f, viewBottom, ScreenWidth, ScreenHeight - viewBottom), Black);
        screen.Fill(RectangleF(0.f, View.Y, View.X, viewHeight), Black);
        screen.Fill(RectangleF(View.X + viewWidth, View.Y, ScreenWidth - View.X - viewWidth, viewHeight), Black);
        screen.Image(_frame, Vector2::Zero());
    }

    void SniperOverlay::DrawCameras(const Screen& screen, const SniperHud& hud)
    {
        for (size_t i = 0; i < RoundViews.size() && i < hud.Cameras.size(); i++)
        {
            const auto& camera = hud.Cameras[i];
            auto rect = RoundViews[i];
            rect.Offset(View.X, View.Y);
            if (camera.Shot == SniperRounds::Shot::Watching)
            {
                continue;
            }

            if (camera.Shot == SniperRounds::Shot::Miss)
            {
                screen.Fill(rect, Black);
                const auto& frame = _miss[ToInt(camera.Time / MissFrameTicks) % static_cast<int>(_miss.size())];
                screen.Image(frame, Vector2(rect.X + rect.Width / 2.f, rect.Y + rect.Height / 2.f) - frame.Size / 2.f);
                continue;
            }

            auto fill = camera.Shot == SniperRounds::Shot::Hit ? HitFill
                : camera.Shot == SniperRounds::Shot::Kill    ? KillFill
                                                              : EmptyFill;
            screen.Fill(rect, Colour(fill));
        }
    }

    Vector4 SniperOverlay::Colour(int index) const
    {
        constexpr float Max = std::numeric_limits<byte>::max();
        return Vector4(_colours->Rgba[index * 4], _colours->Rgba[index * 4 + 1], _colours->Rgba[index * 4 + 2], Max) / Max;
    }

    void SniperOverlay::DrawIris(const Screen& screen)
    {
        if (_iris >= 1.f)
        {
            return;
        }

        std::vector<std::pair<float, Vector4>> circles;
        auto hole = IrisRadius * _iris * _iris;
        auto outer = RingOuter * _pulse * _pulse;
        auto inner = RingInner * _pulse * _pulse;
        if (outer > hole && _pulse != 1.f)
        {
            circles.emplace_back(outer, RingColour);
            if (inner > hole)
            {
                circles.emplace_back(inner, IrisColour);
            }
        }

        circles.emplace_back(hole, Vector4::Zero());
        for (auto y = IrisTop; y < IrisBottom; y++)
        {
            auto dy = y - IrisCentre.Y;
            auto colour = IrisColour;
            auto outside = IrisHalfWidth;
            for (const auto& [r, next] : circles)
            {
                auto edge = MathF::Min(MathF::Abs(dy) < r ? MathF::Sqrt(r * r - dy * dy) : 0.f, outside);
                DrawSpan(screen, y, edge, outside, colour);
                outside = edge;
                colour = next;
            }

            DrawSpan(screen, y, 0.f, outside, colour);
        }
    }

    void SniperOverlay::DrawSpan(const Screen& screen, int y, float near, float far, Vector4 colour)
    {
        if (colour.W == 0.f || far <= near)
        {
            return;
        }

        auto width = far - near;
        screen.Fill(RectangleF(View.X + IrisCentre.X - far, View.Y + y, width, 1.f), colour);
        screen.Fill(RectangleF(View.X + IrisCentre.X + near, View.Y + y, width, 1.f), colour);
    }

    void SniperOverlay::DrawAmmo(const Screen& screen, const SniperHud& hud)
    {
        screen.Image(_weapon, View + Weapon);
        for (auto i = 0; i < Types; i++)
        {
            if (i == 0 || hud.Ammo[i - 1] > 0)
            {
                screen.Image(_icons[i], View + TypeIcons[i]);
            }
        }

        auto selected = hud.SelectedAmmo;
        screen.Image(_labels[selected], View + TypeLabels[selected]);
        if (selected > 0)
        {
            DrawNumber(screen, hud.Ammo[selected - 1], View + Count);
        }
    }

    void SniperOverlay::DrawNumber(const Screen& screen, int value, Vector2 at)
    {
        auto text = std::to_string(Math::Clamp(value, 0, MaxNumber));
        auto x = at.X - DigitWidth / 2.f * static_cast<float>(text.size());
        for (auto c : text)
        {
            auto source = RectangleF(static_cast<float>((c - '0') * DigitWidth), 0.f, static_cast<float>(DigitWidth), _digits.Size.Y);
            screen.Part(_digits, source, Vector2(x, at.Y));
            x += DigitWidth;
        }
    }

    void SniperOverlay::Screen::Fill(RectangleF r, Vector4 colour) const
    {
        Renderer->FillRect(ToCanvas(r), colour);
    }

    void SniperOverlay::Screen::Image(const SniperOverlay::Image& image, Vector2 at) const
    {
        Part(image, RectangleF(0.f, 0.f, image.Size.X, image.Size.Y), at);
    }

    void SniperOverlay::Screen::Part(const SniperOverlay::Image& image, RectangleF source, Vector2 at) const
    {
        Renderer->DrawImage(image.Texture, Palette, image.Size, source, ToCanvas(RectangleF(at.X, at.Y, source.Width, source.Height)), Vector4::One());
    }
}
