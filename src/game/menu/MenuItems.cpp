#include "game/menu/MenuItems.h"

#include <algorithm>
#include <format>

#include "core/Console.h"
#include "core/Files.h"
#include "engine/platform/Display.h"
#include "engine/platform/Window.h"
#include "engine/render/GpuBackend.h"
#include "engine/render/Renderer.h"
#include "formats/Fti.h"
#include "formats/MdkData.h"
#include "formats/Sni.h"
#include "game/dev_tools/DevSession.h"
#include "game/hd_textures/HdGenerator.h"
#include "game/hd_textures/HdJob.h"
#include "game/hd_textures/HdMenu.h"
#include "game/hud/FontView.h"
#include "game/hud/TouchView.h"
#include "game/kurt/Inventory.h"
#include "game/menu/DisplayMenu.h"
#include "game/menu/MenuCursor.h"
#include "game/menu/TouchLayoutPage.h"
#include "game/menu/Ui.h"
#include "game/mods/ModCatalog.h"
#include "game/mods/ModInfo.h"

namespace Mdk::Game::Menu
{
    using Engine::Audio::Bus;
    using Engine::Audio::Looping;
    using Engine::Platform::Fullscreen;
    using Engine::Platform::Key;
    using Engine::Platform::MenuKey;
    using Engine::Platform::Pointer;
    using Engine::Render::Renderer;

    namespace
    {
        // The enums' C# names ($"{value}").
        std::string NameOf(Flow::Graphics value)
        {
            return value == Flow::Graphics::Enhanced ? "Enhanced" : "Original";
        }

        std::string NameOf(Engine::Render::VSync value)
        {
            switch (value)
            {
                case Engine::Render::VSync::On:
                    return "On";
                case Engine::Render::VSync::Off:
                    return "Off";
                default:
                    return "Adaptive";
            }
        }

        std::string NameOf(Engine::Platform::TouchButtons value)
        {
            switch (value)
            {
                case Engine::Platform::TouchButtons::Auto:
                    return "Auto";
                case Engine::Platform::TouchButtons::Off:
                    return "Off";
                default:
                    return "On";
            }
        }

        // Enum.GetValues<TouchButtons>().Length.
        constexpr int TouchButtonsCount = 3;

        std::vector<byte> Ascii(const std::string& text)
        {
            return std::vector<byte>(text.begin(), text.end());
        }

        template <typename T, size_t N>
        int IndexOf(const std::array<T, N>& values, T value)
        {
            auto found = std::find(values.begin(), values.end(), value);
            return found == values.end() ? -1 : static_cast<int>(found - values.begin());
        }
    }

    MenuItems::MenuItems(Ui& ui, std::shared_ptr<Fonts> fonts, float clickGain)
        : _ui(ui), _fonts(std::move(fonts)), _clickGain(clickGain)
    {
        _click = Ui::SoundOf(ui.Fti->GetBytes("SND_PUSH"));
        MenuCursor::Apply(ui);
        auto path = ui.Data()->PathOf(OptionSounds);
        if (!File::Exists(path))
        {
            return;
        }

        auto sounds = Formats::Sni::Load(path);
        _song = SniSound(*sounds, "OPTSONG", Looping::Forever);
        _change = SniSound(*sounds, "OPTBUTT", Looping::Once);
    }

    std::shared_ptr<Engine::Audio::Sound> MenuItems::SniSound(const Formats::Sni& sni, const std::string& name, Looping looping)
    {
        for (const auto& [key, entry] : sni.Entries)
        {
            if (key == name)
            {
                return Ui::SoundOf(sni.GetBytes(entry), looping);
            }
        }

        return nullptr;
    }

    std::string MenuItems::Describe() const
    {
        std::vector<std::string> texts;
        for (const auto& e : _entries)
        {
            texts.push_back(e->Text);
        }

        return Str::Join(" | ", texts);
    }

    void MenuItems::Clear()
    {
        _ui.Audio().Stop(_songVoice);
        _songVoice = 0;
        ClearPage();
    }

    void MenuItems::ClearPage()
    {
        // Leaving the progress page stops the HD textures.
        if (_job != nullptr)
        {
            _job->Cancel();
        }

        _job = nullptr;
        _touchPage = nullptr;
        _back = nullptr;
        Alignment = Align::Centre;
        FirstRow = 0;
        _entries.clear();
        _selected = -1;
        _waiting = std::nullopt;
        _repeat = 0.f;
        _scrollRow = 0;
        _reveal = true;
        _dragStart = std::nullopt;
    }

    void MenuItems::AddItem(const std::string& text, std::function<void()> press)
    {
        Add(std::make_shared<Entry>(text, Kind::Item, std::move(press), nullptr));
    }

    void MenuItems::AddDisabled(const std::string& text)
    {
        auto entry = std::make_shared<Entry>(text, Kind::Item, nullptr, nullptr);
        entry->Disabled = true;
        Add(entry);
    }

    void MenuItems::AddTitle(const std::string& text)
    {
        auto entry = std::make_shared<Entry>(text, Kind::Title, nullptr, nullptr);
        entry->Grow = 1.f;
        Add(entry);
    }

    void MenuItems::AddOption(std::function<std::string()> text, std::function<void(int)> change)
    {
        // Options may depend on each other (the resolution on the display mode): all are relabelled.
        auto step = [this, change](int amount)
        {
            change(amount);
            _ui.ApplySettings();
            for (const auto& option : _entries)
            {
                option->Text = option->Label ? option->Label() : option->Text;
            }

            _ui.Play(_change);
        };

        auto entry = std::make_shared<Entry>(text(), Kind::Item, [step] { step(1); }, step);
        Add(entry);

        entry->Label = text;
    }

    void MenuItems::Add(const std::shared_ptr<Entry>& entry)
    {
        _entries.push_back(entry);
        if (_selected < 0 && entry->Selectable())
        {
            _selected = static_cast<int>(_entries.size()) - 1;
        }
    }

    void MenuItems::Select(int index)
    {
        for (auto i = index; i < static_cast<int>(_entries.size()); i++)
        {
            if (_entries[i]->Selectable())
            {
                _selected = i;
                _reveal = true;
                return;
            }
        }
    }

    void MenuItems::Update(float delta)
    {
        ShowJob();
        Layout();
        if (Visible)
        {
            HandleInput(delta);
        }

        for (size_t i = 0; i < _entries.size(); i++)
        {
            auto& entry = *_entries[i];
            auto target = static_cast<int>(i) == _selected || entry.Kind == Kind::Title ? 1.f : Small;
            entry.Grow = MoveToward(entry.Grow, target, GrowRate * delta);
        }
    }

    float MenuItems::MoveToward(float value, float target, float step)
    {
        return value < target ? MathF::Min(value + step, target) : MathF::Max(value - step, target);
    }

    void MenuItems::HandleInput(float delta)
    {
        auto& input = _ui.Input();
        if (_waiting.has_value())
        {
            WaitForControl(input);
            return;
        }

        // The touch layout editor has its own Back (it may ask first).
        if (_touchPage != nullptr)
        {
            auto page = _touchPage;
            page->Update();
            return;
        }

        if (_back && input.WasPressed(MenuKey::Back))
        {
            Click();
            // A copy: the page it shows replaces _back.
            auto back = _back;
            back();
            return;
        }

        if (input.WasPressed(MenuKey::Up))
        {
            Move(-1);
        }

        if (input.WasPressed(MenuKey::Down))
        {
            Move(1);
        }

        if (Scrolls() && Drag(input))
        {
            return;
        }

        if (!Scrolls() && (input.PointerMoved() || input.WasClicked(Pointer::Left) || input.WasClicked(Pointer::Right)))
        {
            Hover(_ui.View->Pointer(input));
        }

        if (_selected < 0 || _selected >= static_cast<int>(_entries.size()))
        {
            return;
        }

        // A copy: a press may show another page.
        auto selected = _entries[_selected];
        auto pointed = !Scrolls() && selected->Area.Contains(_ui.View->Pointer(input).X, _ui.View->Pointer(input).Y);
        if (input.WasPressed(MenuKey::Accept) || (input.WasClicked(Pointer::Left) && pointed))
        {
            Click();
            if (selected->Press)
            {
                selected->Press();
            }

            return;
        }

        if (!selected->Change)
        {
            return;
        }

        if (input.WasPressed(MenuKey::Right))
        {
            selected->Change(1);
            _repeat = RepeatWait;
        }
        else if (input.WasPressed(MenuKey::Left) || (input.WasClicked(Pointer::Right) && pointed))
        {
            selected->Change(-1);
            _repeat = RepeatWait;
        }
        else
        {
            // Held, an option steps on (the stereo sliders: separations and distances).
            auto held = input.IsDown(MenuKey::Right) ? 1 : input.IsDown(MenuKey::Left) ? -1 : 0;
            if (held == 0)
            {
                _repeat = 0.f;
            }
            else
            {
                _repeat -= delta;
                if (_repeat <= 0.f)
                {
                    selected->Change(held);
                    _repeat = RepeatRate;
                }
            }
        }
    }

    void MenuItems::Move(int step)
    {
        for (auto i = _selected + step; i >= 0 && i < static_cast<int>(_entries.size()); i += step)
        {
            if (!_entries[i]->Selectable())
            {
                continue;
            }

            _selected = i;
            _reveal = true;
            Click();
            return;
        }
    }

    bool MenuItems::Drag(Engine::Platform::Input& input)
    {
        auto point = _ui.View->Pointer(input);
        if (input.WasClicked(Pointer::Left))
        {
            _dragStart = point;
            _dragScroll = _scrollRow;
            _dragged = false;
            return false;
        }

        if (!_dragStart.has_value())
        {
            return false;
        }

        auto start = *_dragStart;
        if (input.IsDown(Pointer::Left))
        {
            // Moved off by half a row any way, it's a drag (no press on release).
            auto rows = ToInt(MathF::Round((start.Y - point.Y) / _row));
            _dragged |= Vector2::Distance(start, point) > _row / 2.f;
            _scrollRow = ClampScroll(_dragScroll + rows);
            return false;
        }

        _dragStart = std::nullopt;
        if (_dragged)
        {
            return false;
        }

        auto index = -1;
        for (size_t i = 0; i < _entries.size(); i++)
        {
            if (_entries[i]->Selectable() && _entries[i]->Area.Contains(point.X, point.Y))
            {
                index = static_cast<int>(i);
                break;
            }
        }

        if (index < 0)
        {
            return false;
        }

        _selected = index;
        Click();
        // A copy: a press may show another page.
        auto entry = _entries[index];
        if (entry->Press)
        {
            entry->Press();
        }

        return true;
    }

    int MenuItems::ClampScroll(int row) const
    {
        return Math::Clamp(row, 0, Math::Max(static_cast<int>(_entries.size()) - _shownRows, 0));
    }

    void MenuItems::Hover(Vector2 point)
    {
        for (size_t i = 0; i < _entries.size(); i++)
        {
            if (!_entries[i]->Selectable() || !_entries[i]->Area.Contains(point.X, point.Y) || static_cast<int>(i) == _selected)
            {
                continue;
            }

            _selected = static_cast<int>(i);
            Click();
            return;
        }
    }

    void MenuItems::Click()
    {
        _ui.Play(_click, Bus::Effects, _clickGain);
    }

    void MenuItems::Layout()
    {
        if (_entries.empty())
        {
            return;
        }

        auto enlargement = Enlargement();
        auto entries = static_cast<int>(_entries.size());
        auto scrollRow = _ui.Window().HasTouchScreen() ? ScrollRow(entries, FirstRow, enlargement) : 0.f;
        _originalRow = scrollRow > 0.f ? Row : RowFor(entries);
        auto row = _row = scrollRow > 0.f ? scrollRow : RowFor(entries, FirstRow, enlargement);
        _shownRows = scrollRow > 0.f ? ShownRows(row, FirstRow) : 0;
        Reveal();
        _textScale = row / _originalRow;
        auto& font = UsesBigFont(_originalRow) ? *_fonts->Big : *_fonts->Small;
        auto widestText = 0;
        for (const auto& e : _entries)
        {
            widestText = Math::Max(widestText, font.Width(Ascii(e->Text)));
        }

        auto width = widestText * _textScale;

        // A scrolling page's big font may be wider than the view: its texts shrink to fit beside the bar.
        auto widest = ScreenView::Size.X - ScrollBarWidth * 4.f;
        if (Scrolls() && width > widest)
        {
            _textScale *= widest / width;
            width = widest;
        }

        auto x = Alignment == Align::Left ? 0.f : (ScreenView::Size.X - width) / 2.f;
        for (auto i = 0; i < entries; i++)
        {
            // A scrolling page's rows off the list have no area (not drawn, not hit).
            auto shown = i - _scrollRow;
            _entries[i]->Area = Scrolls() && (shown < 0 || shown >= _shownRows) ? RectangleF::Empty()
                : RectangleF(x, RowTop(row, (Scrolls() ? shown : i) + FirstRow), width, row);
        }
    }

    void MenuItems::Reveal()
    {
        if (!Scrolls())
        {
            _scrollRow = 0;
            return;
        }

        _scrollRow = ClampScroll(_scrollRow);
        if (!_reveal || _selected < 0)
        {
            return;
        }

        _reveal = false;
        _scrollRow = ClampScroll(Math::Clamp(_scrollRow, _selected - _shownRows + 1, _selected));
    }

    float MenuItems::ScrollRow(int entries, int firstRow, float enlargement)
    {
        if (UsesBigFont(RowFor(entries + firstRow)))
        {
            return 0.f;
        }

        return MathF::Max(Row, MathF::Min(Row * enlargement, (ScreenView::Size.Y - Top * 2.f) / (MinScrollRows + firstRow)));
    }

    int MenuItems::ShownRows(float row, int firstRow)
    {
        return Math::Max(ToInt((ScreenView::Size.Y - Top * 2.f) / row) - firstRow, 1);
    }

    float MenuItems::RowFor(int entries)
    {
        return MathF::Min(Row, (ScreenView::Size.Y - Top * 2.f) / Math::Max(entries, 1));
    }

    float MenuItems::RowFor(int entries, int firstRow, float enlargement)
    {
        return MathF::Max(RowFor(entries),
            MathF::Min(Row * enlargement, (ScreenView::Size.Y - Top * 2.f) / Math::Max(entries + firstRow, 1)));
    }

    float MenuItems::Enlargement(float rowMm)
    {
        return rowMm > 0.f ? Math::Clamp(TouchRowMm / rowMm, 1.f, MaxEnlargement) : 1.f;
    }

    float MenuItems::Enlargement() const
    {
        auto& window = _ui.Window();
        auto unitsPerMm = window.UnitsPerMm();
        if (!window.HasTouchScreen() || unitsPerMm <= 0.f)
        {
            return 1.f;
        }

        // A view unit in window units: the view's scale on the canvas, the canvas on the window.
        auto unit = _ui.View->Scale() * window.Size().Height / Renderer::CanvasHeight;
        return Enlargement(Row * unit / unitsPerMm);
    }

    void MenuItems::Draw()
    {
        if (Visible && _touchPage != nullptr)
        {
            _touchPage->Draw();
            return;
        }

        if (!Visible || _entries.empty())
        {
            return;
        }

        // A press may have just opened another page: its items have no rows yet.
        Layout();
        auto& font = UsesBigFont(_originalRow) ? *_fonts->Big : *_fonts->Small;
        auto baseline = MathF::Round(_row * Baseline);
        for (const auto& entry : _entries)
        {
            if (entry->Area.Height <= 0.f)
            {
                continue;
            }

            auto text = Ascii(entry->Text);
            auto grow = entry->Grow * _textScale;
            auto x = entry->Area.X + (entry->Area.Width - font.Width(text) * grow) / 2.f;
            auto alpha = entry->Disabled ? DisabledAlpha : 1.f;
            _ui.View->Text(font, text, MathF::Round(x), entry->Area.Y + baseline, grow, alpha);
        }

        DrawScrollBar();
    }

    void MenuItems::DrawScrollBar()
    {
        if (!Scrolls())
        {
            return;
        }

        auto top = RowTop(_row, FirstRow);
        auto height = _row * _shownRows;
        auto x = ScreenView::Size.X - ScrollBarWidth * 2.f;
        _ui.View->Fill(RectangleF(x, top, ScrollBarWidth, height), ScrollTrack);
        auto share = height / static_cast<float>(_entries.size());
        _ui.View->Fill(RectangleF(x, top + share * _scrollRow, ScrollBarWidth, share * _shownRows), ScrollThumb);
    }

    // --- Options --------------------------------------------------------------------------------

    void MenuItems::ShowOptions(std::function<void()> back)
    {
        using Show = void (MenuItems::*)(std::function<void()>);

        ClearPage();
        _back = back;
        std::vector<std::pair<std::string, Show>> pages{
            {"Display", &MenuItems::ShowDisplay}, {"Graphics", &MenuItems::ShowGraphics}, {"3D Stereo", &MenuItems::ShowStereo},
            {"Audio", &MenuItems::ShowAudio}, {"Keyboard & mouse", &MenuItems::ShowControls},
            {"Gamepad", &MenuItems::ShowGamepad}, {"Game", &MenuItems::ShowGame}, {"Mods", &MenuItems::ShowMods},
        };
        if (HasTouchPage())
        {
            auto gamepad = std::find_if(pages.begin(), pages.end(), [](const auto& p) { return p.second == &MenuItems::ShowGamepad; });
            pages.insert(gamepad + 1, {"Touch", &MenuItems::ShowTouch});
        }

        for (size_t i = 0; i < pages.size(); i++)
        {
            auto [name, show] = pages[i];
            auto row = static_cast<int>(i);
            AddItem(name, [this, show, back, row]
            {
                (this->*show)([this, back, row]
                {
                    ShowOptions(back);
                    Select(row);
                });
            });
        }

        AddItem("Back", back);
        if (_songVoice == 0)
        {
            _songVoice = _ui.Play(_song, Bus::Music);
        }

        Print();
    }

    void MenuItems::EndPage(std::function<void()> back)
    {
        _back = back;
        AddItem("Back", back);
        Print();
    }

    void MenuItems::Print() const
    {
        Console::WriteLine(std::format("Menu: {}", Describe()));
    }

    void MenuItems::ShowDisplay(std::function<void()> back)
    {
        ClearPage();
        auto& s = _ui.Settings();
        for (auto item : DisplayMenu::Items(HdTextures::HdMenu::Current()))
        {
            switch (item)
            {
                case DisplayItem::Mode:
                    AddOption([&s] { return std::format("Display mode: {}", FullscreenNames[static_cast<int>(s.Fullscreen)]); },
                        [&s](int step)
                        {
                            s.Fullscreen = static_cast<Fullscreen>(Wrap(static_cast<int>(s.Fullscreen) + step,
                                static_cast<int>(FullscreenNames.size())));
                        });
                    break;
                case DisplayItem::Resolution:
                    AddOption([this] { return ResolutionText(); }, [this](int step) { ResolutionStep(step); });
                    break;
                case DisplayItem::Scale:
                    AddOption([&s]
                        {
                            return s.RenderScale == Renderer::AutoScale ? std::string("Render scale: Auto")
                                : std::format("Render scale: {}%", s.RenderScale);
                        },
                        [&s](int step)
                        {
                            const auto& scales = Renderer::Scales;
                            s.RenderScale = scales[Wrap(IndexOf(scales, s.RenderScale) + step, static_cast<int>(scales.size()))];
                        });
                    break;
                case DisplayItem::VSync:
                    AddOption([this] { return VSyncText(); },
                        [&s](int step)
                        {
                            s.VSync = static_cast<Engine::Render::VSync>(Wrap(static_cast<int>(s.VSync) + step, VSyncCount));
                        });
                    break;
                case DisplayItem::FrameLimit:
                    AddOption([&s]
                        {
                            return std::format("Frame limit: {}",
                                s.FrameLimit == Engine::Platform::FrameLimiter::Off ? std::string("Off") : std::to_string(s.FrameLimit));
                        },
                        [&s](int step)
                        {
                            const auto& limits = Engine::Platform::FrameLimiter::Limits;
                            s.FrameLimit = limits[Wrap(IndexOf(limits, s.FrameLimit) + step, static_cast<int>(limits.size()))];
                        });
                    break;
                case DisplayItem::Backend:
                    AddOption([this] { return BackendText(); }, [this](int step) { BackendStep(step); });
                    break;
                case DisplayItem::Gamma:
                    AddOption([&s] { return std::format("Gamma: {:.1f}", s.Gamma); },
                        [&s](int step) { s.Gamma = GammaStep(s.Gamma, step); });
                    break;
            }
        }

        EndPage(back);
    }

    std::string MenuItems::ResolutionText() const
    {
        auto& s = _ui.Settings();
        auto desktop = _ui.Window().Desktop().Size();
        switch (s.Fullscreen)
        {
            case Fullscreen::Off:
                return std::format("Resolution: {}", s.WindowSize.ToString());
            case Fullscreen::Exclusive:
                return std::format("Resolution: {}", s.ExclusiveSize.value_or(desktop).ToString());
            default:
                return std::format("Resolution: {} (desktop)", desktop.ToString());
        }
    }

    void MenuItems::ResolutionStep(int step)
    {
        auto& s = _ui.Settings();
        auto& window = _ui.Window();
        if (s.Fullscreen == Fullscreen::Off)
        {
            s.WindowSize = Engine::Platform::Resolutions::Step(window.WindowedSizes(), s.WindowSize, step);
            return;
        }

        if (s.Fullscreen == Fullscreen::Exclusive)
        {
            std::vector<Engine::Platform::Resolution> sizes;
            for (const auto& m : window.ExclusiveModes())
            {
                sizes.push_back(m.Size());
            }

            s.ExclusiveSize = Engine::Platform::Resolutions::Step(sizes, s.ExclusiveSize.value_or(window.Desktop().Size()), step);
        }
    }

    std::string MenuItems::VSyncText() const
    {
        auto chosen = _ui.Settings().VSync;
        auto used = _ui.Renderer().VSync();
        return used == chosen ? std::format("VSync: {}", NameOf(chosen)) : std::format("VSync: {} ({})", NameOf(chosen), NameOf(used));
    }

    std::string MenuItems::BackendText() const
    {
        auto& renderer = _ui.Renderer();
        auto chosen = _ui.Settings().Backend;
        auto name = chosen == Engine::Render::GpuBackend::Auto
            ? std::format("Auto ({})", Engine::Render::GpuBackends::Name(renderer.Gpu().Backend))
            : Engine::Render::GpuBackends::Name(chosen);
        return chosen == renderer.Requested() ? std::format("GPU: {}", name) : std::format("GPU: {} (restart)", name);
    }

    void MenuItems::BackendStep(int step)
    {
        auto& s = _ui.Settings();
        auto backends = Renderer::Backends();
        auto found = std::find(backends.begin(), backends.end(), s.Backend);
        auto index = found == backends.end() ? -1 : static_cast<int>(found - backends.begin());
        s.Backend = backends[Wrap(index + step, static_cast<int>(backends.size()))];
    }

    void MenuItems::ShowGraphics(std::function<void()> back)
    {
        ClearPage();
        auto& s = _ui.Settings();
        AddOption([&s] { return std::format("Graphics: {}", NameOf(s.Graphics)); },
            [&s](int step) { s.Graphics = static_cast<Flow::Graphics>(Wrap(static_cast<int>(s.Graphics) + step, GraphicsCount)); });
        AddOption([&s] { return std::format("Anti-aliasing: {}", AntiAliasingNames[static_cast<int>(s.AntiAliasing)]); },
            [&s](int step)
            {
                s.AntiAliasing = static_cast<Engine::Render::AntiAliasing>(Wrap(static_cast<int>(s.AntiAliasing) + step,
                    static_cast<int>(AntiAliasingNames.size())));
            });
        AddOption([&s] { return std::format("Gore: {}", OnOff(s.Gore)); }, [&s](int) { s.Gore = !s.Gore; });
        EndPage(back);
    }

    void MenuItems::ShowAudio(std::function<void()> back)
    {
        ClearPage();
        auto& s = _ui.Settings();
        AddOption([&s] { return std::format("Master volume: {}", s.MasterVolume); },
            [&s](int step) { s.MasterVolume = VolumeStep(s.MasterVolume, step); });
        AddOption([&s] { return std::format("Music volume: {}", s.MusicVolume); },
            [&s](int step) { s.MusicVolume = VolumeStep(s.MusicVolume, step); });
        AddOption([&s] { return std::format("Effects volume: {}", s.EffectsVolume); },
            [&s](int step) { s.EffectsVolume = VolumeStep(s.EffectsVolume, step); });
        AddOption([&s] { return std::format("Music filter: {}", OnOff(s.MusicFilter)); },
            [&s](int) { s.MusicFilter = !s.MusicFilter; });
        EndPage(back);
    }

    void MenuItems::ShowGame(std::function<void()> back)
    {
        ClearPage();
        auto& s = _ui.Settings();
        AddOption([&s] { return std::format("Difficulty: {}", DifficultyNames[static_cast<int>(s.Difficulty)]); },
            [&s](int step)
            {
                s.Difficulty = static_cast<Kurt::Difficulty>(Wrap(static_cast<int>(s.Difficulty) + step,
                    static_cast<int>(DifficultyNames.size())));
            });

        // The console's fps overlay, for phones without a keyboard (this session only).
        auto dev = _ui.Dev;
        AddOption([dev] { return std::format("FPS overlay: {}", OnOff(dev->Overlay == DevTools::Switch::On)); },
            [dev](int) { dev->ToggleOverlay(); });
        AddItem("Console", [dev] { dev->ConsoleAsked = true; });
        EndPage(back);
    }

    void MenuItems::ShowStereo(std::function<void()> back)
    {
        using Engine::Render::StereoModes;

        ClearPage();
        auto& s = _ui.Settings();
        AddOption([&s] { return std::format("Mode: {}", StereoModes::Names[static_cast<int>(s.Stereo)]); },
            [&s](int step)
            {
                s.Stereo = static_cast<Engine::Render::Stereo>(Wrap(static_cast<int>(s.Stereo) + step,
                    static_cast<int>(StereoModes::Names.size())));
            });
        AddOption([&s] { return std::format("Separation: {:.2f}", s.StereoSeparation); },
            [&s](int step)
            {
                s.StereoSeparation = Math::Clamp(s.StereoSeparation + step * SeparationStep, 0.f, StereoModes::MaxSeparation);
            });
        AddOption([&s] { return std::format("Convergence: {}", ConvergenceText(s.StereoConvergence)); },
            [&s](int step) { s.StereoConvergence = ConvergenceStep(s.StereoConvergence, step); });
        AddOption([&s] { return std::format("Head tracking: {}", OnOff(s.HeadTracking == Engine::Platform::HeadTracking::On)); },
            [&s](int)
            {
                s.HeadTracking = s.HeadTracking == Engine::Platform::HeadTracking::On
                    ? Engine::Platform::HeadTracking::Off
                    : Engine::Platform::HeadTracking::On;
            });
        EndPage(back);
    }

    float MenuItems::ConvergenceStep(float value, int step)
    {
        using Engine::Render::StereoModes;
        return Math::Clamp(step > 0 ? value * ConvergenceRate : value / ConvergenceRate, StereoModes::MinConvergence,
            StereoModes::MaxConvergence);
    }

    std::string MenuItems::ConvergenceText(float value)
    {
        return value >= Engine::Render::StereoModes::MaxConvergence ? "far" : Str::FromFloatTrim(value, 1);
    }

    void MenuItems::ShowMods(std::function<void()> back)
    {
        ClearPage();
        _ui.ScanMods();
        auto& s = _ui.Settings();
        AddTitle("Mods: enhanced look");
        for (const auto& mod : _ui.Mods->Mods)
        {
            auto name = static_cast<int>(mod.Name.size()) > MaxModName ? mod.Name.substr(0, MaxModName) : mod.Name;
            auto folder = mod.Folder;
            AddOption([&s, name, folder] { return std::format("{}: {}", name, OnOff(s.StateOf(folder) == Mods::ModState::On)); },
                [&s, folder](int)
                {
                    s.Mods[folder] = s.StateOf(folder) == Mods::ModState::On ? Mods::ModState::Off : Mods::ModState::On;
                });
        }

        if (_ui.Mods->Mods.empty())
        {
            AddDisabled("No mods in mods/");
        }

        auto hdItems = HdTextures::HdMenu::Items(HdTextures::HdMenu::Current());
        if (std::find(hdItems.begin(), hdItems.end(), HdTextures::HdItem::Make) != hdItems.end())
        {
            AddItem("Make HD textures", [this, back] { ShowHdTextures([this, back] { ShowMods(back); }); });
        }

        // Android: pick the MDK folder again (new or changed files, HD textures made on a PC).
        if (_ui.Import)
        {
            auto import = _ui.Import;
            AddItem("Import from folder", [this, import, back]
            {
                import();
                ShowMods(back);
            });
        }

        EndPage(back);
    }

    void MenuItems::ShowHdTextures(std::function<void()> back)
    {
        ClearPage();
        _back = back;
        AddTitle("HD textures");
        AddTitle("Real-ESRGAN, on the GPU");
        _jobLine = std::make_shared<Entry>("", Kind::Title, nullptr, nullptr);
        _jobLine->Grow = 1.f;
        Add(_jobLine);
        _jobBack = std::make_shared<Entry>("Cancel", Kind::Item, back, nullptr);
        Add(_jobBack);
        auto options = HdTextures::HdOptions::Default();
        options.Source = _ui.Settings().Upscaler();
        _job = HdTextures::HdJob::Start(_ui.Data(), _ui.UserFolder(), options);
        ShowJob();
    }

    void MenuItems::ShowJob()
    {
        if (_job == nullptr)
        {
            return;
        }

        _jobLine->Text = _job->Progress->Describe();
        _jobBack->Text = _job->Progress->Finished() ? "Back" : "Cancel";
    }

    void MenuItems::ShowControls(std::function<void()> back)
    {
        ClearPage();
        auto& s = _ui.Settings();
        AddOption([&s] { return std::format("Mouse sensitivity: {:.2f}", s.MouseSensitivity); },
            [&s](int step) { s.MouseSensitivity = SensitivityStep(s.MouseSensitivity, step); });
        AddOption([&s] { return std::format("Invert mouse: {}", OnOff(s.InvertMouse)); },
            [&s](int) { s.InvertMouse = !s.InvertMouse; });
        for (const auto& [key, name] : Flow::Settings::Actions())
        {
            auto entry = std::make_shared<Entry>(BindingText(key, name), Kind::Item, nullptr, nullptr);
            std::weak_ptr<Entry> weak = entry;
            entry->Press = [this, weak, key, name]
            {
                auto self = weak.lock();
                _waiting = key;
                _waitingDevice = Device::Keyboard;
                _waitingEntry = self;
                _armed = false;
                self->Text = std::format("{}: press a key...", name);
            };
            Add(entry);
        }

        AddItem("Default keys", [this, back]
        {
            _ui.Settings().Bindings.Clear();
            _ui.ApplySettings();
            ShowControls(back);
        });
        EndPage(back);
    }

    void MenuItems::ShowGamepad(std::function<void()> back)
    {
        ClearPage();
        auto& s = _ui.Settings();
        AddOption([&s] { return std::format("Look sensitivity: {:.2f}", s.PadSensitivity); },
            [&s](int step) { s.PadSensitivity = SensitivityStep(s.PadSensitivity, step); });
        AddOption([&s] { return std::format("Invert look: {}", OnOff(s.InvertPad)); },
            [&s](int) { s.InvertPad = !s.InvertPad; });
        for (const auto& [key, name] : Flow::Settings::PadActions())
        {
            auto entry = std::make_shared<Entry>(PadBindingText(key, name), Kind::Item, nullptr, nullptr);
            std::weak_ptr<Entry> weak = entry;
            entry->Press = [this, weak, key, name]
            {
                auto self = weak.lock();
                _waiting = key;
                _waitingDevice = Device::Gamepad;
                _waitingEntry = self;
                _armed = false;
                self->Text = std::format("{}: press a button...", name);
            };
            Add(entry);
        }

        AddItem("Default buttons", [this, back]
        {
            _ui.Settings().PadBindings.Clear();
            _ui.ApplySettings();
            ShowGamepad(back);
        });
        EndPage(back);
    }

    bool MenuItems::HasTouchPage() const
    {
        return _ui.Window().HasTouchScreen() || _ui.TouchEditor;
    }

    void MenuItems::ShowTouch(std::function<void()> back)
    {
        ClearPage();
        auto& s = _ui.Settings();
        if (_ui.Window().HasTouchScreen())
        {
            AddOption([&s] { return std::format("Touch controls: {}", NameOf(s.Touch)); },
                [&s](int step)
                {
                    s.Touch = static_cast<Engine::Platform::TouchButtons>(Wrap(static_cast<int>(s.Touch) + step, TouchButtonsCount));
                });
        }

        AddOption([&s] { return std::format("Touch opacity: {}%", s.TouchOpacity); },
            [&s](int step) { s.TouchOpacity = OpacityStep(s.TouchOpacity, step); });
        // Back on its row.
        auto row = static_cast<int>(_entries.size());
        AddItem("Edit touch layout", [this, back, row]
        {
            ShowTouchLayout([this, back, row]
            {
                ShowTouch(back);
                Select(row);
            });
        });
        EndPage(back);
    }

    void MenuItems::ShowTouchLayout(std::function<void()> back)
    {
        ClearPage();
        _back = back;
        if (_touchView == nullptr)
        {
            _touchView = std::make_shared<Hud::TouchView>(_ui.Renderer(), _fonts->Small);
        }

        _touchPage = std::make_shared<TouchLayoutPage>(_ui, *_touchView, back);
    }

    int MenuItems::OpacityStep(int value, int step)
    {
        auto index = IndexOf(TouchOpacities, value);
        auto count = static_cast<int>(TouchOpacities.size());
        return TouchOpacities[Wrap((index >= 0 ? index : count - 1) + step, count)];
    }

    void MenuItems::WaitForControl(Engine::Platform::Input& input)
    {
        if (!_armed)
        {
            _armed = true;
            return;
        }

        if (!_waiting.has_value())
        {
            return;
        }

        auto key = *_waiting;
        if (_waitingDevice == Device::Gamepad)
        {
            WaitForButton(input, key);
            return;
        }

        if (input.LastControl().empty())
        {
            return;
        }

        if (!input.WasPressed(MenuKey::Back))
        {
            _ui.Settings().Bindings[key] = input.LastControl();
            _ui.ApplySettings();
        }

        const auto& actions = Flow::Settings::Actions();
        auto action = std::find_if(actions.begin(), actions.end(), [key](const auto& a) { return a.first == key; });
        _waitingEntry->Text = BindingText(key, action->second);
        _waiting = std::nullopt;
    }

    void MenuItems::WaitForButton(Engine::Platform::Input& input, Key key)
    {
        auto button = input.LastPadControl();
        if (button.empty() && input.LastControl().empty())
        {
            return;
        }

        if (!button.empty() && input.BindPad(key, button))
        {
            _ui.Settings().PadBindings[key] = button;
            _ui.ApplySettings();
        }

        const auto& actions = Flow::Settings::PadActions();
        auto action = std::find_if(actions.begin(), actions.end(), [key](const auto& a) { return a.first == key; });
        _waitingEntry->Text = PadBindingText(key, action->second);
        _waiting = std::nullopt;
    }

    std::string MenuItems::BindingText(Key key, const std::string& name) const
    {
        return std::format("{}: {}", name, _ui.Input().Describe(key));
    }

    std::string MenuItems::PadBindingText(Key key, const std::string& name) const
    {
        return std::format("{}: {}", name, _ui.Input().DescribePad(key));
    }

    float MenuItems::SensitivityStep(float value, int step)
    {
        auto index = IndexOf(Sensitivities, value);
        return Sensitivities[Wrap((index >= 0 ? index : DefaultSensitivity) + step, static_cast<int>(Sensitivities.size()))];
    }

    float MenuItems::GammaStep(float value, int step)
    {
        auto index = IndexOf(Gammas, value);
        return Gammas[Wrap((index >= 0 ? index : IndexOf(Gammas, Renderer::DefaultGamma)) + step, static_cast<int>(Gammas.size()))];
    }

    std::string MenuItems::OnOff(bool value)
    {
        return value ? "On" : "Off";
    }
}
