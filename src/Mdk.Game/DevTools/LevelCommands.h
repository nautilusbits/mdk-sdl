#pragma once

#include <functional>
#include <optional>
#include <string>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Game/DevTools/ICommandTarget.h"
#include "Mdk.Game/Mods/ModReport.h"

namespace Mdk::Game::Collision
{
    class ArenaSpace;
}

namespace Mdk::Game::Kurt
{
    class Kurt;
}

namespace Mdk::Game::Level
{
    class LevelData;
}

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;
}

namespace Mdk::Game::DevTools
{
    // The console's commands on the level being played (Viewer).
    class LevelCommands : public ILevelTarget
    {
    public:
        LevelCommands(Level::LevelData& level, Kurt::Kurt& kurt, Scripts::ScriptRuntime& scripts, Collision::ArenaSpace& space,
            std::function<bool(const std::string&)> save)
            : _level(level), _kurt(kurt), _scripts(scripts), _space(space), _save(std::move(save)) {}

        // C#: get; init.
        Game::Mods::ModReport ModsReport = Game::Mods::ModReport::None();

        const Game::Mods::ModReport& Mods() const override { return ModsReport; }

        Placement Where() override;

        bool Teleport(const std::string& arena, std::optional<Vector3> point) override;

        void SetGod(Switch god) override;

        void SetNoclip(Switch noclip) override;

        void SetOneHit(Switch oneHit) override;

        bool Give(const std::string& pickup) override;

        void SetHealth(int health) override;

        int KillEnemies() override;

        bool Save(const std::string& slot) override;

        void SetDifficulty(Kurt::Difficulty difficulty) override;

        std::optional<std::string> Snapshot() override;

    private:
        Level::LevelData& _level;
        Kurt::Kurt& _kurt;
        Scripts::ScriptRuntime& _scripts;
        Collision::ArenaSpace& _space;
        std::function<bool(const std::string&)> _save;

        bool Enter(const std::string& arena, Vector3 at);
    };
}
