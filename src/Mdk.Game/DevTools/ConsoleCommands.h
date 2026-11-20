#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/DevTools/ICommandTarget.h"

namespace Mdk::Game::DevTools
{
    class CommandRegistry;

    // The console's commands: their arguments parsed, checked and handed to the ICommandTarget; bad
    // ones print the usage. Those of the level answer NotInLevel on the other screens.
    struct ConsoleCommands
    {
        static void Register(CommandRegistry& registry, ICommandTarget& target);

    private:
        using Args = std::vector<std::string>;

        static constexpr const char* All = "all";
        static constexpr int Coordinates = 3;
        static constexpr float MaxTimeScale = 10.f;
        static constexpr const char* NotInLevel = "Not in a level";
        // God mode, noclip and onehit switched outside a level.
        static constexpr const char* NextLevel = " from the next level";
        // "give all": every item (as many as the slots hold), every sniper round, health.
        static constexpr std::array<const char*, 11> AllPickups{"SW_HBOMB", "SW_GATT", "SW_TWIST", "SW_THUMP", "SW_INTER",
            "SW_HOME", "SW_SGREN", "SW_HGREN", "SW_LGREN", "SW_BONES", "SW_H150"};

        static const std::map<std::string, Engine::Render::AntiAliasing>& AntiAliasings();

        // Where Kurt is, and the command line options that start there.
        //   level 3 arena ARENA_1 at -4.00 0.50 195.00 yaw 90
        //   --level=3 --at=-4.00,0.50,195.00,90
        //   --level=3 --teleport=ARENA_1,-4.00,0.50,195.00
        //   --level=3 --console="tp ARENA_1 -4.00 0.50 195.00"
        static std::string Pos(const Placement& at);

        static std::string Map(ICommandTarget& target, const Args& args);

        // "tp ARENA", "tp x y z", "tp ARENA x y z"; commas work as spaces (--teleport's form).
        static std::string Teleport(ILevelTarget& target, const Args& args);

        static std::string Give(ILevelTarget& target, const Args& args);

        static std::string Health(ILevelTarget& target, const Args& args);

        static std::string Save(ILevelTarget& target, const Args& args);

        static std::string Load(ICommandTarget& target, const Args& args);

        static std::string SetDifficulty(ICommandTarget& target, const Args& args);

        static std::string Look(ICommandTarget& target, const Args& args);

        static std::string AntiAlias(ICommandTarget& target, const Args& args);

        // The stereo mode, and the eyes' separation and convergence when given.
        static std::string Stereo(ICommandTarget& target, const Args& args);

        static std::string TimeScale(ICommandTarget& target, const Args& args);

        // The mods found (on or off), then what the level took from them.
        //   Clean walls (clean-walls) on, priority 0
        //   HD textures (Real-ESRGAN) (hd-textures) off, priority -100
        //   mods 1: 14 images, 1 models
        //   images: WALL1 WALL2 ...
        static std::string Mods(ICommandTarget& target);

        static std::string Quit(ICommandTarget& target);

        // A command of the level: run on it, or NotInLevel.
        static std::string InLevel(ICommandTarget& target, const std::function<std::string(ILevelTarget&)>& run);

        // "god on"; outside a level "god on from the next level".
        static std::string Session(ICommandTarget& target, const std::string& name, Switch value);

        static std::string Usage(const std::string& usage) { return "Usage: " + usage; }

        static std::string Name(Switch value);

        // Lower<T>(value): an enum's name in lower case (C++ has no enum names: callers pass the name).
        static std::string Lower(const std::string& name);

        static std::string Format(float value);
    };
}
