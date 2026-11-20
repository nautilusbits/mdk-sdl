#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <doctest.h>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/DevTools/CommandRegistry.h"
#include "Mdk.Game/DevTools/ConsoleCommands.h"
#include "Mdk.Game/DevTools/ICommandTarget.h"
#include "Mdk.Game/Flow/Settings.h"
#include "Mdk.Game/Kurt/Inventory.h"
#include "Mdk.Game/Mods/ModReport.h"

using namespace Mdk;
using namespace Mdk::Engine::Render;
using namespace Mdk::Game::DevTools;
using Mdk::Game::Flow::Graphics;
using Mdk::Game::Kurt::Difficulty;
using Mdk::Game::Mods::ModReport;

// The console's commands: their arguments reach the game and the level (fakes here).

namespace
{
    bool Contains(const std::vector<std::string>& list, const std::string& item)
    {
        return std::find(list.begin(), list.end(), item) != list.end();
    }

    class FakeLevel : public ILevelTarget
    {
    public:
        std::optional<std::pair<std::string, std::optional<Vector3>>> Teleported;
        int Health = 0;
        Difficulty Difficulty{};
        Switch God{};
        Switch Noclip{};
        Switch OneHit{};
        std::optional<std::string> Saved;
        std::vector<std::string> Given;

        const ModReport& Mods() const override { return _mods; }

        Placement Where() override { return Placement{3, "ARENA_1", Vector3(-4.f, 0.5f, 195.f), 90.f}; }

        bool Teleport(const std::string& arena, std::optional<Vector3> point) override
        {
            Teleported = std::make_pair(arena, point);
            return true;
        }

        void SetGod(Switch god) override { God = god; }

        void SetNoclip(Switch noclip) override { Noclip = noclip; }

        void SetOneHit(Switch oneHit) override { OneHit = oneHit; }

        bool Give(const std::string& pickup) override
        {
            Given.push_back(pickup);
            return true;
        }

        void SetHealth(int health) override { Health = health; }

        int KillEnemies() override { return 2; }

        bool Save(const std::string& slot) override
        {
            Saved = slot;
            return true;
        }

        void SetDifficulty(Game::Kurt::Difficulty difficulty) override { Difficulty = difficulty; }

        std::optional<std::string> Snapshot() override { return "{}"; }

    private:
        ModReport _mods{1, {"WALL"}, {"GRUNT"}};
    };

    // The game: the session's switches reach the level, when there's one.
    class FakeGame : public ICommandTarget
    {
    public:
        explicit FakeGame(std::shared_ptr<FakeLevel> level) : CurrentLevel(std::move(level)) {}

        // C#'s settable Level property.
        std::shared_ptr<ILevelTarget> CurrentLevel;
        std::optional<std::pair<int, std::string>> Mapped;
        Difficulty Difficulty{};
        Graphics Look{};
        AntiAliasing AntiAliasing{};
        float TimeScale = 1.f;
        int Overlays = 0;
        bool Quitted = false;

        std::shared_ptr<ILevelTarget> Level() override { return CurrentLevel; }

        bool Map(int number, const std::string& arena) override
        {
            if (number > 8)
            {
                return false;
            }

            Mapped = std::make_pair(number, arena);
            return true;
        }

        bool Load(const std::string&) override { return false; }

        Switch ToggleGod() override
        {
            _god = _god == Switch::On ? Switch::Off : Switch::On;
            if (CurrentLevel)
            {
                CurrentLevel->SetGod(_god);
            }

            return _god;
        }

        Switch ToggleNoclip() override
        {
            _noclip = _noclip == Switch::On ? Switch::Off : Switch::On;
            if (CurrentLevel)
            {
                CurrentLevel->SetNoclip(_noclip);
            }

            return _noclip;
        }

        Switch ToggleOneHit() override
        {
            _oneHit = _oneHit == Switch::On ? Switch::Off : Switch::On;
            if (CurrentLevel)
            {
                CurrentLevel->SetOneHit(_oneHit);
            }

            return _oneHit;
        }

        void SetDifficulty(Game::Kurt::Difficulty difficulty) override
        {
            Difficulty = difficulty;
            if (CurrentLevel)
            {
                CurrentLevel->SetDifficulty(difficulty);
            }
        }

        bool SetLook(Graphics look) override
        {
            Look = look;
            return CurrentLevel != nullptr;
        }

        void SetAntiAliasing(Engine::Render::AntiAliasing antiAliasing) override { AntiAliasing = antiAliasing; }

        Stereo Stereo{};
        std::optional<float> StereoSeparation;
        std::optional<float> StereoConvergence;

        void SetStereo(Engine::Render::Stereo stereo, std::optional<float> separation, std::optional<float> convergence) override
        {
            Stereo = stereo;
            StereoSeparation = separation;
            StereoConvergence = convergence;
        }

        void SetTimeScale(float scale) override { TimeScale = scale; }

        Switch ToggleOverlay() override
        {
            Overlays++;
            return Switch::On;
        }

        void Quit() override { Quitted = true; }

        std::vector<std::string> ModList() override { return {"Clean walls (walls) on, priority 0"}; }

    private:
        Switch _god{};
        Switch _noclip{};
        Switch _oneHit{};
    };
}

// C#'s test class: a fresh fake level, game and registry per test.
struct ConsoleCommandsTests
{
    std::shared_ptr<FakeLevel> _level = std::make_shared<FakeLevel>();
    FakeGame _game{_level};
    CommandRegistry _registry;

    ConsoleCommandsTests() { ConsoleCommands::Register(_registry, _game); }

    std::string Run(const std::string& line) { return Str::Join("\n", _registry.Run(line)); }
};

TEST_CASE_FIXTURE(ConsoleCommandsTests, "ConsoleCommandsTests.PosPrintsCommandLineOptions")
{
    auto text = Run("pos");
    CHECK(Str::Contains(text, "--level=3 --at=-4.00,0.50,195.00,90"));
    CHECK(Str::Contains(text, "--teleport=ARENA_1,-4.00,0.50,195.00"));
    CHECK(Str::Contains(text, "--console=\"tp ARENA_1 -4.00 0.50 195.00\""));
}

TEST_CASE("ConsoleCommandsTests.TeleportTakesAnArenaOrAPoint")
{
    struct Row
    {
        const char* Line;
        const char* Arena;
        std::optional<std::string> Point;
    };

    const Row rows[] = {
        {"tp ARENA_2", "ARENA_2", std::nullopt},
        {"teleport 1 2 3", "", "1 2 3"},
        {"tp 1,2,3", "", "1 2 3"},
        {"tp ARENA_2,1,2,3", "ARENA_2", "1 2 3"},
    };

    for (const auto& row : rows)
    {
        CAPTURE(row.Line);
        ConsoleCommandsTests t;
        t.Run(row.Line);
        REQUIRE(t._level->Teleported.has_value());
        CHECK(std::string(row.Arena) == t._level->Teleported->first);

        std::optional<std::string> point;
        if (auto p = t._level->Teleported->second)
        {
            point = Str::FromFloat(p->X) + " " + Str::FromFloat(p->Y) + " " + Str::FromFloat(p->Z);
        }

        CHECK(row.Point == point);
    }
}

TEST_CASE_FIXTURE(ConsoleCommandsTests, "ConsoleCommandsTests.BadArgumentsShowTheUsage")
{
    CHECK(Str::StartsWith(Run("health lots"), "Usage: health"));
    CHECK(Str::StartsWith(Run("tp 1 2"), "Usage: tp"));
    CHECK(Str::StartsWith(Run("map"), "Usage: map"));
    CHECK(Str::StartsWith(Run("difficulty insane"), "Usage: difficulty"));
    CHECK_FALSE(_level->Teleported.has_value());
}

TEST_CASE_FIXTURE(ConsoleCommandsTests, "ConsoleCommandsTests.MapTakesALevelAndAnArena")
{
    Run("map 6 ARENA_3");
    CHECK(_game.Mapped == std::make_pair(6, std::string("ARENA_3")));
    Run("map 4");
    CHECK(_game.Mapped == std::make_pair(4, std::string("")));
    CHECK(Str::StartsWith(Run("map 9"), "No level 9"));
}

TEST_CASE_FIXTURE(ConsoleCommandsTests, "ConsoleCommandsTests.TogglesAndSettings")
{
    CHECK("god on" == Run("god"));
    CHECK(Switch::On == _level->God);
    CHECK("god off" == Run("god"));
    CHECK("noclip on" == Run("noclip"));
    CHECK(Switch::On == _level->Noclip);
    Run("health 50");
    CHECK(50 == _level->Health);
    Run("difficulty hard");
    CHECK(Difficulty::Hard == _game.Difficulty);
    CHECK(Difficulty::Hard == _level->Difficulty);
    CHECK("look enhanced" == Run("look enhanced"));
    CHECK(Graphics::Enhanced == _game.Look);
    Run("aa 4x");
    CHECK(AntiAliasing::X4 == _game.AntiAliasing);
    CHECK("stereo intr" == Run("stereo intr"));
    CHECK(Stereo::InterlacedReversed == _game.Stereo);
    CHECK("stereo tabr" == Run("stereo tabr"));
    CHECK(Stereo::TabReversed == _game.Stereo);
    CHECK("stereo tab" == Run("stereo tab"));
    CHECK(Stereo::Tab == _game.Stereo);
    Run("stereo sbs 0.5 6");
    CHECK(std::make_tuple(Stereo::Sbs, std::optional<float>(0.5f), std::optional<float>(6.f))
        == std::make_tuple(_game.Stereo, _game.StereoSeparation, _game.StereoConvergence));
    CHECK(Str::StartsWith(Run("stereo sideways"), "Usage"));
    Run("timescale 0.5");
    CHECK(0.5f == _game.TimeScale);
    CHECK(Str::StartsWith(Run("timescale -1"), "Usage"));
    Run("fps");
    CHECK(1 == _game.Overlays);
    Run("quit");
    CHECK(_game.Quitted);
}

TEST_CASE_FIXTURE(ConsoleCommandsTests, "ConsoleCommandsTests.GiveTakesOneOrAll")
{
    Run("give sw_hbomb");
    CHECK(std::vector<std::string>{"SW_HBOMB"} == _level->Given);
    Run("give all");
    CHECK(Contains(_level->Given, "SW_GATT"));
    CHECK(Contains(_level->Given, "SW_H150"));
    CHECK(Str::StartsWith(Run("give apple"), "Not a pickup"));
}

TEST_CASE_FIXTURE(ConsoleCommandsTests, "ConsoleCommandsTests.KillSaveLoad")
{
    CHECK("Killed 2 enemies" == Run("kill"));
    Run("save 7");
    CHECK(std::optional<std::string>("7") == _level->Saved);
    CHECK(Str::StartsWith(Run("load nothing"), "No saved game"));
}

// Menus, the fall, the stream: the level's commands answer, the game's work.
TEST_CASE("ConsoleCommandsTests.LevelCommandsNeedALevel")
{
    const char* lines[] = {"pos", "tp ARENA_2", "give all", "health 50", "kill", "save 7"};
    for (const auto* line : lines)
    {
        CAPTURE(line);
        ConsoleCommandsTests t;
        t._game.CurrentLevel = nullptr;
        CHECK("Not in a level" == t.Run(line));
        CHECK_FALSE(t._level->Teleported.has_value());
        CHECK(t._level->Given.empty());
    }
}

TEST_CASE_FIXTURE(ConsoleCommandsTests, "ConsoleCommandsTests.GameCommandsWorkOutsideALevel")
{
    _game.CurrentLevel = nullptr;
    Run("map 6");
    CHECK(_game.Mapped == std::make_pair(6, std::string("")));
    Run("difficulty easy");
    CHECK(Difficulty::Easy == _game.Difficulty);
    CHECK("look enhanced from the next level" == Run("look enhanced"));
    Run("timescale 2");
    CHECK(2.f == _game.TimeScale);
    Run("fps");
    CHECK(1 == _game.Overlays);

    // God and noclip are the session's: they hold from the next level.
    CHECK("god on from the next level" == Run("god"));
    CHECK("noclip on from the next level" == Run("noclip"));
    CHECK("onehit on from the next level" == Run("onehit"));
    CHECK(Switch::Off == _level->God);
}

TEST_CASE_FIXTURE(ConsoleCommandsTests, "ConsoleCommandsTests.OneHitTogglesInTheLevel")
{
    CHECK("onehit on" == Run("onehit"));
    CHECK(Switch::On == _level->OneHit);
    CHECK("onehit off" == Run("onehit"));
    CHECK(Switch::Off == _level->OneHit);
}

// The mods found, then what the level replaced (outside a level: the list only).
TEST_CASE_FIXTURE(ConsoleCommandsTests, "ConsoleCommandsTests.ModsListsWhatTheLevelTook")
{
    CHECK("Clean walls (walls) on, priority 0\nmods 1: 1 images, 1 models\nimages: WALL\nmodels: GRUNT" == Run("mods"));

    _game.CurrentLevel = nullptr;
    CHECK("Clean walls (walls) on, priority 0" == Run("mods"));
}
