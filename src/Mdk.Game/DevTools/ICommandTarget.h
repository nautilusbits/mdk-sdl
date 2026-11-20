#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/Mods/ModReport.h"

namespace Mdk::Game::Flow
{
    enum class Graphics;
}

namespace Mdk::Game::Kurt
{
    enum class Difficulty;
}

namespace Mdk::Game::DevTools
{
    // A cheat or a tool turned on or off.
    enum class Switch { Off, On };

    // Where Kurt is: the level, his arena, his feet and yaw (degrees).
    struct Placement
    {
        int Level = 0;
        std::string Arena;
        Vector3 Feet;
        float Yaw = 0;

        bool operator==(const Placement&) const = default;
    };

    class ILevelTarget;

    // What the console's commands act on: the game, on any screen, and the level when one
    // is played.
    //   commands ──► ICommandTarget (map, load, god, look, fps...) ──► ILevelTarget? (tp, give, kill...)
    class ICommandTarget
    {
    public:
        virtual ~ICommandTarget() = default;

        // The level being played; null on the other screens (menus, the fall, the stream).
        virtual std::shared_ptr<ILevelTarget> Level() = 0;

        // Plays a level from the start, or from a floor of `arena` ("": the level's start). False
        // for an unknown level.
        virtual bool Map(int level, const std::string& arena) = 0;

        // Loads a saved game; false when there's none of that name.
        virtual bool Load(const std::string& slot) = 0;

        // The session's god mode, noclip and onehit: the level's Kurt now, or the next level's.
        virtual Switch ToggleGod() = 0;
        virtual Switch ToggleNoclip() = 0;
        virtual Switch ToggleOneHit() = 0;

        virtual void SetDifficulty(Kurt::Difficulty difficulty) = 0;

        // The look, saved; returns whether the level reloads with it now.
        virtual bool SetLook(Flow::Graphics look) = 0;

        virtual void SetAntiAliasing(Engine::Render::AntiAliasing antiAliasing) = 0;

        // The stereo mode, and the eyes' separation and convergence when given; saved and
        // applied at once.
        virtual void SetStereo(Engine::Render::Stereo stereo, std::optional<float> separation = std::nullopt,
            std::optional<float> convergence = std::nullopt) = 0;

        virtual void SetTimeScale(float scale) = 0;

        virtual Switch ToggleOverlay() = 0;

        virtual void Quit() = 0;

        // The mods found, in order, each on or off.
        virtual std::vector<std::string> ModList() = 0;
    };

    // What the console's commands act on in the level being played.
    class ILevelTarget
    {
    public:
        virtual ~ILevelTarget() = default;

        // What the level took from the mods.
        virtual const Game::Mods::ModReport& Mods() const = 0;

        virtual Placement Where() = 0;

        // Kurt to a point (in its arena, or the one given), or to a floor of an arena when `point`
        // is null. False for an unknown arena.
        virtual bool Teleport(const std::string& arena, std::optional<Vector3> point) = 0;

        virtual void SetGod(Switch god) = 0;
        virtual void SetNoclip(Switch noclip) = 0;

        // Kurt's hits kill at once.
        virtual void SetOneHit(Switch oneHit) = 0;

        // Kurt takes a pickup (its model name); false when he can't.
        virtual bool Give(const std::string& pickup) = 0;

        virtual void SetHealth(int health) = 0;

        // Kills the enemies of Kurt's arena; returns how many.
        virtual int KillEnemies() = 0;

        // A full save (as F2); false when the level can't be saved now.
        virtual bool Save(const std::string& slot) = 0;

        // Kurt's inventory follows the difficulty.
        virtual void SetDifficulty(Kurt::Difficulty difficulty) = 0;

        // The level's state as a full save, to reload it; null when it can't be saved now.
        virtual std::optional<std::string> Snapshot() = 0;
    };
}
