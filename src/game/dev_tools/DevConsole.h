#pragma once

#include <functional>
#include <memory>
#include <string>
#include <array>
#include <utility>

#include "engine/platform/Input.h"

namespace Mdk::Engine::Diagnostics
{
    class LogRing;
}

namespace Mdk::Game::DevTools
{
    class CommandHistory;
    class CommandRegistry;

    enum class ConsoleState { Closed, Open };

    // What the console's keys do to its line.
    enum class EditKey { Submit, Erase, Complete, Previous, Next, ScrollUp, ScrollDown, Close };

    // The Quake-style console: a line typed, run by the registry, its output and the log
    // above it. The key left of 1 opens and closes it, sliding down from the top.
    //   typed text ──► line ──Enter──► "] line" printed ──► registry ──► output printed ──► log
    //                   ▲ Tab: completion   ▲ Up/Down: history
    class DevConsole
    {
    public:
        // The prompt the echoed lines start with.
        static constexpr const char* Prompt = "] ";

        DevConsole(std::shared_ptr<CommandRegistry> registry, std::shared_ptr<CommandHistory> history,
            Engine::Diagnostics::LogRing& log, std::function<void(const std::string&)> print);

        // Read-only outside (C#: private set).
        ConsoleState State() const { return _state; }

        // The line being typed.
        const std::string& Line() const { return _line; }

        // How far it's down: 0 closed, 1 open.
        float Slide() const { return _slide; }

        // Log lines scrolled back from the newest.
        int Scroll() const { return _scroll; }

        void Toggle() { _state = _state == ConsoleState::Open ? ConsoleState::Closed : ConsoleState::Open; }

        // A frame's keys and text (only Grave while closed); returns whether the console had
        // the keys this frame (the game doesn't get them).
        ConsoleState Update(Engine::Platform::Input& input, float elapsed);

        // Adds typed text in order: Input.Erase erases, other control characters are dropped, those
        // the font hasn't are '?'.
        void Type(const std::string& text);

        void Press(EditKey key);

        // Runs a line as typed: echoed, kept in the history, its output printed.
        void Run(const std::string& line);

    private:
        // Opening or closing takes 1 / this seconds.
        static constexpr float SlideSpeed = 5.f;
        // PageUp and PageDown scroll this many lines.
        static constexpr int ScrollLines = 5;
        static constexpr char FirstPrintable = ' ';
        // 'ÿ': the font's last character (Latin-1 0xFF; the typed text is UTF-8).
        static constexpr int LastFontCharacter = 0xFF;
        static constexpr char Unknown = '?';

        // In the C# dictionary's order (the order a frame's keys are pressed in).
        static constexpr std::array<std::pair<Engine::Platform::RawKey, EditKey>, 7> Keys{{
            {Engine::Platform::RawKey::Enter, EditKey::Submit},
            {Engine::Platform::RawKey::Tab, EditKey::Complete},
            {Engine::Platform::RawKey::Up, EditKey::Previous},
            {Engine::Platform::RawKey::Down, EditKey::Next},
            {Engine::Platform::RawKey::PageUp, EditKey::ScrollUp},
            {Engine::Platform::RawKey::PageDown, EditKey::ScrollDown},
            {Engine::Platform::RawKey::Escape, EditKey::Close},
        }};

        std::shared_ptr<CommandRegistry> _registry;
        std::shared_ptr<CommandHistory> _history;
        Engine::Diagnostics::LogRing& _log;
        std::function<void(const std::string&)> _print;
        ConsoleState _state{};
        std::string _line;
        float _slide = 0;
        int _scroll = 0;

        void Submit();

        void Complete();

        std::string Help() const;

        std::string Clear();
    };
}
