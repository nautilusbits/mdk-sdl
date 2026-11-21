#include "game/menu/SavePrompt.h"

#include <format>
#include <string_view>

#include "core/Console.h"
#include "core/DotNet.h"
#include "engine/platform/Input.h"
#include "formats/Fti.h"
#include "game/hud/FontView.h"
#include "game/menu/Ui.h"

namespace Mdk::Game::Menu
{
    using Engine::Platform::Input;
    using Engine::Platform::MenuKey;
    using Engine::Platform::Pointer;

    namespace
    {
        // C#'s char.IsAsciiLetterOrDigit.
        bool IsAsciiLetterOrDigit(char c)
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        }
    }

    SavePrompt::SavePrompt(Ui& ui, std::shared_ptr<Fonts> fonts, const Flow::SaveGames& saves, const Flow::SaveGame& save,
        const std::string& name)
        : _ui(ui), _fonts(std::move(fonts)), _saves(saves), _save(save)
    {
        _name = name.size() > NameLength ? name.substr(0, NameLength) : name;
        // A full save (F2) asks for the name at once.
        _stage = save.Kind == Flow::SaveKind::Snapshot ? Stage::Name : Stage::Ask;
        _cursor = static_cast<int>(_name.size());
    }

    void SavePrompt::Update(Input& input, float delta)
    {
        _ticks += delta * TicksPerSecond;
        _grow = MathF::Min(_grow + delta * TicksPerSecond, GrowTicks);
        if (_stage == Stage::Ask)
        {
            Ask(input);
            return;
        }

        TypeName(input);
    }

    void SavePrompt::Ask(Input& input)
    {
        if (input.PointerMoved() || input.WasClicked(Pointer::Left))
        {
            auto row = ToInt(MathF::Floor((_ui.View->Pointer(input).Y - RowTop) / RowHeight));
            if (row >= 0 && row < static_cast<int>(Answers.size()))
            {
                Select(row);
            }
        }

        if (input.WasPressed(MenuKey::Up) || input.WasPressed(MenuKey::Down))
        {
            Select(1 - _selected);
        }

        if (input.WasPressed(MenuKey::Accept) || input.WasClicked(Pointer::Left))
        {
            Answer();
        }
        else if (input.WasPressed(MenuKey::Back))
        {
            _closed = true;
        }
    }

    void SavePrompt::Select(int row)
    {
        if (row == _selected)
        {
            return;
        }

        _selected = row;
        _grow = 0.f;
    }

    void SavePrompt::Answer()
    {
        if (_selected != Yes)
        {
            _closed = true;
            return;
        }

        _stage = Stage::Name;
    }

    void SavePrompt::TypeName(Input& input)
    {
        if (input.WasPressed(MenuKey::Back))
        {
            _closed = true;
            return;
        }

        // Enter saves; Space (also an Accept key) doesn't.
        if (input.WasPressed(MenuKey::Accept) && input.Typed().find(' ') == std::string::npos)
        {
            Save();
            return;
        }

        auto length = static_cast<int>(_name.size());
        if (input.WasPressed(MenuKey::Left))
        {
            _cursor = Math::Max(_cursor - 1, 0);
        }

        if (input.WasPressed(MenuKey::Right))
        {
            _cursor = Math::Min(_cursor + 1, length);
        }

        if (input.WasPressed(MenuKey::Home))
        {
            _cursor = 0;
        }

        if (input.WasPressed(MenuKey::End))
        {
            _cursor = length;
        }

        if (input.WasPressed(MenuKey::Delete) && _cursor < length)
        {
            _name.erase(_cursor, 1);
        }

        for (auto c : Str::ToUpper(input.Typed()))
        {
            Type(c);
        }
    }

    void SavePrompt::Type(char c)
    {
        // Backspace, in order with the text (a phone keyboard's corrections).
        if (c == Input::Erase)
        {
            if (_cursor > 0)
            {
                _name.erase(_cursor - 1, 1);
                _cursor--;
            }

            return;
        }

        if (_cursor >= NameLength || !(IsAsciiLetterOrDigit(c) || std::string_view(NameExtra).find(c) != std::string_view::npos))
        {
            return;
        }

        auto length = static_cast<int>(_name.size());
        _name = _name.substr(0, _cursor) + c + (_cursor + 1 < length ? _name.substr(_cursor + 1) : "");
        _cursor++;
    }

    void SavePrompt::Save()
    {
        if (_name.empty() || !_saves.Write(_name, _save))
        {
            return;
        }

        Console::WriteLine(std::format("Saved game {}: level {}", _name, _save.Level));
        _saved = true;
        _closed = true;
    }

    void SavePrompt::Draw()
    {
        auto& view = *_ui.View;
        auto& font = *_fonts->Big;
        view.FillCanvas(Ui::Black);
        if (_stage == Stage::Ask)
        {
            DrawLine("SV_ASK", -1, Small);
            for (auto row = 0; row < static_cast<int>(Answers.size()); row++)
            {
                auto grow = row == _selected ? Small + (1.f - Small) * _grow / GrowTicks : Small;
                DrawLine(Answers[row], row, grow);
            }

            return;
        }

        // The title, the name one character per step, and the blinking cursor.
        auto title = _ui.Fti->GetTextBytes("SV_TITLE");
        view.Text(font, title, (ScreenView::Size.X - static_cast<float>(font.Width(title))) * 0.5f, TitleY);
        for (auto i = 0; i < static_cast<int>(_name.size()); i++)
        {
            std::vector<byte> glyph{static_cast<byte>(_name[i])};
            view.Text(font, glyph, NameX + NameStep * static_cast<float>(i) - static_cast<float>(font.Width(glyph)) * 0.5f, NameY);
        }

        if ((ToInt(_ticks) & BlinkTicks) != 0)
        {
            static constexpr byte cursor[] = {'_'};
            view.Text(font, cursor, NameX + NameStep * static_cast<float>(_cursor) - static_cast<float>(font.Width(cursor)) * 0.5f, NameY);
        }
    }

    void SavePrompt::DrawLine(const std::string& name, int row, float scale)
    {
        auto text = _ui.Fti->GetTextBytes(name);
        auto& font = *_fonts->Big;
        _ui.View->Text(font, text, (ScreenView::Size.X - static_cast<float>(font.Width(text)) * scale) * 0.5f, RowY + RowHeight * static_cast<float>(row), scale);
    }
}
