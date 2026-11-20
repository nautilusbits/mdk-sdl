#include "Mdk.Game/Menu/PauseMenu.h"

#include <format>

#include "Mdk.Core/Console.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Engine/Platform/Window.h"
#include "Mdk.Game/Hud/FontView.h"
#include "Mdk.Game/Menu/MenuItems.h"
#include "Mdk.Game/Menu/Ui.h"

namespace Mdk::Game::Menu
{
    using Engine::Platform::Key;
    using Engine::Platform::MenuKey;

    PauseMenu::PauseMenu(Ui& ui, std::optional<Quick> quick) : _ui(ui), _quick(std::move(quick))
    {
        _fonts = std::make_shared<Fonts>(ui.Renderer(), *ui.Fti, nullptr, ui.CanvasMods());
        _items = std::make_shared<MenuItems>(ui, _fonts, ClickGain);
    }

    std::string PauseMenu::KeysHint(Engine::Platform::Input& input)
    {
        return std::format("Quick save: {}   Quick load: {}", input.Describe(Key::QuickSave), input.Describe(Key::QuickLoad));
    }

    void PauseMenu::Show()
    {
        _open = true;
        ShowMain();
    }

    Flow::Event PauseMenu::Update(float delta)
    {
        _ui.View->Layout(ScreenView::Fit::Inside);
        // Esc resumes; on the options' pages it goes up a page.
        if (_ui.Input().WasPressed(MenuKey::Back) && !_items->HandlesBack())
        {
            Resume();
            return _next;
        }

        _items->Update(delta);
        _ui.View->FillCanvas(Dim);
        _items->Draw();
        DrawHint();
        return _next;
    }

    void PauseMenu::DrawHint()
    {
        // A touch screen has no F2 or F9: its menu items save and load.
        if (_page != Page::Main || _ui.Window().HasTouchScreen())
        {
            return;
        }

        auto hint = KeysHint(_ui.Input());
        std::vector<byte> text(hint.begin(), hint.end());
        auto& font = *_fonts->Small;
        _ui.View->Text(font, text, MathF::Round((ScreenView::Size.X - font.Width(text)) / 2.f), HintY);
    }

    void PauseMenu::ShowMain()
    {
        _items->Clear();
        _page = Page::Main;
        _items->AddItem("Resume", [this] { Resume(); });

        AddQuick();
        _items->AddItem("Options", [this]
        {
            _page = Page::Options;
            _items->ShowOptions([this] { ShowMain(); });
        });
        _items->AddItem("Main menu", [this] { _next = Flow::Event::Menu; });
        _items->AddItem("Quit", [this] { _next = Flow::Event::Quit; });
        Console::WriteLine(std::format("Menu: {}", _items->Describe()));
        Console::WriteLine(std::format("Hint: {}", KeysHint(_ui.Input())));
    }

    void PauseMenu::AddQuick()
    {
        if (!_quick.has_value())
        {
            return;
        }

        auto& quick = *_quick;
        _items->AddItem("Quick save", [this, &quick] { ResumeThen(quick.Save); });
        if (!quick.CanLoad())
        {
            _items->AddDisabled("Quick load");
            return;
        }

        _items->AddItem("Quick load", [this, &quick] { ResumeThen(quick.Load); });
    }

    void PauseMenu::Resume()
    {
        _items->Clear();
        _open = false;
    }

    void PauseMenu::ResumeThen(const std::function<void()>& quick)
    {
        Resume();
        quick();
    }
}
