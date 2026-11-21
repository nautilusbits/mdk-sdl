#pragma once

#include <array>
#include <memory>
#include <string>

#include "engine/platform/Window.h"
#include "game/flow/SaveGames.h"

namespace Mdk::Engine::Platform
{
    class Input;
}

namespace Mdk::Game::Menu
{
    class Fonts;
    class Ui;

    // The save prompt after a level (0x42b520(1), each frame 0x42b75c): SV_ASK "Save Game?" with
    // ABORT2 "Yes" and ABORT3 "No", then SV_TITLE "Name for Saved Game" with the name typed below it
    // (up to 8 letters, digits, _ and $), preset to the next level's number. Enter saves, Esc gives
    // up. FONTBIG on black.
    //   Save Game?        y 139
    //       Yes           y 175   the selected line grows from 65 % to full size in 5 ticks
    //       No            y 211
    class SavePrompt
    {
    public:
        static constexpr int NameLength = 8;

        // Asks whether to save `save`, offering `name`.
        SavePrompt(Ui& ui, std::shared_ptr<Fonts> fonts, const Flow::SaveGames& saves, const Flow::SaveGame& save,
            const std::string& name);

        // Read-only outside (C#: private set).
        bool Closed() const { return _closed; }

        // Closed after writing the save, under Name.
        bool Saved() const { return _saved; }

        const std::string& Name() const { return _name; }

        // A touch screen's keyboard shows while the name is typed.
        Engine::Platform::Typing Typing() const
        {
            return _stage == Stage::Name && !_closed ? Engine::Platform::Typing::On : Engine::Platform::Typing::Off;
        }

        void Update(Engine::Platform::Input& input, float delta);

        void Draw();

    private:
        enum class Stage { Ask, Name };

        static constexpr float TicksPerSecond = 30.f;
        static constexpr float RowY = 175.f;
        static constexpr float RowHeight = 36.f;
        // The rows under the pointer start 26 above the first baseline.
        static constexpr float RowTop = RowY - RowHeight + 10.f;
        static constexpr std::array<const char*, 2> Answers{"ABORT2", "ABORT3"};
        static constexpr int Yes = 0;
        static constexpr float Small = 0.65f;
        static constexpr float GrowTicks = 5.f;
        // The name: the title's baseline, then one character per 28 pixels from x 202, baseline 200.
        static constexpr float TitleY = 31.f;
        static constexpr float NameX = 202.f;
        static constexpr float NameY = 200.f;
        static constexpr float NameStep = 28.f;
        static constexpr const char* NameExtra = "_$";
        // The cursor blinks every 8 frames (0x5742e8 & 8).
        static constexpr int BlinkTicks = 8;

        Ui& _ui;
        std::shared_ptr<Fonts> _fonts;
        Flow::SaveGames _saves;
        Flow::SaveGame _save;
        Stage _stage = Stage::Ask;
        int _selected = Yes;
        float _grow = 0;
        std::string _name;
        int _cursor = 0;
        float _ticks = 0;
        bool _closed = false;
        bool _saved = false;

        // Yes or No: up and down or the mouse choose, Enter, Space or a click confirms, Esc says no.
        void Ask(Engine::Platform::Input& input);

        void Select(int row);

        void Answer();

        // Typed characters replace the one at the cursor; Backspace and Delete remove one,
        // Left, Right, Home and End move the cursor.
        void TypeName(Engine::Platform::Input& input);

        void Type(char c);

        void Save();

        // A line centred at row `row`, scaled about its centre.
        void DrawLine(const std::string& name, int row, float scale);
    };
}
