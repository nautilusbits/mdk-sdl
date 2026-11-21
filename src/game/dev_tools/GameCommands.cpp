#include "game/dev_tools/GameCommands.h"

#include <array>
#include <format>

#include "core/DotNet.h"
#include "formats/BetaDemo.h"
#include "game/dev_tools/DevSession.h"
#include "game/flow/GameState.h"
#include "game/flow/SaveGames.h"
#include "game/flow/Settings.h"
#include "game/menu/Ui.h"
#include "game/mods/ModCatalog.h"

namespace Mdk::Game::DevTools
{
    namespace
    {
        // C++ has no enum names: a mod's state, by value, in lower case.
        constexpr std::array<const char*, 2> ModStateNames{"on", "off"};
    }

    Flow::Event GameCommands::TakeNext()
    {
        auto next = _next;
        _next = Flow::Event::None;
        return next;
    }

    bool GameCommands::Map(int number, const std::string& arena)
    {
        auto known = Flow::GameState::IndexOf(number) >= 0 || (Formats::BetaDemo::IsBeta(number) && _ui.Beta != nullptr);
        if (!known)
        {
            return false;
        }

        _state.Level = number;
        _state.StartArena = !arena.empty() ? std::optional<std::string>(arena) : std::nullopt;
        _state.Snapshot = std::nullopt;
        _state.Carry = std::nullopt;
        _next = Flow::Event::Play;
        return true;
    }

    bool GameCommands::Load(const std::string& slot)
    {
        auto saved = Flow::SaveGames::In(_ui.UserFolder()).Read(slot);
        if (!saved)
        {
            return false;
        }

        _state.Load(*saved);
        _next = saved->Kind == Flow::SaveKind::BeforeLevel ? Flow::Event::Briefing : Flow::Event::Play;
        return true;
    }

    Switch GameCommands::ToggleGod()
    {
        auto& dev = *_ui.Dev;
        dev.God = Flip(dev.God);
        if (auto level = Level())
        {
            level->SetGod(dev.God);
        }

        return dev.God;
    }

    Switch GameCommands::ToggleNoclip()
    {
        auto& dev = *_ui.Dev;
        dev.Noclip = Flip(dev.Noclip);
        if (auto level = Level())
        {
            level->SetNoclip(dev.Noclip);
        }

        return dev.Noclip;
    }

    Switch GameCommands::ToggleOneHit()
    {
        auto& dev = *_ui.Dev;
        dev.OneHit = Flip(dev.OneHit);
        if (auto level = Level())
        {
            level->SetOneHit(dev.OneHit);
        }

        return dev.OneHit;
    }

    void GameCommands::SetDifficulty(Kurt::Difficulty difficulty)
    {
        _ui.Settings().Difficulty = difficulty;
        if (auto level = Level())
        {
            level->SetDifficulty(difficulty);
        }

        _ui.ApplySettings();
    }

    bool GameCommands::SetLook(Flow::Graphics look)
    {
        _ui.Settings().Graphics = look;
        _ui.ApplySettings();
        auto played = Level();
        if (!played)
        {
            return false;
        }

        auto snapshot = played->Snapshot();
        if (!snapshot)
        {
            return false;
        }

        _state.Level = played->Where().Level;
        _state.Snapshot = snapshot;
        _next = Flow::Event::Play;
        return true;
    }

    void GameCommands::SetAntiAliasing(Engine::Render::AntiAliasing antiAliasing)
    {
        _ui.Settings().AntiAliasing = antiAliasing;
        _ui.ApplySettings();
    }

    void GameCommands::SetStereo(Engine::Render::Stereo stereo, std::optional<float> separation, std::optional<float> convergence)
    {
        auto& settings = _ui.Settings();
        settings.Stereo = stereo;
        if (separation)
        {
            settings.StereoSeparation = Math::Clamp(*separation, 0.f, Engine::Render::StereoModes::MaxSeparation);
        }

        if (convergence)
        {
            settings.StereoConvergence = Math::Clamp(*convergence, 0.f, Engine::Render::StereoModes::MaxConvergence);
        }

        _ui.ApplySettings();
    }

    Switch GameCommands::ToggleOverlay()
    {
        return _ui.Dev->ToggleOverlay();
    }

    std::vector<std::string> GameCommands::ModList()
    {
        _ui.ScanMods();
        std::vector<std::string> list;
        for (const auto& m : _ui.Mods->Mods)
        {
            auto state = ModStateNames[static_cast<int>(_ui.Settings().StateOf(m.Folder))];
            list.push_back(std::format("{} ({}) {}, priority {}", m.Name, m.Folder, state, m.Priority));
        }

        return list;
    }
}
