#include "Mdk.Game/DevTools/ConsoleCommands.h"

#include <algorithm>
#include <array>
#include <optional>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/DevTools/CommandRegistry.h"
#include "Mdk.Game/Flow/Settings.h"
#include "Mdk.Game/Kurt/Inventory.h"

namespace Mdk::Game::DevTools
{
    namespace
    {
        // C++ has no enum names: the names of the enums the console parses, by value.
        constexpr std::array<const char*, 3> DifficultyNames{"Easy", "Normal", "Hard"};
        constexpr std::array<const char*, 2> GraphicsNames{"Original", "Enhanced"};
        constexpr std::array<const char*, 2> SwitchNames{"Off", "On"};

        // Enum.TryParse(ignoreCase: true) + Enum.IsDefined: a member's name or its number.
        template <typename TEnum, size_t N>
        bool TryParseEnum(const std::string& text, const std::array<const char*, N>& names, TEnum& value)
        {
            for (size_t i = 0; i < N; i++)
            {
                if (Str::EqualsIgnoreCase(text, names[i]))
                {
                    value = static_cast<TEnum>(i);
                    return true;
                }
            }

            int number = 0;
            if (!Str::TryParseInt(text, number) || number < 0 || number >= static_cast<int>(N))
            {
                return false;
            }

            value = static_cast<TEnum>(number);
            return true;
        }
    }

    const std::map<std::string, Engine::Render::AntiAliasing>& ConsoleCommands::AntiAliasings()
    {
        static const std::map<std::string, Engine::Render::AntiAliasing> antiAliasings{
            {"off", Engine::Render::AntiAliasing::Off},
            {"2x", Engine::Render::AntiAliasing::X2},
            {"4x", Engine::Render::AntiAliasing::X4},
        };
        return antiAliasings;
    }

    void ConsoleCommands::Register(CommandRegistry& registry, ICommandTarget& target)
    {
        auto* t = &target;
        registry.Add({"map", "map <level> [arena]", [t](const Args& args) { return Map(*t, args); }});
        registry.Add({"teleport", "teleport <x y z | arena [x y z]>",
            [t](const Args& args) { return InLevel(*t, [&](ILevelTarget& level) { return Teleport(level, args); }); },
            std::vector<std::string>{"tp"}});
        registry.Add({"pos", "pos", [t](const Args&) { return InLevel(*t, [](ILevelTarget& level) { return Pos(level.Where()); }); }});
        registry.Add({"noclip", "noclip", [t](const Args&) { return Session(*t, "noclip", t->ToggleNoclip()); }});
        registry.Add({"god", "god", [t](const Args&) { return Session(*t, "god", t->ToggleGod()); }});
        registry.Add({"onehit", "onehit", [t](const Args&) { return Session(*t, "onehit", t->ToggleOneHit()); }});
        registry.Add({"give", "give <all | SW_...>",
            [t](const Args& args) { return InLevel(*t, [&](ILevelTarget& level) { return Give(level, args); }); }});
        registry.Add({"health", "health <n>",
            [t](const Args& args) { return InLevel(*t, [&](ILevelTarget& level) { return Health(level, args); }); }});
        registry.Add({"kill", "kill",
            [t](const Args&) { return InLevel(*t, [](ILevelTarget& level) { return std::format("Killed {} enemies", level.KillEnemies()); }); }});
        registry.Add({"save", "save <slot>",
            [t](const Args& args) { return InLevel(*t, [&](ILevelTarget& level) { return Save(level, args); }); }});
        registry.Add({"load", "load <slot>", [t](const Args& args) { return Load(*t, args); }});
        registry.Add({"difficulty", "difficulty <easy|normal|hard>", [t](const Args& args) { return SetDifficulty(*t, args); }});
        registry.Add({"look", "look <original|enhanced>", [t](const Args& args) { return Look(*t, args); }});
        registry.Add({"aa", "aa <off|2x|4x>", [t](const Args& args) { return AntiAlias(*t, args); }});
        registry.Add({"stereo", "stereo <off|sbs|sbsfull|crossview|int|intr|tab|tabr|vr> [separation] [convergence]",
            [t](const Args& args) { return Stereo(*t, args); }});
        registry.Add({"timescale", "timescale <x>", [t](const Args& args) { return TimeScale(*t, args); }});
        registry.Add({"fps", "fps", [t](const Args&) { return std::format("overlay {}", Name(t->ToggleOverlay())); }});
        registry.Add({"mods", "mods", [t](const Args&) { return Mods(*t); }});
        registry.Add({"quit", "quit", [t](const Args&) { return Quit(*t); }});
    }

    std::string ConsoleCommands::Pos(const Placement& at)
    {
        auto x = Format(at.Feet.X);
        auto y = Format(at.Feet.Y);
        auto z = Format(at.Feet.Z);
        auto yaw = std::format("{:.0f}", MathF::RoundAwayFromZero(at.Yaw));
        auto level = std::format("--level={}", at.Level);
        return std::format("level {} arena {} at {} {} {} yaw {}\n", at.Level, at.Arena, x, y, z, yaw) +
            std::format("{} --at={},{},{},{}\n", level, x, y, z, yaw) +
            std::format("{} --teleport={},{},{},{}\n", level, at.Arena, x, y, z) +
            std::format("{} --console=\"tp {} {} {} {}\"", level, at.Arena, x, y, z);
    }

    std::string ConsoleCommands::Map(ICommandTarget& target, const Args& args)
    {
        int level = 0;
        if (args.size() < 1 || args.size() > 2 || !Str::TryParseInt(args[0], level))
        {
            return Usage("map <level> [arena]");
        }

        auto arena = args.size() > 1 ? Str::ToUpper(args[1]) : "";
        return target.Map(level, arena) ? Str::TrimEnd(std::format("Loading level {} {}", level, arena)) : std::format("No level {}", level);
    }

    std::string ConsoleCommands::Teleport(ILevelTarget& target, const Args& args)
    {
        auto words = Str::Split(Str::Join(" ", args), " ,", Str::SplitOptions::RemoveEmpty);
        std::vector<std::optional<float>> numbers;
        for (const auto& w : words)
        {
            float n = 0;
            numbers.push_back(Str::TryParseFloat(w, n) ? std::optional<float>(n) : std::nullopt);
        }

        auto arena = !words.empty() && !numbers[0] ? Str::ToUpper(words[0]) : "";
        std::vector<std::optional<float>> coordinates(numbers.begin() + (!arena.empty() ? 1 : 0), numbers.end());
        auto allNumbers = std::all_of(coordinates.begin(), coordinates.end(), [](const std::optional<float>& n) { return n.has_value(); });
        auto point = static_cast<int>(coordinates.size()) == Coordinates && allNumbers
            ? std::optional<Vector3>(Vector3(*coordinates[0], *coordinates[1], *coordinates[2]))
            : std::nullopt;
        auto valid = point || (!arena.empty() && coordinates.empty());
        if (!valid)
        {
            return Usage("tp <x y z | arena [x y z]>");
        }

        return target.Teleport(arena, point) ? Str::Split(Pos(target.Where()), '\n')[0] : std::format("No arena {}", arena);
    }

    std::string ConsoleCommands::Give(ILevelTarget& target, const Args& args)
    {
        if (args.size() != 1)
        {
            return Usage("give <all | SW_...>");
        }

        if (Str::EqualsIgnoreCase(args[0], All))
        {
            auto taken = 0;
            for (const auto* pickup : AllPickups)
            {
                if (target.Give(pickup))
                {
                    taken++;
                }
            }

            return std::format("Took {} pickups", taken);
        }

        auto pickup = Str::ToUpper(args[0]);
        if (!Kurt::Inventory::IsPickup(pickup))
        {
            return std::format("Not a pickup: {}", pickup);
        }

        return target.Give(pickup) ? std::format("Took {}", pickup) : std::format("Can't take {}", pickup);
    }

    std::string ConsoleCommands::Health(ILevelTarget& target, const Args& args)
    {
        int health = 0;
        if (args.size() != 1 || !Str::TryParseInt(args[0], health) || health < 0)
        {
            return Usage("health <n>");
        }

        target.SetHealth(health);
        return std::format("health {}", health);
    }

    std::string ConsoleCommands::Save(ILevelTarget& target, const Args& args)
    {
        if (args.size() != 1)
        {
            return Usage("save <slot>");
        }

        return target.Save(args[0]) ? std::format("Saved {}", args[0]) : "Can't save now";
    }

    std::string ConsoleCommands::Load(ICommandTarget& target, const Args& args)
    {
        if (args.size() != 1)
        {
            return Usage("load <slot>");
        }

        return target.Load(args[0]) ? std::format("Loading {}", args[0]) : std::format("No saved game {}", args[0]);
    }

    std::string ConsoleCommands::SetDifficulty(ICommandTarget& target, const Args& args)
    {
        auto difficulty = Kurt::Difficulty::Easy;
        if (args.size() != 1 || !TryParseEnum(args[0], DifficultyNames, difficulty))
        {
            return Usage("difficulty <easy|normal|hard>");
        }

        target.SetDifficulty(difficulty);
        return std::format("difficulty {}", Lower(DifficultyNames[static_cast<int>(difficulty)]));
    }

    std::string ConsoleCommands::Look(ICommandTarget& target, const Args& args)
    {
        auto look = Flow::Graphics::Original;
        if (args.size() != 1 || !TryParseEnum(args[0], GraphicsNames, look))
        {
            return Usage("look <original|enhanced>");
        }

        auto name = Lower(GraphicsNames[static_cast<int>(look)]);
        return target.SetLook(look) ? std::format("look {}", name) : std::format("look {}{}", name, NextLevel);
    }

    std::string ConsoleCommands::AntiAlias(ICommandTarget& target, const Args& args)
    {
        if (args.size() != 1)
        {
            return Usage("aa <off|2x|4x>");
        }

        auto found = AntiAliasings().find(Str::ToLower(args[0]));
        if (found == AntiAliasings().end())
        {
            return Usage("aa <off|2x|4x>");
        }

        target.SetAntiAliasing(found->second);
        return std::format("aa {}", Str::ToLower(args[0]));
    }

    std::string ConsoleCommands::Stereo(ICommandTarget& target, const Args& args)
    {
        const std::string usage = "stereo <off|sbs|sbsfull|crossview|int|intr|tab|tabr|vr> [separation] [convergence]";
        auto stereo = Engine::Render::Stereo::Off;
        if (args.size() < 1 || args.size() > 3 || !Engine::Render::StereoModes::TryParse(args[0], stereo))
        {
            return Usage(usage);
        }

        std::optional<float> separation;
        std::optional<float> convergence;
        float separationValue = 0;
        if (args.size() > 1 && Str::TryParseFloat(args[1], separationValue))
        {
            separation = separationValue;
        }
        else if (args.size() > 1)
        {
            return Usage(usage);
        }

        float convergenceValue = 0;
        if (args.size() > 2 && Str::TryParseFloat(args[2], convergenceValue))
        {
            convergence = convergenceValue;
        }
        else if (args.size() > 2)
        {
            return Usage(usage);
        }

        target.SetStereo(stereo, separation, convergence);
        return std::format("stereo {}", Str::ToLower(args[0]));
    }

    std::string ConsoleCommands::TimeScale(ICommandTarget& target, const Args& args)
    {
        float scale = 0;
        if (args.size() != 1 || !Str::TryParseFloat(args[0], scale) || scale <= 0.f || scale > MaxTimeScale)
        {
            return Usage(std::format("timescale <x> (0 to {})", Str::FromFloat(MaxTimeScale)));
        }

        target.SetTimeScale(scale);
        return std::format("timescale {}", Str::FromFloat(scale));
    }

    std::string ConsoleCommands::Mods(ICommandTarget& target)
    {
        auto list = target.ModList();
        auto lines = !list.empty() ? list : std::vector<std::string>{"No mods in mods/"};
        if (auto level = target.Level())
        {
            auto modLines = level->Mods().Lines();
            lines.insert(lines.end(), modLines.begin(), modLines.end());
        }

        return Str::Join("\n", lines);
    }

    std::string ConsoleCommands::Quit(ICommandTarget& target)
    {
        target.Quit();
        return "";
    }

    std::string ConsoleCommands::InLevel(ICommandTarget& target, const std::function<std::string(ILevelTarget&)>& run)
    {
        auto level = target.Level();
        return level ? run(*level) : NotInLevel;
    }

    std::string ConsoleCommands::Session(ICommandTarget& target, const std::string& name, Switch value)
    {
        return std::format("{} {}{}", name, Name(value), target.Level() == nullptr ? NextLevel : "");
    }

    std::string ConsoleCommands::Name(Switch value)
    {
        return Lower(SwitchNames[static_cast<int>(value)]);
    }

    std::string ConsoleCommands::Lower(const std::string& name)
    {
        return Str::ToLower(name);
    }

    std::string ConsoleCommands::Format(float value)
    {
        return std::format("{:.2f}", value);
    }
}
