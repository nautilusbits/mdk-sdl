#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Mdk.Game/DevTools/DevConsole.h"

namespace Mdk::Engine::Platform
{
    class Input;
}

namespace Mdk::Engine::Render
{
    class Renderer;
}

namespace Mdk::Game::Hud
{
    class FontView;
}

namespace Mdk::Game::DevTools
{
    class ConsoleView;
    class DevSession;
    class DevTextView;
    class OverlayView;

    // The developer tools of every screen: the console (Grave) and the debug overlay (F3).
    // The game updates them before each screen's frame; while the console is open the screen runs
    // on without input.
    //   Input ──► Grave, F3 ──► console / overlay
    //         └─► console open? ──► console's line, the screen gets nothing : the screen's keys
    class DevUi
    {
    public:
        DevUi(std::shared_ptr<DevConsole> console, std::function<void()> toggleOverlay)
            : _console(std::move(console)), _toggleOverlay(std::move(toggleOverlay)) {}

        DevConsole& Console() const { return *_console; }

        // The frame's keys: returns ConsoleState.Open when the console had them (also on the frame
        // Grave opens it, so its character isn't typed elsewhere).
        ConsoleState Update(Engine::Platform::Input& input, float elapsed);

        // The tests' --console: the console opens and runs these lines, once.
        void RunOnce(const std::optional<std::string>& lines);

    private:
        std::shared_ptr<DevConsole> _console;
        std::function<void()> _toggleOverlay;
        bool _optionRan = false;
    };

    // Draws the developer tools on the canvas over any screen: the overlay (the frame's
    // timings, then the screen's lines) and the console.
    class DevUiView
    {
    public:
        DevUiView(Engine::Render::Renderer& renderer, std::shared_ptr<Hud::FontView> font);
        ~DevUiView();

        ConsoleView& Console() const { return *_console; }

        // The GPU's and the display's lines, set when they change.
        std::vector<std::string> System;

        // The overlay asks the screen's lines only when it's shown.
        void Draw(DevUi& dev, DevSession& session, const std::function<std::vector<std::string>()>& screen);

    private:
        // The overlay's text is made again this often (seconds): its numbers are averages, and
        // making it every frame would make garbage every frame.
        static constexpr double Refresh = 0.25;

        Engine::Render::Renderer& _renderer;
        // OverlayView and ConsoleView draw through it (C#: captured by their constructors).
        std::shared_ptr<DevTextView> _text;
        std::shared_ptr<OverlayView> _overlay;
        std::shared_ptr<ConsoleView> _console;
        std::vector<std::string> _lines;
        long long _made = 0;
    };
}
