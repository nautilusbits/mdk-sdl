#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include <doctest.h>

#include "core/Files.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"
#include "game/flow/SaveGames.h"
#include "game/flow/Settings.h"
#include "game/flow/TestPresses.h"
#include "game/menu/PauseMenu.h"
#include "game/menu/SavePrompt.h"
#include "game/menu/Ui.h"
#include "shared/TempDir.h"

using namespace Mdk;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Game::Flow;
using namespace Mdk::Game::Menu;

// Quick save (F2, the original's full save) and quick load (F9): bindings, settings,
// the pause menu's hint and the save quick load picks.
namespace
{
    Input NewInput()
    {
        Input input;
        input.BeginFrame();
        return input;
    }

    SaveGame Snapshot(int level) { return {SaveKind::Snapshot, level, SaveGame::FullHealth, 0, false, ""}; }

    bool ListsAction(Key key, const std::string& name)
    {
        const auto& actions = Settings::Actions();
        return std::find(actions.begin(), actions.end(), std::pair<Key, std::string>(key, name)) != actions.end();
    }

    // The C# passes null! for the Ui, the fonts and the saves: naming never touches them. The
    // saves are copied by value here, so they're an unused empty set.
    SavePrompt Prompt(const SaveGame& save, const std::string& name)
    {
        Ui* ui = nullptr;
        return SavePrompt(*ui, nullptr, SaveGames(""), save, name);
    }
}

TEST_CASE("QuickSaveTests.QuickKeysDefaultToF2AndF9")
{
    auto input = NewInput();

    input.SetKey(SDL_SCANCODE_F2, Input::State::Down, Repeat::No);
    input.SetKey(SDL_SCANCODE_F9, Input::State::Down, Repeat::No);

    CHECK(input.WasPressed(Key::QuickSave));
    CHECK(input.WasPressed(Key::QuickLoad));
    CHECK(std::string("F2") == input.Describe(Key::QuickSave));
    CHECK(std::string("F9") == input.Describe(Key::QuickLoad));
}

TEST_CASE("QuickSaveTests.OldSettingsGetQuickDefaults")
{
    auto input = NewInput();
    auto read = Settings::Parse("master_volume=80\nbind.Jump=Right Ctrl\n");

    read.Bind(input);

    CHECK_FALSE(read.Bindings.ContainsKey(Key::QuickSave));
    CHECK(std::string("F2") == input.Describe(Key::QuickSave));
    CHECK(std::string("F9") == input.Describe(Key::QuickLoad));
}

TEST_CASE("QuickSaveTests.QuickBindingsRoundTrip")
{
    auto input = NewInput();
    Settings settings;
    settings.Bindings[Key::QuickSave] = "F5";
    settings.Bindings[Key::QuickLoad] = "F6";

    auto read = Settings::Parse(settings.Format());
    read.Bind(input);

    CHECK(settings.Format().find("bind.QuickLoad=F6") != std::string::npos);
    CHECK(std::string("F5") == input.Describe(Key::QuickSave));
    CHECK(std::string("F6") == input.Describe(Key::QuickLoad));
}

TEST_CASE("QuickSaveTests.ControlsListQuickActions")
{
    CHECK(ListsAction(Key::QuickSave, "Quick save"));
    CHECK(ListsAction(Key::QuickLoad, "Quick load"));
}

TEST_CASE("QuickSaveTests.PauseHintNamesBoundKeys")
{
    auto input = NewInput();
    CHECK(std::string("Quick save: F2   Quick load: F9") == PauseMenu::KeysHint(input));

    input.Bind(Key::QuickLoad, "F5");

    CHECK(std::string("Quick save: F2   Quick load: F5") == PauseMenu::KeysHint(input));
}

TEST_CASE("QuickSaveTests.QuickLoadPicksSessionSaveElseNewest")
{
    Mdk::Tests::TempDir temp("mdk-quick-");
    auto folder = Path::Combine(temp.Path(), "mdk_quick");
    SaveGames saves(folder);
    auto save = Snapshot(7);

    CHECK(saves.QuickSlot(std::nullopt) == std::nullopt);

    saves.Write("OLD", save);
    saves.Write("NEW", save);
    auto old = std::filesystem::path(Path::Combine(folder, "OLD.sav"));
    std::filesystem::last_write_time(old, std::filesystem::file_time_type::clock::now() - std::chrono::hours(1));

    CHECK(std::optional<std::string>("NEW") == saves.QuickSlot(std::nullopt));
    CHECK(std::optional<std::string>("OLD") == saves.QuickSlot("OLD"));
    CHECK(std::optional<std::string>("NEW") == saves.QuickSlot("GONE"));
}

TEST_CASE("QuickSaveTests.TestPressesFireOnceAtTheirTime")
{
    auto input = NewInput();
    auto presses = TestPresses::Parse("QuickSave@1,Menu.Accept@1.5");

    presses.Apply(input, 0.5f);
    CHECK_FALSE(input.WasPressed(Key::QuickSave));

    presses.Apply(input, 1.f);
    CHECK(input.WasPressed(Key::QuickSave));
    CHECK_FALSE(input.WasPressed(MenuKey::Accept));

    input.BeginFrame();
    presses.Apply(input, 2.f);
    CHECK_FALSE(input.WasPressed(Key::QuickSave));
    CHECK(input.WasPressed(MenuKey::Accept));
}

TEST_CASE("QuickSaveTests.NameCorrectionsEraseInOrder")
{
    auto prompt = Prompt(Snapshot(3), "");
    auto input = NewInput();
    input.AddText("kurt");
    prompt.Update(input, 0.f);

    // The keyboard's correction: two Backspaces, then "ts", in one frame.
    input.BeginFrame();
    input.SetKey(SDL_SCANCODE_BACKSPACE, Input::State::Down, Repeat::No);
    input.SetKey(SDL_SCANCODE_BACKSPACE, Input::State::Up, Repeat::No);
    input.SetKey(SDL_SCANCODE_BACKSPACE, Input::State::Down, Repeat::No);
    input.SetKey(SDL_SCANCODE_BACKSPACE, Input::State::Up, Repeat::No);
    input.AddText("ts");
    prompt.Update(input, 0.f);
    CHECK(std::string("KUTS") == prompt.Name());
}

TEST_CASE("QuickSaveTests.NamePromptAsksForKeyboard")
{
    auto snapshot = Snapshot(3);
    auto afterLevel = snapshot;
    afterLevel.Kind = SaveKind::BeforeLevel;

    // A touch screen's keyboard: while the name is typed, not for Yes or No.
    auto prompt = Prompt(snapshot, "4");
    CHECK(Typing::On == prompt.Typing());
    CHECK(Typing::Off == Prompt(afterLevel, "4").Typing());

    auto input = NewInput();
    input.Press(MenuKey::Back);
    prompt.Update(input, 0.f);
    CHECK(prompt.Closed());
    CHECK(Typing::Off == prompt.Typing());
}
