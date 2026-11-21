#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <doctest.h>

#include "Asserts.h"
#include "core/DotNet.h"
#include "engine/platform/Display.h"
#include "engine/platform/HeadTracker.h"
#include "engine/platform/Input.h"
#include "engine/platform/TouchControls.h"
#include "engine/render/Renderer.h"
#include "game/flow/GameState.h"
#include "game/flow/LevelFlow.h"
#include "game/flow/SaveGames.h"
#include "game/flow/Settings.h"
#include "game/hd_textures/HdGenerator.h"
#include "game/kurt/Inventory.h"
#include "game/menu/EndMovie.h"
#include "game/menu/IntroSplash.h"
#include "game/menu/MenuItems.h"
#include "game/menu/StatsScreen.h"
#include "game/menu/Typesetter.h"
#include "game/mods/ModCatalog.h"
#include "game/scripts/GameStats.h"
#include "game/scripts/ScriptRuntime.h"

using namespace Mdk;
using namespace Mdk::Engine::Platform;
using namespace Mdk::Engine::Render;
using namespace Mdk::Game::Flow;
using namespace Mdk::Game::Kurt;
using namespace Mdk::Game::Menu;
using namespace Mdk::Game::Scripts;

// The menus, saves and level flow (game_state.gd, settings.gd, menu_items.gd, stats_screen.gd).

namespace
{
    std::tuple<std::string, float, float, bool, int> Fields(const Typesetter::Line& line)
    {
        return {std::string(line.Text.begin(), line.Text.end()), line.X, line.Y, line.Centred, line.Start};
    }

    std::vector<byte> Ascii(std::string_view text) { return std::vector<byte>(text.begin(), text.end()); }

    // Path.GetTempPath()/mdk-saves-{Guid:N}.
    std::string TempFolder()
    {
        std::random_device device;
        std::mt19937_64 random(device());
        constexpr const char* Hex = "0123456789abcdef";
        constexpr int GuidDigits = 32;
        std::string name = "mdk-saves-";
        for (int i = 0; i < GuidDigits; i++)
        {
            name += Hex[random() % 16];
        }

        return (std::filesystem::temp_directory_path() / name).string();
    }
}

TEST_CASE("FlowTests.LevelsPlayInOrder")
{
    auto level = GameState::Order[0];
    std::vector<int> played{level};
    while (LevelFlow::AfterLevel(level, ScriptRuntime::GameOver::No) != LevelFlow::After::Menu)
    {
        level = LevelFlow::Next(level);
        played.push_back(level);
    }

    CHECK(played == std::vector<int>{7, 6, 3, 4, 8, 5});
}

TEST_CASE("FlowTests.AfterLevel")
{
    const std::pair<int, LevelFlow::After> rows[] = {
        {7, LevelFlow::After::Statistics},
        {4, LevelFlow::After::Statistics},
        {8, LevelFlow::After::LastLevel},
        {5, LevelFlow::After::Menu},
    };
    for (auto [level, after] : rows)
    {
        CAPTURE(level);
        CHECK(LevelFlow::AfterLevel(level, ScriptRuntime::GameOver::No) == after);
    }
}

TEST_CASE("FlowTests.GameOverGoesToMenu")
{
    CHECK(LevelFlow::AfterLevel(7, ScriptRuntime::GameOver::Yes) == LevelFlow::After::Menu);
}

TEST_CASE("FlowTests.SaveNameIsLevelNumberInOrder")
{
    CHECK(LevelFlow::SaveName(7) == "1");
    CHECK(LevelFlow::SaveName(5) == "6");
}

TEST_CASE("FlowTests.SettingsRoundTrip")
{
    Settings settings;
    settings.MasterVolume = 30;
    settings.MusicVolume = 0;
    settings.EffectsVolume = 100;
    settings.MusicFilter = false;
    settings.MouseSensitivity = 2.5f;
    settings.InvertMouse = true;
    settings.Fullscreen = Fullscreen::Desktop;
    settings.Difficulty = Difficulty::Hard;
    settings.Gore = false;
    settings.Graphics = Graphics::Enhanced;
    settings.AntiAliasing = AntiAliasing::X4;
    settings.Stereo = Stereo::InterlacedReversed;
    settings.StereoSeparation = 0.4f;
    settings.StereoConvergence = 12.f;
    settings.HeadTracking = HeadTracking::On;
    settings.Bindings[Key::Fire] = "Left mouse";
    settings.Bindings[Key::Jump] = "Right Ctrl";
    settings.PadBindings[Key::Fire] = "RB";
    settings.PadSensitivity = 1.5f;
    settings.InvertPad = true;
    settings.Touch = TouchButtons::Off;

    auto read = Settings::Parse(settings.Format());

    CHECK(read.Format() == settings.Format());
    CHECK(read.Difficulty == Difficulty::Hard);
    CHECK(read.Graphics == Graphics::Enhanced);
    CHECK(read.AntiAliasing == AntiAliasing::X4);
    CHECK(read.Stereo == Stereo::InterlacedReversed);
    CHECK(read.StereoSeparation == 0.4f);
    CHECK(read.HeadTracking == HeadTracking::On);
    CHECK(read.Bindings.At(Key::Jump) == "Right Ctrl");
    CHECK(read.PadBindings.At(Key::Fire) == "RB");
    CHECK(read.PadSensitivity == 1.5f);
    CHECK(read.InvertPad);
    CHECK(read.Touch == TouchButtons::Off);
}

TEST_CASE("FlowTests.BadSettingsKeepDefaults")
{
    auto read = Settings::Parse("master_volume=loud\nmusic_volume=500\nunknown=1\nbind.Nothing=W\ngraphics=7\nantialiasing=X16\ntextures=8K\n");

    CHECK(read.MasterVolume == Settings().MasterVolume);
    CHECK(read.Mods.empty());
    CHECK(read.Graphics == Graphics::Original);
    CHECK(read.AntiAliasing == AntiAliasing::Off);
    CHECK(read.MusicVolume == Settings::MaxVolume);
    CHECK(read.Bindings.Count() == 0);
}

TEST_CASE("FlowTests.OldSmoothModelsKeyIgnored")
{
    // settings.cfg saved while "Smooth models" existed.
    auto read = Settings::Parse("smooth_models=On\ntextures=Hd\n");

    CHECK(read.StateOf(Mdk::Game::HdTextures::HdGenerator::ModFolder) == Mdk::Game::Mods::ModState::On);
    CHECK(read.Format().find("smooth_models") == std::string::npos);
}

TEST_CASE("FlowTests.GunterStreamCarriesHealthOnly")
{
    GameState state;
    state.Level = 8;

    state.EnterLastLevel(37);

    CHECK(state.Level == 5);
    REQUIRE(state.Carry.has_value());
    CHECK(state.Carry->Health == 37);
    CHECK(state.Carry->Pickups.empty());
}

TEST_CASE("FlowTests.SaveRoundTrip")
{
    SaveGame save{SaveKind::BeforeLevel, 4, 100, 2, true, "2026-10-06T10:00:00"};

    CHECK(SaveGame::Parse(save.ToJson()) == save);
}

TEST_CASE("FlowTests.ReadsGodotSaves")
{
    const std::string godot = "{\"type\": 3, \"level\": 8.0, \"health\": 100, \"deaths\": 1, \"strike_used\": false, \"time\": \"2026-01-01T12:00:00\"}";

    CHECK(SaveGame::Parse(godot) == SaveGame{SaveKind::LevelStart, 8, 100, 1, false, "2026-01-01T12:00:00"});
}

TEST_CASE("FlowTests.InvalidSavesAreRejected")
{
    const char* rows[] = {"{\"level\": 2}", "{\"type\": 3}", "not json"};
    for (auto json : rows)
    {
        CAPTURE(json);
        CHECK_FALSE(SaveGame::Parse(json).has_value());
    }
}

TEST_CASE("FlowTests.SavesAreListedAndDeleted")
{
    auto folder = TempFolder();
    SaveGames saves(folder);
    saves.Delete(SaveGames::LastGame);
    GameState state;
    state.Level = 3;

    // try/finally: the folder goes whatever the checks say (CHECKs don't throw).
    CHECK(saves.Write("B", state.Save(SaveKind::LevelStart)));
    CHECK(saves.Write("A", state.Save(SaveKind::BeforeLevel)));

    CHECK(saves.List() == std::vector<std::string>{"A", "B"});
    auto a = saves.Read("A");
    CHECK(a.has_value());
    CHECK((a ? a->Level : -1) == 3);
    saves.Delete("A");
    CHECK(saves.List() == std::vector<std::string>{"B"});

    std::filesystem::remove_all(folder);
}

TEST_CASE("FlowTests.MenuRows")
{
    struct Row
    {
        int Entries;
        float Height;
        bool Big;
    };
    const Row rows[] = {{1, 36.f, true}, {11, 350.f / 11.f, true}, {17, 350.f / 17.f, false}};
    for (auto& row : rows)
    {
        CAPTURE(row.Entries);
        CHECK_PRECISION(row.Height, MenuItems::RowFor(row.Entries), 3);
        CHECK(MenuItems::UsesBigFont(MenuItems::RowFor(row.Entries)) == row.Big);
    }
}

TEST_CASE("FlowTests.TouchRowsGrowAsFarAsThePageFits")
{
    struct Row
    {
        int Entries;
        int FirstRow;
        float Enlargement;
        float Height;
    };
    const Row rows[] = {
        {6, 0, 1.f, 36.f},
        {6, 0, 1.4f, 36.f * 1.4f},
        {8, 0, 1.4f, 350.f / 8.f},
        {6, 2, 1.4f, 350.f / 8.f},
        {17, 0, 1.4f, 350.f / 17.f},
    };
    for (auto& row : rows)
    {
        // A desktop's rows (1) are the original's; a full page's can't grow.
        CAPTURE(row.Entries);
        CAPTURE(row.FirstRow);
        CAPTURE(row.Enlargement);
        CHECK_PRECISION(row.Height, MenuItems::RowFor(row.Entries, row.FirstRow, row.Enlargement), 3);
    }
}

TEST_CASE("FlowTests.TouchRowsGrowToTenMillimetres")
{
    const std::pair<float, float> rows[] = {{6.5f, 1.4f}, {8.f, 1.25f}, {13.7f, 1.f}, {0.f, 1.f}};
    for (auto [rowMm, enlargement] : rows)
    {
        // A phone's 108-pixel rows (6.5 mm) grow by the most; a tablet's are big enough.
        CAPTURE(rowMm);
        CHECK_PRECISION(enlargement, MenuItems::Enlargement(rowMm), 3);
    }
}

TEST_CASE("FlowTests.LongTouchPagesScroll")
{
    struct Row
    {
        int Entries;
        int FirstRow;
        float Enlargement;
        float Height;
        int Shown;
    };
    const Row rows[] = {
        {22, 0, 1.4f, 350.f / 8.f, 8},
        {16, 0, 1.f, 36.f, 9},
        {9, 0, 1.4f, 0.f, 0},
        {10, 0, 1.4f, 0.f, 0},
    };
    for (auto& row : rows)
    {
        // The keys' page (22 items) on a phone: 8 grown rows show, the rest scroll; a page that
        // fits with the big font (the options' 10) doesn't scroll.
        CAPTURE(row.Entries);
        CHECK_PRECISION(row.Height, MenuItems::ScrollRow(row.Entries, row.FirstRow, row.Enlargement), 3);
        if (row.Shown > 0)
        {
            CHECK(MenuItems::ShownRows(row.Height, row.FirstRow) == row.Shown);
        }
    }
}

TEST_CASE("FlowTests.QuitTitleStandsAtRow3")
{
    CHECK(MenuItems::RowTop(MenuItems::RowFor(3), 3) == 113.f);
}

TEST_CASE("FlowTests.VolumeSteps")
{
    const std::tuple<int, int, int> rows[] = {{80, 1, 90}, {100, 1, 0}, {0, -1, 100}};
    for (auto [value, step, expected] : rows)
    {
        CAPTURE(value);
        CHECK(MenuItems::VolumeStep(value, step) == expected);
    }
}

TEST_CASE("FlowTests.SensitivitySteps")
{
    const std::tuple<float, int, float> rows[] = {{1.f, 1, 1.25f}, {3.f, 1, 0.25f}, {1.1f, -1, 0.75f}};
    for (auto [value, step, expected] : rows)
    {
        CAPTURE(value);
        CHECK(MenuItems::SensitivityStep(value, step) == expected);
    }
}

TEST_CASE("FlowTests.GammaSteps")
{
    const std::tuple<float, int, float> rows[] = {{1.f, 1, 1.1f}, {2.f, 1, 0.5f}, {0.5f, -1, 2.f}, {1.05f, 1, 1.1f}};
    for (auto [value, step, expected] : rows)
    {
        CAPTURE(value);
        CHECK(MenuItems::GammaStep(value, step) == expected);
    }
}

TEST_CASE("FlowTests.GammaRoundTripsAndStaysInRange")
{
    Settings settings;
    settings.Gamma = 1.3f;
    CHECK(Settings::Parse(settings.Format()).Gamma == 1.3f);
    CHECK(Settings::Parse("gamma=9").Gamma == Renderer::MaxGamma);
    CHECK(Settings::Parse("gamma=bright").Gamma == 1.f);
}

TEST_CASE("FlowTests.ScoreValues")
{
    CHECK(StatsScreen::ValueText(4, 8, 20) == "8/20");
    CHECK(StatsScreen::ValueText(1, 50, 100) == "50%");
    CHECK(StatsScreen::ValueText(0, 120, 120) == "120");
    CHECK(GameStats::Percent(1, 3) == 33);
    CHECK(GameStats::Percent(0, 0) == 0);
}

TEST_CASE("FlowTests.EnemiesAreCounted")
{
    GameStats stats;
    stats.CountEnemy("xg", GameStats::Kill::Created);
    stats.CountEnemy("XG", GameStats::Kill::Killed);
    stats.CountEnemy("X7DOOR", GameStats::Kill::Created);

    CHECK(stats.Enemies == 1);
    CHECK(stats.Kills == 1);
}

TEST_CASE("FlowTests.TypesetterLines")
{
    auto text = Ascii(R"(!!!News!!!\20n\cOne two\cThree\x\iFree\d)");
    auto [lines, count] = Typesetter::Layout(text, 32.f);

    REQUIRE(lines.size() == 5);
    CHECK(Fields(lines[0]) == std::tuple<std::string, float, float, bool, int>("!!!News!!!", 0.f, 32.f, false, 0));
    CHECK(Fields(lines[1]) == std::tuple<std::string, float, float, bool, int>("", 0.f, 88.f, false, 10));
    CHECK(Fields(lines[2]) == std::tuple<std::string, float, float, bool, int>("One two", 300.f, 88.f, true, 10));
    CHECK(Fields(lines[3]) == std::tuple<std::string, float, float, bool, int>("Three", 300.f, 88.f, true, 17));
    CHECK(Fields(lines[4]) == std::tuple<std::string, float, float, bool, int>("Free", 0.f, 88.f, false, 22));
    CHECK(count == 22);
}

TEST_CASE("FlowTests.SplashRuns")
{
    std::vector<byte> bytes = {3, 7, 254, 1, 2, 0};

    CHECK(IntroSplash::Unpack(bytes, 0, 6) == std::vector<byte>{7, 7, 7, 1, 2, 0});
}

TEST_CASE("FlowTests.SplashFadesPalettes")
{
    std::vector<byte> from = {0};
    std::vector<byte> to = {200};
    CHECK(IntroSplash::Blend(from, to, 0.5f) == std::vector<byte>{100});
    CHECK(IntroSplash::Blend(from, to, 1.f) == std::vector<byte>{200});
}

TEST_CASE("FlowTests.EndMovieFlash")
{
    const std::pair<int, float> rows[] = {{209, 0.f}, {233, 1.f}, {260, 0.f}};
    for (auto [frame, flash] : rows)
    {
        CAPTURE(frame);
        CHECK(EndMovie::Flash(frame) == flash);
    }
}
