#include "game/dev_tools/DevConsole.h"

#include <algorithm>

#include "core/DotNet.h"
#include "core/Latin1.h"
#include "engine/diagnostics/LogRing.h"
#include "game/dev_tools/CommandHistory.h"
#include "game/dev_tools/CommandRegistry.h"

namespace Mdk::Game::DevTools
{
    using Engine::Platform::Input;
    using Engine::Platform::MenuKey;
    using Engine::Platform::RawKey;

    DevConsole::DevConsole(std::shared_ptr<CommandRegistry> registry, std::shared_ptr<CommandHistory> history,
        Engine::Diagnostics::LogRing& log, std::function<void(const std::string&)> print)
        : _registry(std::move(registry)), _history(std::move(history)), _log(log), _print(std::move(print))
    {
        _registry->Add({"help", "help", [this](const std::vector<std::string>&) { return Help(); }});
        _registry->Add({"clear", "clear", [this](const std::vector<std::string>&) { return Clear(); }});
    }

    ConsoleState DevConsole::Update(Input& input, float elapsed)
    {
        auto owner = _state;
        if (input.WasPressed(RawKey::Grave))
        {
            Toggle();
        }
        else if (owner == ConsoleState::Open)
        {
            // The text of the frame's Grave (` or ; by the layout) isn't typed.
            Type(input.Typed());
            for (const auto& [raw, key] : Keys)
            {
                if (input.WasPressed(raw))
                {
                    Press(key);
                }
            }

            // A phone's Back button: it has no Escape key.
            if (_state == ConsoleState::Open && input.WasPressed(MenuKey::Back))
            {
                Press(EditKey::Close);
            }
        }

        auto goal = _state == ConsoleState::Open ? 1.f : 0.f;
        _slide = goal > _slide ? MathF::Min(_slide + SlideSpeed * elapsed, goal) : MathF::Max(_slide - SlideSpeed * elapsed, goal);
        return owner;
    }

    void DevConsole::Type(const std::string& text)
    {
        // Code points of the UTF-8 text (C#: UTF-16 chars; one above U+FFFF is two of them).
        constexpr char32_t LastUtf16 = 0xFFFF;
        for (size_t i = 0; i < text.size();)
        {
            auto c = Latin1::Next(text, i);
            if (c == static_cast<char32_t>(Input::Erase))
            {
                Press(EditKey::Erase);
                continue;
            }

            if (c < static_cast<char32_t>(FirstPrintable))
            {
                continue;
            }

            if (c <= static_cast<char32_t>(LastFontCharacter))
            {
                Latin1::Append(_line, c);
                continue;
            }

            _line += c > LastUtf16 ? std::string(2, Unknown) : std::string(1, Unknown);
        }
    }

    void DevConsole::Press(EditKey key)
    {
        switch (key)
        {
            case EditKey::Submit:
                Submit();
                break;
            case EditKey::Erase:
                Latin1::RemoveLast(_line);
                break;
            case EditKey::Complete:
                Complete();
                break;
            case EditKey::Previous:
                _line = _history->Previous();
                break;
            case EditKey::Next:
                _line = _history->Next();
                break;
            case EditKey::ScrollUp:
                _scroll += ScrollLines;
                break;
            case EditKey::ScrollDown:
                _scroll = std::max(_scroll - ScrollLines, 0);
                break;
            case EditKey::Close:
                _state = ConsoleState::Closed;
                break;
        }
    }

    void DevConsole::Run(const std::string& line)
    {
        _print(Prompt + line);
        _history->Add(line);
        for (const auto& output : _registry->Run(line))
        {
            _print(output);
        }
    }

    void DevConsole::Submit()
    {
        auto line = Str::Trim(_line);
        _line = "";
        _scroll = 0;
        if (!line.empty())
        {
            Run(line);
        }
    }

    void DevConsole::Complete()
    {
        auto [line, matches] = _registry->Complete(_line);
        _line = line;
        if (matches.size() > 1)
        {
            _print(Str::Join("  ", matches));
        }
    }

    std::string DevConsole::Help() const
    {
        std::vector<std::string> lines;
        for (const auto& c : _registry->Commands())
        {
            lines.push_back(c->Aliases && !c->Aliases->empty()
                ? std::format("{}  (also {})", c->Usage, Str::Join(", ", *c->Aliases))
                : c->Usage);
        }

        return Str::Join("\n", lines);
    }

    std::string DevConsole::Clear()
    {
        _log.Clear();
        _scroll = 0;
        return "";
    }
}
