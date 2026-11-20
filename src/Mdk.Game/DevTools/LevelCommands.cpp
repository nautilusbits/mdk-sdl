#include "Mdk.Game/DevTools/LevelCommands.h"

#include <algorithm>

#include "Mdk.Formats/Arena.h"
#include "Mdk.Game/Collision/ArenaSpace.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/ArenaStops.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Scripts/GameStats.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"

namespace Mdk::Game::DevTools
{
    Placement LevelCommands::Where()
    {
        return {_level.Number, _scripts.CurrentArena, _kurt.Feet, _kurt.Yaw};
    }

    bool LevelCommands::Teleport(const std::string& arena, std::optional<Vector3> point)
    {
        if (!point)
        {
            auto stop = Level::ArenaStops::Find(_level, _space, arena);
            return stop && Enter(arena, *stop);
        }

        auto at = *point;
        auto target = !arena.empty() ? arena : _space.ArenaAt(at).value_or(_scripts.CurrentArena);
        auto known = std::any_of(_level.Arenas.begin(), _level.Arenas.end(), [&](const auto& a) { return a->Name == target; });
        return _level.IsReachable(target) && known && Enter(target, at);
    }

    bool LevelCommands::Enter(const std::string& arena, Vector3 at)
    {
        _scripts.TeleportKurt(arena, at, _kurt.Yaw);
        return true;
    }

    void LevelCommands::SetGod(Switch god)
    {
        _kurt.Mortality = god == Switch::On ? Kurt::Mortality::God : Kurt::Mortality::Mortal;
    }

    void LevelCommands::SetNoclip(Switch noclip)
    {
        _kurt.Clipping = noclip == Switch::On ? Kurt::Clipping::Off : Kurt::Clipping::On;
    }

    void LevelCommands::SetOneHit(Switch oneHit)
    {
        _scripts.Lethality = oneHit == Switch::On ? Scripts::Lethality::OneHit : Scripts::Lethality::Normal;
    }

    bool LevelCommands::Give(const std::string& pickup)
    {
        return !_kurt.Collect(pickup).empty();
    }

    void LevelCommands::SetHealth(int health)
    {
        _kurt.SetHealth(health);
    }

    int LevelCommands::KillEnemies()
    {
        std::vector<std::shared_ptr<Objects::MdkObject>> enemies;
        for (const auto& o : _scripts.Objects)
        {
            if (!o->Dead && o->Health > 0 && o->Arena == _scripts.CurrentArena && Scripts::GameStats::IsEnemy(o->TypeName))
            {
                enemies.push_back(o);
            }
        }

        for (const auto& enemy : enemies)
        {
            _scripts.Kill(*enemy);
        }

        return static_cast<int>(enemies.size());
    }

    bool LevelCommands::Save(const std::string& slot)
    {
        return _scripts.CanSnapshot() && _save(slot);
    }

    void LevelCommands::SetDifficulty(Kurt::Difficulty difficulty)
    {
        _kurt.Inventory->Difficulty = difficulty;
    }

    std::optional<std::string> LevelCommands::Snapshot()
    {
        return _scripts.CanSnapshot() ? std::optional<std::string>(_scripts.Capture()) : std::nullopt;
    }
}
