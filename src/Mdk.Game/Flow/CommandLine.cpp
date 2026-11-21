// The command line's options as the game's: the desktop's (Mdk.App) and the Android bench's.

#include "Mdk.Game/Flow/CommandLine.h"

#include <format>

#include "Mdk.Core/Console.h"
#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/EnumNames.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/Display.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Game/FrameDump.h"
#include "Mdk.Game/Flow/EnumNames.h"
#include "Mdk.Game/Flow/TestPresses.h"
#include "Mdk.Game/Menu/MainMenu.h"
#include "Mdk.Game/Menu/StatsScreen.h"
#include "Mdk.Game/RideTest.h"
#include "Mdk.Game/Scripts/StrikeScene.h"
#include "Mdk.Game/SniperTest.h"
#include "Mdk.Game/SoakTest.h"
#include "Mdk.Game/Viewer.h"

namespace Mdk::Game::Flow
{
    using Engine::Platform::Resolution;
    using Engine::Render::Stereo;

    namespace
    {
        constexpr int DefaultLevel = 7;
        // --perf's game seconds before frames are measured.
        constexpr float DefaultWarmup = 2.f;

        std::vector<int> ParseInts(const std::string& text)
        {
            std::vector<int> values;
            for (const auto& part : Str::Split(text, ','))
            {
                values.push_back(Str::ParseInt(part));
            }

            return values;
        }
    }

    Dictionary<std::string, std::string> CommandLine::Parse(const std::vector<std::string>& args)
    {
        Dictionary<std::string, std::string> options;
        for (const auto& a : args)
        {
            if (!Str::StartsWith(a, "--"))
            {
                continue;
            }

            auto p = Str::SplitCount(std::string_view(a).substr(2), '=', 2);
            options.Add(p[0], p.size() > 1 ? p[1] : "");
        }

        return options;
    }

    std::optional<GameOptions> CommandLine::Options(const Dictionary<std::string, std::string>& options)
    {
        auto has = [&options](const std::string& name) { return options.ContainsKey(name); };
        auto get = [&options](const std::string& name) -> std::optional<std::string>
        {
            const auto* value = options.Find(name);
            return value != nullptr ? std::optional<std::string>(*value) : std::nullopt;
        };

        std::optional<Vector3> position;
        std::optional<float> yaw;
        if (auto at = get("at"))
        {
            std::vector<float> v;
            for (const auto& s : Str::Split(*at, ','))
            {
                v.push_back(Str::ParseFloat(s));
            }

            position = Vector3(v.at(0), v.at(1), v.at(2));
            yaw = v.size() > 3 ? std::optional<float>(v[3]) : std::nullopt;
        }

        auto Number = [&get](const std::string& name) -> std::optional<int>
        {
            auto text = get(name);
            return text.has_value() ? std::optional<int>(Str::ParseInt(*text)) : std::nullopt;
        };
        // The first given of these (C#'s ?? chain).
        auto level = DefaultLevel;
        for (const auto* name : {"level", "stats", "briefing", "stream", "fall"})
        {
            if (auto number = Number(name))
            {
                level = *number;
                break;
            }
        }

        auto pitchText = get("pitch");
        auto pitch = pitchText.has_value() ? Str::ParseFloat(*pitchText) : 0.f;
        std::optional<Stereo> stereo;
        if (auto stereoText = get("stereo"))
        {
            Stereo parsedStereo{};
            if (!Engine::Render::StereoModes::TryParse(*stereoText, parsedStereo))
            {
                Console::Error().WriteLine("--stereo: off, sbs, sbsfull, crossview, int, intr, tab, tabr or vr");
                return std::nullopt;
            }

            stereo = parsedStereo;
        }

        auto Real = [&get](const std::string& name) -> std::optional<float>
        {
            auto text = get(name);
            return text.has_value() ? std::optional<float>(Str::ParseFloat(*text)) : std::nullopt;
        };
        auto stereoSeparation = Real("stereo-separation");
        auto stereoConvergence = Real("stereo-convergence");
        auto screenshot = get("screenshot");
        auto sound = has("mute") ? SoundMode::Muted : SoundMode::On;
        auto Seconds = [&Real](const std::string& name) { return Real(name).value_or(0.f); };
        ViewerOptions viewer;
        viewer.Level = level;
        viewer.Position = position;
        viewer.Yaw = yaw;
        viewer.Pitch = pitch;
        viewer.Sound = sound;
        viewer.Screenshot = screenshot;
        viewer.Wait = Seconds("wait");
        viewer.Walk = Seconds("walk");
        viewer.Look = Seconds("look");
        viewer.Delay = Seconds("delay");
        viewer.Jump = has("jump");
        viewer.Fire = has("fire");
        if (auto give = get("give"))
        {
            viewer.Give = Str::Split(*give, ',');
        }

        viewer.Use = has("use");
        viewer.Fly = has("fly");
        viewer.Profile = has("profile");
        viewer.Trace = has("trace");
        if (auto sniper = get("sniper"))
        {
            viewer.Sniper = SniperTest::Setup(*sniper);
        }

        viewer.Zoom = Seconds("zoom");
        viewer.SniperFire = has("sniper-fire");
        if (auto strike = get("strike"))
        {
            viewer.Strike = *strike == "dive" ? Scripts::StrikeScene::Plane::Only : Scripts::StrikeScene::Plane::WithPilot;
        }

        viewer.Die = has("die");
        viewer.Event = Number("event");
        if (auto teleport = get("teleport"))
        {
            viewer.Teleport = TeleportTarget::Parse(*teleport);
        }

        viewer.Kill = get("kill");
        viewer.Ride = get("ride");
        if (auto bomber = get("bomber"))
        {
            viewer.Bomber = *bomber == "drop" ? BomberTest::Drop : BomberTest::Ride;
        }

        viewer.Snapshot = get("snapshot");
        if (auto soak = get("soak"))
        {
            viewer.Soak = !soak->empty() ? Str::ParseInt(*soak) : 1;
        }

        viewer.Route = has("tour") ? SoakRoute::Tour : SoakRoute::Stay;
        viewer.BetaTeleport = Number("beta-teleport");
        if (auto roll = get("roll"))
        {
            viewer.Roll = *roll == "left" ? BetaRoll::Left : BetaRoll::Right;
        }

        viewer.Console = get("console");
        if (auto frames = get("frames"))
        {
            viewer.Frames = FrameDump::Parse(*frames);
        }

        viewer.FrameRate = Number("fps");

        // Test options of a level (and --screenshot without --menu) skip the menu, as in the Godot port.
        auto start = has("end") ? Start::EndMovie
            : has("stats") ? Start::Statistics
            : has("briefing") ? Start::Briefing
            : has("stream") ? Start::Stream
            : has("fall") ? Start::Fall
            : has("level") || (screenshot.has_value() && !has("menu")) ? Start::Level
            : Start::Menu;
        auto page = has("touch-layout") ? Menu::MenuPage::TouchLayout
            : has("controls") ? Menu::MenuPage::Controls
            : has("gamepad") ? Menu::MenuPage::Gamepad
            : has("options") ? Menu::MenuPage::Options
            : has("beta-levels") ? Menu::MenuPage::BetaLevels
            : Menu::MenuPage::Main;
        // Tests run without a window: --hidden, or MDK_HIDDEN set (so nothing takes the focus).
        auto hiddenVariable = GetEnv("MDK_HIDDEN");
        auto hidden = has("hidden") || (hiddenVariable.has_value() && !hiddenVariable->empty());
        GameOptions gameOptions;
        gameOptions.Start = start;
        gameOptions.Level = viewer;
        gameOptions.Display = hidden ? Display::Hidden : Display::Window;
        gameOptions.Page = page;
        gameOptions.Splash = !has("menu") || has("splash");
        if (auto phase = Number("phase"))
        {
            gameOptions.Phase = static_cast<Menu::StatsScreen::Phase>(*phase);
        }

        if (auto counts = get("counts"))
        {
            gameOptions.Counts = ParseInts(*counts);
        }

        gameOptions.Towns = Number("towns");
        gameOptions.Load = get("load");
        gameOptions.Save = get("save");
        gameOptions.Screenshot = screenshot;
        gameOptions.Wait = Seconds("wait");
        gameOptions.Health = Number("health");
        if (has("enhanced") || has("original"))
        {
            gameOptions.Graphics = has("enhanced") ? Graphics::Enhanced : Graphics::Original;
        }

        if (has("bloodyes") || has("nobloodno"))
        {
            gameOptions.Gore = has("bloodyes");
        }

        gameOptions.Stereo = stereo;
        gameOptions.StereoSeparation = stereoSeparation;
        gameOptions.StereoConvergence = stereoConvergence;
        gameOptions.Gamma = Real("gamma");
        if (auto qualityText = get("quality"))
        {
            Engine::Render::EffectQuality quality{};
            if (!EnumNames::TryParse(*qualityText, Flow::Names::EffectQuality, quality, EnumNames::Case::Ignore))
            {
                throw ArgumentException(std::format("Requested value '{}' was not found.", *qualityText));
            }

            gameOptions.Quality = quality;
        }

        if (auto presses = get("press"))
        {
            gameOptions.Presses = std::make_shared<TestPresses>(TestPresses::Parse(*presses));
        }

        if (auto mods = get("mod"))
        {
            gameOptions.Mods = Str::Split(*mods, ',', Str::SplitOptions::RemoveEmpty);
        }

        gameOptions.Gpu = get("gpu");
        gameOptions.RecordAudio = get("record-audio");
        Resolution hiddenSize{};
        auto sizeText = get("size");
        if (sizeText.has_value() && Resolution::TryParse(*sizeText, hiddenSize))
        {
            gameOptions.HiddenSize = hiddenSize;
        }

        gameOptions.HiddenFrames = has("swapchain") ? Engine::Render::HiddenFrames::Swapchain : Engine::Render::HiddenFrames::Offscreen;
        if (auto perf = get("perf"))
        {
            gameOptions.Perf = !perf->empty() ? Str::ParseFloat(*perf) : DefaultWarmup;
        }

        return gameOptions;
    }
}
