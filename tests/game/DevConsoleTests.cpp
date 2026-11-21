#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <SDL3/SDL_scancode.h>
#include <doctest.h>

#include "core/DotNet.h"
#include "core/Drawing.h"
#include "core/Numerics.h"
#include "engine/diagnostics/LogRing.h"
#include "engine/platform/Input.h"
#include "game/dev_tools/CommandHistory.h"
#include "game/dev_tools/CommandRegistry.h"
#include "game/dev_tools/CommandText.h"
#include "game/dev_tools/ConsoleView.h"
#include "game/dev_tools/DevConsole.h"
#include "game/dev_tools/DevUi.h"

using namespace Mdk;
using namespace Mdk::Engine::Diagnostics;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game::DevTools;

using Strings = std::vector<std::string>;

// The console's parts: parsing, the registry, completion, history, the input line, its keys.

namespace
{
    constexpr float Step = 1.f / 60.f;

    // A console with a "health" command that records its lines; its output goes to log.
    std::shared_ptr<DevConsole> NewConsole(LogRing& log, std::shared_ptr<Strings>& ran)
    {
        auto lines = std::make_shared<Strings>();
        ran = lines;
        auto registry = std::make_shared<CommandRegistry>();
        registry->Add(Command{"health", "health <n>", [lines](const Strings& args) { lines->push_back("health " + Str::Join(" ", args)); return std::string(); }});
        return std::make_shared<DevConsole>(registry, std::make_shared<CommandHistory>(), log, [&log](const std::string& line) { log.Add(line); });
    }

    std::shared_ptr<DevConsole> NewConsole(LogRing& log)
    {
        std::shared_ptr<Strings> ran;
        return NewConsole(log, ran);
    }

    bool Contains(const Strings& list, const std::string& item)
    {
        return std::find(list.begin(), list.end(), item) != list.end();
    }
}

TEST_CASE("DevConsoleTests.TouchesOnTheConsoleFindItsParts")
{
    // The open console (bottom at 180) on a phone's 800-wide canvas, a cut-out's 69 on the left.
    auto close = ConsoleView::CloseArea(180.f, RectangleF::FromLTRB(69.f, 0.f, 774.f, 360.f));

    CHECK(RectangleF(738.f, 4.f, 32.f, 32.f) == close);
    CHECK(ConsoleSpot::Close == ConsoleView::Spot(Vector2(750.f, 20.f), 180.f, close));
    CHECK(ConsoleSpot::Log == ConsoleView::Spot(Vector2(300.f, 100.f), 180.f, close));
    CHECK(ConsoleSpot::Line == ConsoleView::Spot(Vector2(300.f, 175.f), 180.f, close));
    CHECK(ConsoleSpot::Below == ConsoleView::Spot(Vector2(300.f, 250.f), 180.f, close));
}

TEST_CASE("DevConsoleTests.LinesSplitIntoCommandsAndWords")
{
    auto commands = CommandText::Parse("  tp  ARENA_1 ; god;; health 50 ");
    REQUIRE(3 == commands.size());
    CHECK(Strings{"tp", "ARENA_1"} == commands[0]);
    CHECK(Strings{"god"} == commands[1]);
    CHECK(Strings{"health", "50"} == commands[2]);
}

TEST_CASE("DevConsoleTests.RegistryRunsCommandsByNameOrAlias")
{
    CommandRegistry registry;
    Strings seen;
    registry.Add(Command{"teleport", "teleport <arena>", [&seen](const Strings& args) { seen.insert(seen.end(), args.begin(), args.end()); return std::string("done"); }, Strings{"tp"}});

    CHECK(Strings{"done"} == registry.Run("TP ARENA_1"));
    CHECK(Strings{"ARENA_1"} == seen);
    CHECK(Strings{"Unknown command: fly"} == registry.Run("fly"));
}

TEST_CASE("DevConsoleTests.CompletionFindsCommandNames")
{
    CommandRegistry registry;
    for (const auto* name : {"god", "give", "health"})
    {
        registry.Add(Command{name, name, [](const Strings&) { return std::string(); }});
    }

    CHECK("health " == std::get<0>(registry.Complete("he")));
    auto [line, matches] = registry.Complete("g");
    CHECK("g" == line);
    CHECK(Strings{"give", "god"} == matches);
    CHECK("gi" == std::get<0>(registry.Complete("gi")).substr(0, 2));
    CHECK("give " == std::get<0>(registry.Complete("gi")));

    // Only the command's name completes.
    CHECK("give al" == std::get<0>(registry.Complete("give al")));
}

TEST_CASE("DevConsoleTests.HistoryWalksBackAndForth")
{
    CommandHistory history;
    history.Add("god");
    history.Add("pos");
    history.Add("pos");

    CHECK("pos" == history.Previous());
    CHECK("god" == history.Previous());
    CHECK("god" == history.Previous());
    CHECK("pos" == history.Next());
    CHECK("" == history.Next());
}

TEST_CASE("DevConsoleTests.ConsoleEditsRunsAndRemembers")
{
    LogRing log(10);
    std::shared_ptr<Strings> ran;
    auto console = NewConsole(log, ran);
    console->Toggle();
    CHECK(ConsoleState::Open == console->State());

    console->Type("hea");
    console->Press(EditKey::Complete);
    CHECK("health " == console->Line());
    console->Type("51");
    console->Press(EditKey::Erase);
    console->Type("0");
    console->Press(EditKey::Submit);

    CHECK(Strings{"health 50"} == *ran);
    CHECK("" == console->Line());
    CHECK(Contains(log.Lines(), "] health 50"));

    console->Press(EditKey::Previous);
    CHECK("health 50" == console->Line());
}

TEST_CASE("DevConsoleTests.ConsoleClearsTheLogAndListsCommands")
{
    LogRing log(10);
    auto console = NewConsole(log);
    console->Run("help");
    auto lines = log.Lines();
    CHECK(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return Str::StartsWith(l, "health"); }));

    console->Run("clear");
    CHECK(log.Lines().empty());
}

TEST_CASE("DevConsoleTests.ConsoleKeysAreFixedByPlace")
{
    Input input;
    input.BeginFrame();

    // The key left of 1, whatever the layout types there (";" on a Slovak keyboard).
    input.SetKey(SDL_SCANCODE_GRAVE, Input::State::Down, Repeat::No);
    input.SetKey(SDL_SCANCODE_F3, Input::State::Down, Repeat::No);
    input.SetKey(SDL_SCANCODE_BACKSPACE, Input::State::Down, Repeat::Yes);
    CHECK(input.WasPressed(RawKey::Grave));
    CHECK(input.WasPressed(RawKey::F3));
    CHECK(input.WasPressed(RawKey::Backspace));
    CHECK_FALSE(input.WasPressed(RawKey::Tab));

    input.BeginFrame();
    CHECK_FALSE(input.WasPressed(RawKey::Grave));
}

// A phone's Back button (no Escape key there) closes the open console.
TEST_CASE("DevConsoleTests.BackClosesTheConsole")
{
    LogRing log(10);
    auto console = NewConsole(log);
    console->Toggle();
    Input input;
    input.BeginFrame();
    input.SetKey(SDL_SCANCODE_AC_BACK, Input::State::Down, Repeat::No);

    console->Update(input, 0.f);

    CHECK(ConsoleState::Closed == console->State());
}

// Any screen: while the console is open (and on the frame Grave opens it) the screen
// gets no keys, typing, clicks or mouse; keys held by tests stay. Closed, input passes.
TEST_CASE("DevConsoleTests.OpenConsoleTakesTheScreensInput")
{
    LogRing log(10);
    auto console = NewConsole(log);
    auto overlays = 0;
    DevUi dev(console, [&overlays] { overlays++; });
    Input input;

    input.BeginFrame();
    input.SetKey(SDL_SCANCODE_W, Input::State::Down, Repeat::No);
    input.SetKey(SDL_SCANCODE_F3, Input::State::Down, Repeat::No);
    CHECK(ConsoleState::Closed == dev.Update(input, Step));
    CHECK(input.WasPressed(Key::Forward));
    CHECK(1 == overlays);

    input.BeginFrame();
    input.SetKey(SDL_SCANCODE_W, Input::State::Up, Repeat::No);
    input.SetKey(SDL_SCANCODE_GRAVE, Input::State::Down, Repeat::No);
    input.AddText("`");
    CHECK(ConsoleState::Open == dev.Update(input, Step));
    CHECK("" == input.Typed());

    input.BeginFrame();
    input.Hold(Key::Jump, Input::State::Down);
    input.SetKey(SDL_SCANCODE_W, Input::State::Down, Repeat::No);
    input.SetButton(MouseButton::Left, Input::State::Down);
    input.AddMouseMotion(5.f, 5.f);
    input.AddText("w");
    CHECK(ConsoleState::Open == dev.Update(input, Step));
    CHECK("w" == console->Line());
    CHECK_FALSE(input.WasPressed(Key::Forward));
    CHECK_FALSE(input.IsDown(Key::Forward));
    CHECK_FALSE(input.WasClicked(Pointer::Left));
    CHECK(0.f == input.MouseX());
    CHECK("" == input.Typed());
    CHECK(input.IsDown(Key::Jump));
}

TEST_CASE("DevConsoleTests.KeyboardCorrectionsEraseInOrder")
{
    // A phone's keyboard corrects "fps" to "gps": three Backspaces, then the new word, in one frame.
    LogRing log(10);
    auto console = NewConsole(log);
    console->Toggle();
    Input input;
    input.BeginFrame();
    input.AddText("fps");
    console->Update(input, Step);

    input.BeginFrame();
    for (auto i = 0; i < 3; i++)
    {
        input.SetKey(SDL_SCANCODE_BACKSPACE, Input::State::Down, Repeat::No);
        input.SetKey(SDL_SCANCODE_BACKSPACE, Input::State::Up, Repeat::No);
    }

    input.AddText("gps");
    console->Update(input, Step);
    CHECK("gps" == console->Line());
}

TEST_CASE("DevConsoleTests.ConsoleOptionRunsOnce")
{
    LogRing log(10);
    std::shared_ptr<Strings> ran;
    auto console = NewConsole(log, ran);
    DevUi dev(console, [] {});
    dev.RunOnce("health 5");
    dev.RunOnce("health 6");
    CHECK(Strings{"health 5"} == *ran);
    CHECK(ConsoleState::Open == console->State());
}
