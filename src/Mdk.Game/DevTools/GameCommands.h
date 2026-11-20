#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Mdk.Game/DevTools/ICommandTarget.h"
#include "Mdk.Game/Flow/Screen.h"

namespace Mdk::Game::Flow
{
    class GameState;
}

namespace Mdk::Game::Menu
{
    class Ui;
}

namespace Mdk::Game::DevTools
{
    // The console's commands on any screen (Game): those of the level go to `level`, the screen's
    // level or null. Commands that leave the screen (map, load, quit, a new look) set the event the
    // game takes after the frame (TakeNext).
    class GameCommands : public ICommandTarget
    {
    public:
        GameCommands(Menu::Ui& ui, Flow::GameState& state, std::function<std::shared_ptr<ILevelTarget>()> level)
            : _ui(ui), _state(state), _level(std::move(level)) {}

        std::shared_ptr<ILevelTarget> Level() override { return _level(); }

        // Game time runs this much faster, on every screen (C#: private set).
        float TimeScale() const { return _timeScale; }

        // The event a command asked for (once), Event.None if none.
        Flow::Event TakeNext();

        bool Map(int number, const std::string& arena) override;

        bool Load(const std::string& slot) override;

        Switch ToggleGod() override;

        Switch ToggleNoclip() override;

        Switch ToggleOneHit() override;

        void SetDifficulty(Kurt::Difficulty difficulty) override;

        // The level reloads as it is (a full save) in the new look, when it can be saved.
        bool SetLook(Flow::Graphics look) override;

        void SetAntiAliasing(Engine::Render::AntiAliasing antiAliasing) override;

        void SetStereo(Engine::Render::Stereo stereo, std::optional<float> separation = std::nullopt,
            std::optional<float> convergence = std::nullopt) override;

        void SetTimeScale(float scale) override { _timeScale = scale; }

        Switch ToggleOverlay() override;

        void Quit() override { _next = Flow::Event::Quit; }

        std::vector<std::string> ModList() override;

    private:
        Menu::Ui& _ui;
        Flow::GameState& _state;
        std::function<std::shared_ptr<ILevelTarget>()> _level;
        Flow::Event _next = Flow::Event::None;
        float _timeScale = 1.f;

        static Switch Flip(Switch value) { return value == Switch::On ? Switch::Off : Switch::On; }
    };
}
