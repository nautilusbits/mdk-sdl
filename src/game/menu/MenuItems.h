#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Drawing.h"
#include "core/Numerics.h"
#include "engine/audio/AudioDevice.h"
#include "engine/platform/Input.h"
#include "game/flow/Settings.h"

namespace Mdk::Formats
{
    class Sni;
}

namespace Mdk::Game::HdTextures
{
    class HdJob;
}

namespace Mdk::Game::Hud
{
    class TouchView;
}

namespace Mdk::Game::Menu
{
    class Fonts;
    class TouchLayoutPage;
    class Ui;

    // A list of menu items drawn like the original's menus (menu_items.gd, menu_entry.gd,
    // 0x42c374): FONTBIG in SYS_PAL, unselected items at 65 %, the selected one growing
    // to full size in 5 ticks. Rows of 36 from y 5 on the 600 x 360 view (baselines at 31 + 36 x row,
    // 0x4265c0); longer pages get lower rows, and below 24 the small font. Also the options and
    // controls pages shared by the main menu and the pause menu.
    //   main menu: a column at the view's left edge, items centred on its width
    //   other pages: centred on x 300
    class MenuItems
    {
    public:
        // Where the column of items stands on the view.
        enum class Align { Left, Centre };

        Align Alignment = Align::Centre;
        // The row the first item stands in.
        int FirstRow = 0;
        bool Visible = true;

        MenuItems(Ui& ui, std::shared_ptr<Fonts> fonts, float clickGain = 1.f);

        // The texts of the page, for tests.
        std::string Describe() const;

        void Clear();

        void AddItem(const std::string& text, std::function<void()> press);

        // An item that can't be chosen (shown greyed).
        void AddDisabled(const std::string& text);

        // A line at full size that can't be selected ("Really Quit?", list titles).
        void AddTitle(const std::string& text);

        // An item that changes a setting by a step: a click or Right goes on, a right click or
        // Left back. `text` gives its label.
        void AddOption(std::function<std::string()> text, std::function<void(int)> change);

        bool HandlesBack() const { return static_cast<bool>(_back); }

        // Selects the item at `index` (the first selectable one after it).
        void Select(int index);

        // A frame: keys, the mouse and the items' growth.
        void Update(float delta);

        // A touch screen's row for a page whose fitted rows take the small font (the keys' pages):
        // grown by `enlargement` as the short pages', but so that at least MinScrollRows show; the
        // rest scroll. 0: the page fits, it doesn't scroll.
        //   phone (× 1.4), 18 items: min(50.4, 350 / 8) = 43.75, 8 rows shown
        //   tablet (× 1):  18 items: 36, 9 rows shown
        static float ScrollRow(int entries, int firstRow, float enlargement);

        // The whole rows a scrolling page shows below `firstRow`.
        static int ShownRows(float row, int firstRow);

        // The height of a row for a page of `entries` items: 36, less when they don't fit.
        static float RowFor(int entries);

        // The height of a row grown by `enlargement` as far as the page's rows (from `firstRow`)
        // fit, never below RowFor(int).
        //   phone, 6 items: min(36 × 1.4, 350 / 6) = 50.4
        static float RowFor(int entries, int firstRow, float enlargement);

        // How much a touch screen's rows grow: to TouchRowMm from a row of `rowMm`, at most
        // MaxEnlargement (1: no touch screen, or an unknown display).
        //   phone  1080 px, 16.5 px/mm: a row of 108 px is 6.5 mm ─► × 1.4
        //   tablet 1800 px, 12.6 px/mm: 173 px, 13.7 mm ─► × 1
        static float Enlargement(float rowMm);

        // Rows below 24 use the small font.
        static bool UsesBigFont(float row) { return row >= SmallFontRow; }

        // A row's top on the view.
        static float RowTop(float row, int index) { return Top + row * index; }

        void Draw();

        // --- Options --------------------------------------------------------------------------------

        // The options (see Settings), a page per kind; each change is applied and saved at once.
        // Esc goes up a page.
        //   Options ─┬─ Display   mode, resolution, render scale, VSync, frame limit, GPU backend
        //            ├─ Graphics  look, anti-aliasing, gore
        //            ├─ Audio     volumes, music filter
        //            ├─ Keyboard & mouse  mouse, key bindings
        //            ├─ Gamepad   look, button bindings
        //            ├─ Touch     touch controls, opacity, layout editor (a touch screen only)
        //            ├─ Game      difficulty
        //            └─ Mods      mods on/off, Make HD textures (desktop), Import from folder (Android)
        void ShowOptions(std::function<void()> back);

        // The mouse, and the key bindings: choosing a binding waits for a key or a mouse
        // button (Esc cancels).
        void ShowControls(std::function<void()> back);

        // The gamepad's look, and its button bindings: choosing a binding waits for a button
        // (a key cancels: B and Start are buttons too). The sticks and Start (pause) are fixed.
        void ShowGamepad(std::function<void()> back);

        // The on-screen controls: when they show (TouchButtons, a touch screen only), their
        // opacity, and the layout editor.
        void ShowTouch(std::function<void()> back);

        // The touch layout editor (TouchEditor); Back cancels it.
        void ShowTouchLayout(std::function<void()> back);

        // The next opacity of the list (wrapping; from full when it isn't in it).
        static int OpacityStep(int value, int step);

        // Volumes go by 10 and wrap (0-100).
        static int VolumeStep(int value, int step)
        {
            return Wrap(value + step * VolumeIncrement, Flow::Settings::MaxVolume + VolumeIncrement);
        }

        // The next sensitivity of the list (wrapping; from 1 when it isn't in it).
        static float SensitivityStep(float value, int step);

        // The next gamma of the list (wrapping; from 1 when it isn't in it).
        static float GammaStep(float value, int step);

    private:
        enum class Kind { Item, Title };

        // What a binding waits for: a key or mouse button, or a gamepad button.
        enum class Device { Keyboard, Gamepad };

        static constexpr float Top = 5.f;
        static constexpr float Row = 36.f;
        // The baseline is 26 below the row's top.
        static constexpr float Baseline = 26.f / 36.f;
        static constexpr float SmallFontRow = 24.f;
        // A touch screen's rows grow towards this height (millimetres), at most by the factor.
        static constexpr float TouchRowMm = 10.f;
        static constexpr float MaxEnlargement = 1.4f;
        // A touch screen's long page scrolls; it shows at least this many rows.
        static constexpr int MinScrollRows = 8;
        // The scroll bar on the view's right edge (view units) and its colours.
        static constexpr float ScrollBarWidth = 4.f;
        static constexpr Vector4 ScrollTrack{1.f, 1.f, 1.f, 0.15f};
        static constexpr Vector4 ScrollThumb{1.f, 1.f, 1.f, 0.6f};
        static constexpr float Small = 0.65f;
        // 0.35 x 0.2 per tick.
        static constexpr float GrowRate = 0.07f * 30.f;
        static constexpr float DisabledAlpha = 0.5f;
        static constexpr int VolumeIncrement = 10;
        static constexpr std::array<float, 9> Sensitivities{0.25f, 0.5f, 0.75f, 1.f, 1.25f, 1.5f, 2.f, 2.5f, 3.f};
        static constexpr int DefaultSensitivity = 3;
        // Longer mod names are cut (the row's width).
        static constexpr int MaxModName = 24;
        static constexpr std::array<const char*, 3> DifficultyNames{"Easy", "Normal", "Hard"};
        static constexpr std::array<const char*, 3> AntiAliasingNames{"Off", "2x", "4x"};
        // The stereo sliders: a held Left/Right waits, then steps again.
        static constexpr float RepeatWait = 0.4f;
        static constexpr float RepeatRate = 0.06f;
        static constexpr float SeparationStep = 0.05f;
        static constexpr float ConvergenceRate = 1.15f;
        // Enum.GetValues<Graphics>().Length.
        static constexpr int GraphicsCount = 2;
        // The options' sounds (0x42bb6c): the OPTSONG loop on the music bus and OPTBUTT on each change.
        static constexpr const char* OptionSounds = "MISC/MDKSOUND.SNI";
        static constexpr std::array<int, 4> TouchOpacities{Flow::Settings::MinOpacity, 50, 75, Flow::Settings::MaxOpacity};
        static constexpr std::array<const char*, 3> FullscreenNames{"Windowed", "Fullscreen", "Exclusive"};
        // Enum.GetValues<VSync>().Length.
        static constexpr int VSyncCount = 3;
        // The gammas by 0.1, Renderer.MinGamma to Renderer.MaxGamma.
        static constexpr std::array<float, 16> Gammas{0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f,
            1.6f, 1.7f, 1.8f, 1.9f, 2.f};

        struct Entry
        {
            Entry(std::string text, Kind kind, std::function<void()> press, std::function<void(int)> change)
                : Text(std::move(text)), Kind(kind), Press(std::move(press)), Change(std::move(change)) {}

            std::string Text;
            MenuItems::Kind Kind;
            std::function<void()> Press;
            // An option: changed by a step (+1 or -1).
            std::function<void(int)> Change;
            bool Disabled = false;
            // An option's label, made again when an option changes.
            std::function<std::string()> Label;
            float Grow = Small;
            RectangleF Area;

            bool Selectable() const { return Kind == MenuItems::Kind::Item && !Disabled; }
        };

        Ui& _ui;
        std::shared_ptr<Fonts> _fonts;
        std::vector<std::shared_ptr<Entry>> _entries;
        std::shared_ptr<Engine::Audio::Sound> _click;
        float _clickGain;
        std::shared_ptr<Engine::Audio::Sound> _song;
        std::shared_ptr<Engine::Audio::Sound> _change;
        int _songVoice = 0;
        int _selected = -1;
        // The controls page waits for a key, or a gamepad button, for this action (from the next
        // frame on).
        std::optional<Engine::Platform::Key> _waiting;
        Device _waitingDevice{};
        std::shared_ptr<Entry> _waitingEntry;
        bool _armed = false;
        // The time to the next step of an option whose Left/Right is held.
        float _repeat = 0;
        // The HD textures being made, and the page's lines that follow it.
        std::shared_ptr<HdTextures::HdJob> _job;
        std::shared_ptr<Entry> _jobLine;
        std::shared_ptr<Entry> _jobBack;
        // The touch layout editor while it's the page, and its drawing (made once).
        std::shared_ptr<TouchLayoutPage> _touchPage;
        std::shared_ptr<Hud::TouchView> _touchView;
        // The rows' height as the original's (it picks the font), and the texts' scale on
        // it (1, larger on a touch screen's grown rows).
        float _originalRow = 0;
        float _textScale = 1.f;
        // A scrolling page (a touch screen's long one): its row, the first row shown, how many
        // show, and a drag in progress (where it started, and whether it moved: a tap presses on release).
        float _row = 0;
        int _scrollRow = 0;
        int _shownRows = 0;
        bool _reveal = false;
        std::optional<Vector2> _dragStart;
        int _dragScroll = 0;
        bool _dragged = false;
        // Esc's page: the one above (the options' pages), or null when the screen handles Esc.
        std::function<void()> _back;

        bool Scrolls() const { return _shownRows > 0; }

        static std::shared_ptr<Engine::Audio::Sound> SniSound(const Formats::Sni& sni, const std::string& name,
            Engine::Audio::Looping looping);

        // A new page; the options' song plays on across their pages.
        void ClearPage();

        void Add(const std::shared_ptr<Entry>& entry);

        static float MoveToward(float value, float target, float step);

        void HandleInput(float delta);

        // The next selectable item up or down.
        void Move(int step);

        // A scrolling page's pointer: a drag scrolls by whole rows, a tap (released where it
        // didn't drag) selects and presses its item. Returns whether a tap pressed one.
        bool Drag(Engine::Platform::Input& input);

        int ClampScroll(int row) const;

        void Hover(Vector2 point);

        void Click();

        // Rows on the view (0x4265c0).
        void Layout();

        // The selection changed by keys or a new page: a scrolling list shows it.
        void Reveal();

        // The growth of this window's rows (Enlargement(float)).
        float Enlargement() const;

        // A scrolling page's bar on the right: the list's height, the shown rows' part lit.
        void DrawScrollBar();

        // A sub-page of the options: Esc and its last item go back.
        void EndPage(std::function<void()> back);

        // The page's items, for tests.
        void Print() const;

        // The window and the frame (DisplayMenu: a phone shows fewer).
        void ShowDisplay(std::function<void()> back);

        // The window's size, the exclusive mode's, or the desktop's (borderless fullscreen).
        std::string ResolutionText() const;

        void ResolutionStep(int step);

        // VSync as chosen, and the mode used when the display falls back to another.
        std::string VSyncText() const;

        // The backend chosen (Auto names SDL's choice); a change waits for the next start.
        std::string BackendText() const;

        void BackendStep(int step);

        // The levels' look, its anti-aliasing, and gore.
        void ShowGraphics(std::function<void()> back);

        void ShowAudio(std::function<void()> back);

        void ShowGame(std::function<void()> back);

        // The 3D stereo page: the layout of the two eyes, their separation and the distance
        // where their images converge (the same point on the screen in both; sliders: hold Left or
        // Right to step again). Stereo.
        void ShowStereo(std::function<void()> back);

        // A convergence change: each step scales the distance (they span an order of magnitude).
        static float ConvergenceStep(float value, int step);

        // The convergence distance ("far" at the most: parallel rays).
        static std::string ConvergenceText(float value);

        // The mods found (ModCatalog), each switched on or off; the enhanced look takes them from
        // the next level (2D images at once). Then the HD textures' maker (desktop: Android never
        // makes them) and, on Android, the import of the MDK folder again.
        void ShowMods(std::function<void()> back);

        // Makes the HD textures (HdGenerator, minutes on a GPU) while showing its progress;
        // leaving the page cancels it.
        void ShowHdTextures(std::function<void()> back);

        // The progress line, and "Back" once the job is over.
        void ShowJob();

        // The Touch page: a touch screen's, or the desktop's with --touch-layout (tests).
        bool HasTouchPage() const;

        // The key or button pressed after the item was chosen becomes the binding.
        void WaitForControl(Engine::Platform::Input& input);

        // A gamepad button binds (not Start: it stays pause); a key or mouse button cancels.
        void WaitForButton(Engine::Platform::Input& input, Engine::Platform::Key key);

        std::string BindingText(Engine::Platform::Key key, const std::string& name) const;

        std::string PadBindingText(Engine::Platform::Key key, const std::string& name) const;

        static int Wrap(int value, int count) { return ((value % count) + count) % count; }

        static std::string OnOff(bool value);
    };
}
