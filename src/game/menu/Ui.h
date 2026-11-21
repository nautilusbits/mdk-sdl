#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "core/DotNet.h"
#include "core/Drawing.h"
#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"

namespace Mdk::Engine::Platform
{
    class Input;
    class Window;
}

namespace Mdk::Engine::Render
{
    class Renderer;
}

namespace Mdk::Formats
{
    class BetaDemo;
    class Bni;
    class Fti;
    class MdkData;
    class Palette;
    class Texture;
}

namespace Mdk::Game::DevTools
{
    class DevSession;
}

namespace Mdk::Game::Flow
{
    class Settings;
}

namespace Mdk::Game::Hud
{
    class FontView;
}

namespace Mdk::Game::Mods
{
    class ModCatalog;
    class ModImages;
}

namespace Mdk::Game::Menu
{
    class ScreenView;

    // What the game's screens share: the data, the engine, the settings and MDKFONT.FTI.
    // GPU resources are not kept here: each screen creates its own (see Renderer.Release).
    class Ui
    {
    public:
        Ui(std::shared_ptr<Formats::MdkData> data, Engine::Platform::Window& window, Engine::Render::Renderer& renderer,
            Engine::Audio::AudioDevice& audio, Engine::Platform::Input& input, Flow::Settings& settings, std::string userFolder);

        const std::shared_ptr<Formats::MdkData>& Data() const { return _data; }
        Engine::Platform::Window& Window() const { return _window; }
        Engine::Render::Renderer& Renderer() const { return _renderer; }
        Engine::Audio::AudioDevice& Audio() const { return _audio; }
        Engine::Platform::Input& Input() const { return _input; }
        Flow::Settings& Settings() const { return _settings; }
        // Where the settings and the saved games are kept.
        const std::string& UserFolder() const { return _userFolder; }

        std::shared_ptr<Formats::Fti> Fti;
        std::shared_ptr<ScreenView> View;
        // The 1996 demo, when it's found (its levels are extras).
        std::shared_ptr<Formats::BetaDemo> Beta;
        // The console's and the debug overlay's session: the log, timings, history.
        std::shared_ptr<DevTools::DevSession> Dev;
        // Imports the game files again (Android); null elsewhere.
        std::function<void()> Import;
        // The touch layout editor without a touch screen (--touch-layout; tests).
        bool TouchEditor = false;
        // The mods found in the user folder (ModCatalog); the Mods page scans again.
        std::shared_ptr<Game::Mods::ModCatalog> Mods;

        void ScanMods();

        // The enabled mods' images for a level (null: the menus).
        std::shared_ptr<Game::Mods::ModImages> ModImages(std::optional<int> level);

        // The menus' mod images (2D: drawn by the enhanced look only), kept while the same mods are on.
        std::shared_ptr<Game::Mods::ModImages> MenuImages();

        // Mods' 2D images for a screen (null: the menus'): none in the original look, which draws the originals.
        std::shared_ptr<Game::Mods::ModImages> CanvasMods(std::optional<int> level = std::nullopt);

        // The settings changed: applied, and saved.
        void ApplySettings();

        // A 2D frame (no 3D scene) on black.
        void Present(const std::optional<std::string>& screenshot);

        static constexpr Vector4 Black{0.f, 0.f, 0.f, 1.f};
        static constexpr Vector4 White{1.f, 1.f, 1.f, 1.f};

        // A sound of a WAV file, or null.
        static std::shared_ptr<Engine::Audio::Sound> SoundOf(std::span<const byte> wav,
            Engine::Audio::Looping looping = Engine::Audio::Looping::Once);

        // A BNI entry's WAV as a sound, or null.
        static std::shared_ptr<Engine::Audio::Sound> SoundOf(const Formats::Bni& bni, const std::string& name,
            Engine::Audio::Looping looping = Engine::Audio::Looping::Once);

        // A BNI entry's bytes.
        static std::vector<byte> BytesOf(const Formats::Bni& bni, const std::string& name);

        // Plays a sound (at full volume of its bus); returns the voice.
        int Play(const std::shared_ptr<Engine::Audio::Sound>& sound, Engine::Audio::Bus bus = Engine::Audio::Bus::Effects,
            float gain = 1.f);

    private:
        static constexpr char FolderSeparator = '|';

        std::shared_ptr<Formats::MdkData> _data;
        Engine::Platform::Window& _window;
        Engine::Render::Renderer& _renderer;
        Engine::Audio::AudioDevice& _audio;
        Engine::Platform::Input& _input;
        Flow::Settings& _settings;
        std::string _userFolder;
        std::shared_ptr<Game::Mods::ModImages> _menuImages;
        std::string _menuFolders;
    };

    // The original's 600 x 360 screen on the canvas's safe area (clear of cut-outs): scaled
    // to fit, centred, black bars around it, never cut (menus, statistics, videos). The loading
    // screen fills a 4:3 canvas's height as the original's, its sides cut; other screens fit it too.
    //   canvas ┌──────────────────────────┐
    //          │   ┌──────────────────┐   │  origin + view point × scale
    //          │   │  600 x 360 view  │   │
    //          │   └──────────────────┘   │
    //          └──────────────────────────┘
    class ScreenView
    {
    public:
        static constexpr Vector2 Size{600.f, 360.f};

        enum class Fit { Inside, Height };

        explicit ScreenView(Engine::Render::Renderer& renderer) : _renderer(renderer) {}

        // Read-only outside (C#: private set).
        float Scale() const { return _scale; }
        Vector2 Origin() const { return _origin; }

        // Places a `size` view (the 600 x 360 screen, or a movie's) on the canvas.
        void Layout(Fit fit, std::optional<Vector2> size = std::nullopt);

        // The safe area made even about the canvas's centre (each side's inset the larger
        // of its and the opposite side's): a cut-out on one side doesn't push the screen off centre.
        //   canvas 800 wide, safe 69 ─ 774 (cut-out left) ─► 69 ─ 731
        static RectangleF Centred(RectangleF safe, Vector2 canvas);

        // A view's scale and origin on a canvas: inside it, or (a 4:3 canvas) to its height.
        //   600 x 360 on 480 x 360 (4:3), Height: × 1,    x from -60 (sides cut)
        //   600 x 360 on 576 x 360 (16:10):       × 0.96, y from 7.2 (bars)
        static std::tuple<float, Vector2> Place(Fit fit, Vector2 view, Vector2 canvas);

        Vector2 ToCanvas(Vector2 point) const { return _origin + point * _scale; }

        RectangleF ToCanvas(RectangleF area) const;

        // The pointer (window coordinates) on the view.
        Vector2 Pointer(Engine::Platform::Input& input) const;

        void Fill(RectangleF area, Vector4 colour);

        // The whole canvas.
        void FillCanvas(Vector4 colour);

        // A text from x along the baseline y (view coordinates), scaled about the baseline.
        void Text(Hud::FontView& font, std::span<const byte> text, float x, float y, float scale = 1.f, float alpha = 1.f);

    private:
        // The 4:3 canvas the loading screen's height fills (within a pixel's rounding).
        static constexpr float FourByThree = 4.f / 3.f;
        static constexpr float AspectTolerance = 0.01f;

        Engine::Render::Renderer& _renderer;
        float _scale = 1.f;
        Vector2 _origin;
    };

    // The interface fonts of MDKFONT.FTI in its SYS_PAL colours.
    class Fonts
    {
    public:
        std::shared_ptr<Hud::FontView> Big;
        std::shared_ptr<Hud::FontView> Small;

        // The fonts; `mods`' images of their atlases replace them.
        Fonts(Engine::Render::Renderer& renderer, const Formats::Fti& fti, const Formats::Palette* palette = nullptr,
            const std::shared_ptr<Game::Mods::ModImages>& mods = nullptr);

    private:
        static constexpr int BigSpace = 6;
        static constexpr int SmallSpace = 4;
    };

    // An image of palette indices with its palette on the GPU; both can change (videos, fades).
    class PalettedImage
    {
    public:
        int Width = 0;
        int Height = 0;

        Vector2 Size() const { return Vector2(static_cast<float>(Width), static_cast<float>(Height)); }

        PalettedImage(Engine::Render::Renderer& renderer, int width, int height, std::span<const byte> indices,
            std::span<const byte> paletteRgb);

        // An image of a texture (BNI, LBB) with a palette of 768 RGB bytes; a mod's image of
        // its name replaces it (CanvasImages).
        static std::shared_ptr<PalettedImage> Of(Engine::Render::Renderer& renderer, const Formats::Texture& texture,
            std::span<const byte> paletteRgb, const std::shared_ptr<Game::Mods::ModImages>& mods = nullptr);

        void SetIndices(std::span<const byte> indices);

        void SetPalette(std::span<const byte> paletteRgb);

        // Draws it at `at` of the view, at its size.
        void Draw(const ScreenView& view, Vector2 at);

    private:
        static constexpr int PaletteBytes = 256 * 4;

        Engine::Render::Renderer& _renderer;
        int _texture = 0;
        int _palette = 0;
    };
}
