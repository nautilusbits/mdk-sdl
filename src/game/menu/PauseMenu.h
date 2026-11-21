#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "core/Numerics.h"
#include "game/flow/Screen.h"

namespace Mdk::Engine::Platform
{
    class Input;
}

namespace Mdk::Game::Menu
{
    class Fonts;
    class MenuItems;
    class Ui;

    // The pause menu (Esc during the game; pause_menu.gd): resume, quick save, quick load,
    // options, back to the main menu, quit, over the dimmed game. The game waits while it's open. Its
    // first page names the quick save and quick load keys at the bottom (small font).
    class PauseMenu
    {
    public:
        // The level's quick save and quick load (what the keys do), and whether there's a
        // save to load (Quick load is greyed out otherwise). The fall has none.
        struct Quick
        {
            std::function<void()> Save;
            std::function<void()> Load;
            std::function<bool()> CanLoad;
        };

        explicit PauseMenu(Ui& ui, std::optional<Quick> quick = std::nullopt);

        // Read-only outside (C#: private set).
        bool Open() const { return _open; }

        // The bound quick keys: "Quick save: F2   Quick load: F9".
        static std::string KeysHint(Engine::Platform::Input& input);

        void Show();

        // A frame while open: the menu over the dimmed game. Returns the game's next screen.
        Flow::Event Update(float delta);

    private:
        enum class Page { Main, Options };

        static constexpr float ClickGain = 0.5f;
        static constexpr Vector4 Dim{0.f, 0.f, 0.f, 0.6f};
        // The keys' hint: its baseline on the 600 x 360 view.
        static constexpr float HintY = 345.f;

        Ui& _ui;
        std::shared_ptr<Fonts> _fonts;
        std::shared_ptr<MenuItems> _items;
        std::optional<Quick> _quick;
        Flow::Event _next = Flow::Event::None;
        Page _page{};
        bool _open = false;

        void DrawHint();

        void ShowMain();

        // Quick save and quick load: the menu closes first, quick save's name prompt and the
        // loaded level take the screen.
        void AddQuick();

        void Resume();

        void ResumeThen(const std::function<void()>& quick);
    };
}
