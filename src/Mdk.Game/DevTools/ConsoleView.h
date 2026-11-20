#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Mdk.Core/Drawing.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/DevTools/DevTextView.h"

namespace Mdk::Engine::Diagnostics
{
    class LogRing;
}

namespace Mdk::Game::DevTools
{
    class DevConsole;

    // Whether the console shows a close button (a touch screen: no Escape key).
    enum class CloseButton { Hidden, Shown };

    // Where a touch lands on the open console: its log, its typed line, its close button,
    // or the game below it.
    enum class ConsoleSpot { Log, Line, Close, Below };

    // Draws the console: half the canvas high, slid down from the top by DevConsole.Slide; the line
    // typed at the bottom, the log above it, newest last.
    //   ┌──────────────────────────────┐ ▲
    //   │ Level 3: 12 arenas ...       │ │ half the canvas
    //   │ ] pos                        │ │
    //   │ ] god_          MDK SDL 1.2.1│ ▼ × slide
    //   └──────────────────────────────┘
    // A touch screen's console has a close button at its top right (Spot).
    class ConsoleView
    {
    public:
        explicit ConsoleView(DevTextView& text) : _text(text) {}

        // The console's bottom edge on the canvas (0 when closed).
        static float Bottom(const DevConsole& console);

        DevTools::CloseButton CloseButton{};

        // The close button on the canvas for a console whose bottom edge is at `bottom`: its top
        // right, inside the safe area.
        static RectangleF CloseArea(float bottom, RectangleF safe);

        // What a touch at `point` (canvas) hits on a console whose bottom edge is at `bottom`.
        //   ┌──────────────────[X]┐  Close
        //   │ log                 │  Log: hides the keyboard
        //   │ ] line_             │  Line: shows it
        //   └─────────────────────┘
        //     game                   Below: closes the console
        static ConsoleSpot Spot(Vector2 point, float bottom, RectangleF close);

        // A touch on this console (Spot(Vector2, float, RectangleF)).
        ConsoleSpot Spot(const DevConsole& console, Vector2 point);

        void Draw(const DevConsole& console, Engine::Diagnostics::LogRing& log);

    private:
        static constexpr float Margin = 4.f;
        static constexpr float EdgeHeight = 1.f;
        static constexpr const char* Cursor = "_";
        static constexpr Vector4 Background{0.02f, 0.03f, 0.06f, 0.85f};
        static constexpr Vector4 Edge{0.5f, 0.6f, 0.8f, 1.f};
        // The close button's side (canvas units: about 5 mm on a phone) and its cross's
        // squares (as many on each diagonal).
        static constexpr float CloseSize = 32.f;
        static constexpr int CrossSquares = 9;
        static constexpr float CrossInset = 8.f;
        static constexpr Vector4 CloseFill{0.15f, 0.18f, 0.25f, 0.9f};
        // The typed line's touch area: its row and the one above (canvas units).
        static constexpr float LineArea = DevTextView::LineHeight * 2.f + Margin;

        // The version, at the bottom right as in Quake's console.
        static const std::string& Version();

        static float Height() { return Engine::Render::Renderer::CanvasHeight / 2.f; }

        DevTextView& _text;
        // The log's lines shown (kept), the line typed and its prompt (made when it changes).
        std::vector<std::string> _lines;
        std::optional<std::string> _typed;
        std::string _prompt;

        // A touch screen's close button: a box with a cross of squares.
        void DrawClose(float bottom);
    };
}
