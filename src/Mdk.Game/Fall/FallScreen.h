#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Game/Audio/SoundBank.h"
#include "Mdk.Game/Fall/FallSim.h"
#include "Mdk.Game/Fall/FallView.h"
#include "Mdk.Game/Flow/Screen.h"
#include "Mdk.Game/Viewer.h"

namespace Mdk::Game::Flow
{
    class GameState;
}

namespace Mdk::Game::Menu
{
    class Ui;
    class PauseMenu;
}

namespace Mdk::Game::Fall
{
    // The fall before a level (fall.gd) as a screen of the game: the keys steer Kurt, Esc
    // opens the pause menu as in a level (0x403cf8). It ends with the level, which keeps Kurt's health and pickups
    // (GameState.Carry), or, when he dies, with the main menu.
    //   input ──► FallSim ──► FallView (3D), FallHud, palette effects ──► frame
    //                └── sounds (FALL3D.SNI): 8 voices, the wind and grind loops
    class FallScreen : public Flow::IScreen
    {
    public:
        FallScreen(Menu::Ui& ui, Flow::GameState& state, ViewerOptions test);

        Flow::Event Frame(float elapsed, const std::optional<std::string>& screenshot) override;

        // The fall's time, Kurt's height above the ground, his health and pickups.
        std::vector<std::string> Status() const override;

        void Dispose() override;

    private:
        // The falls are those of the level indices 0-4 (LEVEL5 has none).
        static constexpr int LastIndex = 4;
        static constexpr float MaxDelta = 0.1f;
        // The original mixes the fall's sounds on 8 voices at most; more are dropped.
        static constexpr int MaxVoices = 8;
        static constexpr float MessageSeconds = 3.f;
        static constexpr float PrintInterval = 1.f;

        Menu::Ui& _ui;
        Flow::GameState& _state;
        ViewerOptions _test;
        std::unique_ptr<FallSim> _sim;
        std::unique_ptr<FallView> _view;
        std::shared_ptr<Audio::SoundBank> _sounds;
        std::shared_ptr<Menu::PauseMenu> _pause;
        std::vector<int> _voices;
        int _windVoice = 0;
        int _grindVoice = 0;
        float _nextPrint = 0;

        // A frame under the pause menu: the fall drawn as it stopped, the menu over it.
        Flow::Event Paused(float elapsed, const std::optional<std::string>& screenshot);

        // The level with Kurt's health and pickups, or the menu when he died.
        Flow::Event Next();

        // The steering keys: x right, y up the screen (the turn or strafe keys, forward and back).
        static Vector2 Steering(Engine::Platform::Input& input);

        // Tests: "forward" held for --walk seconds from --delay seconds of fall.
        void HoldTestKeys(Engine::Platform::Input& input);

        // Tests (--profile): Kurt, the camera, the wind and the health once a second of fall.
        void Profile();

        void Report(const std::string& what);

        void UpdateSounds();

        int Loop(const std::string& name);

        void Play(const std::string& name);
    };
}
