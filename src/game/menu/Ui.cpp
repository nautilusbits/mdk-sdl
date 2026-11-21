#include "game/menu/Ui.h"

#include "engine/platform/Input.h"
#include "engine/platform/Window.h"
#include "engine/render/Renderer.h"
#include "formats/BetaDemo.h"
#include "formats/Bni.h"
#include "formats/Font.h"
#include "formats/Fti.h"
#include "formats/MdkData.h"
#include "formats/Palette.h"
#include "formats/Texture.h"
#include "formats/Wav.h"
#include "game/dev_tools/DevSession.h"
#include "game/flow/Settings.h"
#include "game/hud/FontView.h"
#include "game/mods/CanvasImages.h"
#include "game/mods/ModCatalog.h"
#include "game/mods/ModFiles.h"
#include "game/mods/ModImages.h"

namespace Mdk::Game::Menu
{
    Ui::Ui(std::shared_ptr<Formats::MdkData> data, Engine::Platform::Window& window, Engine::Render::Renderer& renderer,
        Engine::Audio::AudioDevice& audio, Engine::Platform::Input& input, Flow::Settings& settings, std::string userFolder)
        : Fti(Formats::Fti::Load(data->PathOf("MISC/MDKFONT.FTI"))),
          View(std::make_shared<ScreenView>(renderer)),
          Beta(Formats::BetaDemo::Find(data)),
          Dev(std::make_shared<DevTools::DevSession>()),
          Mods(Game::Mods::ModCatalog::Scan(Game::Mods::ModCatalog::FolderIn(userFolder))),
          _data(std::move(data)),
          _window(window),
          _renderer(renderer),
          _audio(audio),
          _input(input),
          _settings(settings),
          _userFolder(std::move(userFolder))
    {
    }

    void Ui::ScanMods()
    {
        Mods->Close();
        Mods = Game::Mods::ModCatalog::Scan(Game::Mods::ModCatalog::FolderIn(_userFolder));
        _menuImages = nullptr;
    }

    std::shared_ptr<Game::Mods::ModImages> Ui::ModImages(std::optional<int> level)
    {
        return Mods->ModImages(_settings, level);
    }

    std::shared_ptr<Game::Mods::ModImages> Ui::MenuImages()
    {
        std::vector<std::string> wheres;
        for (const auto& f : Mods->Files(_settings))
        {
            wheres.push_back(f->Where(""));
        }

        auto folders = Str::Join(std::string(1, FolderSeparator), wheres);
        if (_menuImages == nullptr || folders != _menuFolders)
        {
            _menuImages = ModImages(std::nullopt);
            _menuFolders = folders;
        }

        return _menuImages;
    }

    std::shared_ptr<Game::Mods::ModImages> Ui::CanvasMods(std::optional<int> level)
    {
        if (_settings.Graphics != Flow::Graphics::Enhanced)
        {
            return nullptr;
        }

        return level.has_value() ? ModImages(*level) : MenuImages();
    }

    void Ui::ApplySettings()
    {
        _settings.Apply(_audio, _window, _renderer, _input);
        _settings.Save(Flow::Settings::PathIn(_userFolder));
    }

    void Ui::Present(const std::optional<std::string>& screenshot)
    {
        _renderer.Present(Engine::Render::View{}, Black, screenshot);
    }

    std::shared_ptr<Engine::Audio::Sound> Ui::SoundOf(std::span<const byte> wav, Engine::Audio::Looping looping)
    {
        auto parsed = Formats::Wav::Parse(wav);
        return !parsed.has_value()
            ? nullptr
            : Engine::Audio::Sound::FromPcm(parsed->Data, parsed->Channels, parsed->SampleRate, parsed->BitsPerSample, looping);
    }

    std::shared_ptr<Engine::Audio::Sound> Ui::SoundOf(const Formats::Bni& bni, const std::string& name,
        Engine::Audio::Looping looping)
    {
        return bni.Has(name) ? SoundOf(BytesOf(bni, name), looping) : nullptr;
    }

    std::vector<byte> Ui::BytesOf(const Formats::Bni& bni, const std::string& name)
    {
        const auto& entry = bni.Entries().At(name);
        const auto& bytes = *bni.Bytes();
        return std::vector<byte>(bytes.begin() + entry.Offset, bytes.begin() + entry.Offset + entry.Size);
    }

    int Ui::Play(const std::shared_ptr<Engine::Audio::Sound>& sound, Engine::Audio::Bus bus, float gain)
    {
        return sound == nullptr ? 0 : _audio.Play(sound, gain, 1.f, 0.f, bus);
    }

    void ScreenView::Layout(Fit fit, std::optional<Vector2> size)
    {
        auto safe = Centred(_renderer.CanvasSafeArea(),
            Vector2(_renderer.CanvasWidth(), Engine::Render::Renderer::CanvasHeight));
        std::tie(_scale, _origin) = Place(fit, size.value_or(Size), Vector2(safe.Width, safe.Height));
        _origin += Vector2(safe.X, safe.Y);
    }

    RectangleF ScreenView::Centred(RectangleF safe, Vector2 canvas)
    {
        auto x = MathF::Max(safe.Left(), canvas.X - safe.Right());
        auto y = MathF::Max(safe.Top(), canvas.Y - safe.Bottom());
        return RectangleF::FromLTRB(x, y, canvas.X - x, canvas.Y - y);
    }

    std::tuple<float, Vector2> ScreenView::Place(Fit fit, Vector2 view, Vector2 canvas)
    {
        auto height = fit == Fit::Height && MathF::Abs(canvas.X / canvas.Y - FourByThree) < AspectTolerance;
        auto scale = height ? canvas.Y / view.Y : MathF::Min(canvas.X / view.X, canvas.Y / view.Y);
        return {scale, (canvas - view * scale) / 2.f};
    }

    RectangleF ScreenView::ToCanvas(RectangleF area) const
    {
        return RectangleF(_origin.X + area.X * _scale, _origin.Y + area.Y * _scale, area.Width * _scale, area.Height * _scale);
    }

    Vector2 ScreenView::Pointer(Engine::Platform::Input& input) const
    {
        return (_renderer.CanvasPoint(input.PointerX(), input.PointerY()) - _origin) / _scale;
    }

    void ScreenView::Fill(RectangleF area, Vector4 colour)
    {
        _renderer.FillRect(ToCanvas(area), colour);
    }

    void ScreenView::FillCanvas(Vector4 colour)
    {
        _renderer.FillRect(RectangleF(0.f, 0.f, _renderer.CanvasWidth(), Engine::Render::Renderer::CanvasHeight), colour);
    }

    void ScreenView::Text(Hud::FontView& font, std::span<const byte> text, float x, float y, float scale, float alpha)
    {
        auto at = ToCanvas(Vector2(x, y));
        font.Draw(text, at.X, at.Y, scale * _scale, alpha);
    }

    Fonts::Fonts(Engine::Render::Renderer& renderer, const Formats::Fti& fti, const Formats::Palette* palette,
        const std::shared_ptr<Game::Mods::ModImages>& mods)
    {
        auto own = palette == nullptr ? Formats::Palette::FromRgb(fti.GetBytes("SYS_PAL")) : nullptr;
        const auto& colours = palette != nullptr ? *palette : *own;
        Big = std::make_shared<Hud::FontView>(renderer, Formats::Font::Parse(fti.GetBytes(Hud::FontView::Big), BigSpace),
            colours, mods, Hud::FontView::Big);
        Small = std::make_shared<Hud::FontView>(renderer, Formats::Font::Parse(fti.GetBytes(Hud::FontView::Small), SmallSpace),
            colours, mods, Hud::FontView::Small);
    }

    PalettedImage::PalettedImage(Engine::Render::Renderer& renderer, int width, int height, std::span<const byte> indices,
        std::span<const byte> paletteRgb)
        : Width(width), Height(height), _renderer(renderer)
    {
        _texture = renderer.CreateIndexTexture(width, height, indices);
        _palette = renderer.CreatePalette(Formats::Palette::FromRgb(paletteRgb)->Rgba);
    }

    std::shared_ptr<PalettedImage> PalettedImage::Of(Engine::Render::Renderer& renderer, const Formats::Texture& texture,
        std::span<const byte> paletteRgb, const std::shared_ptr<Game::Mods::ModImages>& mods)
    {
        auto image = std::make_shared<PalettedImage>(renderer, texture.Width, texture.Height, texture.Indices, paletteRgb);
        Game::Mods::CanvasImages::Replace(renderer, mods, texture.Name, texture, *Formats::Palette::FromRgb(paletteRgb),
            image->_texture, image->_palette);
        return image;
    }

    void PalettedImage::SetIndices(std::span<const byte> indices)
    {
        _renderer.UpdateTexture(_texture, Width, Height, indices);
    }

    void PalettedImage::SetPalette(std::span<const byte> paletteRgb)
    {
        auto rgba = Formats::Palette::FromRgb(paletteRgb)->Rgba;
        if (rgba.size() == PaletteBytes)
        {
            _renderer.UpdateTexture(_palette, static_cast<int>(rgba.size()) / 4, 1, rgba);
        }
    }

    void PalettedImage::Draw(const ScreenView& view, Vector2 at)
    {
        _renderer.DrawImage(_texture, _palette, Size(), RectangleF(0.f, 0.f, static_cast<float>(Width), static_cast<float>(Height)),
            view.ToCanvas(RectangleF(at.X, at.Y, static_cast<float>(Width), static_cast<float>(Height))), Vector4::One());
    }
}
