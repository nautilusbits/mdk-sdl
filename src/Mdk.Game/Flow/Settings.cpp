#include "Mdk.Game/Flow/Settings.h"

#include <algorithm>
#include <format>

#include "Mdk.Core/Console.h"
#include "Mdk.Core/EnumNames.h"
#include "Mdk.Core/Files.h"
#include "Mdk.Engine/Audio/AudioDevice.h"
#include "Mdk.Game/Flow/EnumNames.h"
#include "Mdk.Game/HdTextures/HdGenerator.h"

namespace Mdk::Game::Flow
{
    using Engine::Platform::Key;

    namespace
    {
        // bool.ToString(): True or False.
        std::string BoolText(bool value)
        {
            return value ? "True" : "False";
        }

        // bool.TryParse: True or False in any case, spaces around ignored.
        bool TryParseBool(const std::string& text, bool& value)
        {
            auto trimmed = Str::Trim(text);
            if (Str::EqualsIgnoreCase(trimmed, "True"))
            {
                value = true;
                return true;
            }

            if (Str::EqualsIgnoreCase(trimmed, "False"))
            {
                value = false;
                return true;
            }

            return false;
        }

        // Enum.TryParse(value) && Enum.IsDefined: a member's name or number.
        template <typename E, size_t N>
        bool TryParseDefined(const std::string& text, const std::array<std::string_view, N>& names, E& value)
        {
            E parsed{};
            if (!EnumNames::TryParse(text, names, parsed) || !EnumNames::IsDefined(parsed, names))
            {
                return false;
            }

            value = parsed;
            return true;
        }

        template <typename T, size_t N>
        bool Contains(const std::array<T, N>& values, T value)
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }
    }

    const std::vector<std::pair<Key, std::string>>& Settings::Actions()
    {
        static const std::vector<std::pair<Key, std::string>> actions{
            {Key::Forward, "Forward"}, {Key::Back, "Back"}, {Key::TurnLeft, "Turn left"}, {Key::TurnRight, "Turn right"},
            {Key::StrafeLeft, "Strafe left"}, {Key::StrafeRight, "Strafe right"}, {Key::Jump, "Jump"}, {Key::Turbo, "Run"},
            {Key::AlwaysRun, "Always run"}, {Key::Fire, "Fire"}, {Key::Sniper, "Sniper mode"}, {Key::ZoomIn, "Zoom in"}, {Key::ZoomOut, "Zoom out"},
            {Key::UseItem, "Use item"}, {Key::ItemNext, "Next item"}, {Key::ItemPrevious, "Previous item"},
            {Key::QuickSave, "Quick save"}, {Key::QuickLoad, "Quick load"},
        };
        return actions;
    }

    const std::vector<std::pair<Key, std::string>>& Settings::PadActions()
    {
        static const std::vector<std::pair<Key, std::string>> actions = []
        {
            std::vector<std::pair<Key, std::string>> pad;
            for (const auto& action : Actions())
            {
                auto key = action.first;
                if (key == Key::Forward || key == Key::Back || key == Key::TurnLeft || key == Key::TurnRight
                    || key == Key::StrafeLeft || key == Key::StrafeRight)
                {
                    continue;
                }

                pad.push_back(action);
            }

            return pad;
        }();
        return actions;
    }

    Game::Mods::ModState Settings::StateOf(const std::string& mod) const
    {
        auto found = Mods.find(mod);
        return found == Mods.end() ? Game::Mods::ModState::On : found->second;
    }

    std::string Settings::PathIn(const std::string& folder)
    {
        return Path::Combine(folder, FileName);
    }

    Settings Settings::Load(const std::string& path)
    {
        return File::Exists(path) ? Parse(File::ReadAllText(path)) : Settings();
    }

    void Settings::Save(const std::string& path) const
    {
        try
        {
            Directory::CreateDirectory(Path::GetDirectoryName(path));
            File::WriteAllText(path, Format());
        }
        catch (const IOException& e)
        {
            Console::Error().WriteLine(std::format("Settings not saved: {}", e.what()));
        }
    }

    Settings Settings::Parse(const std::string& text)
    {
        Settings settings;
        for (const auto& line : Str::Split(text, '\n'))
        {
            auto parts = Str::SplitCount(Str::Trim(line), '=', 2);
            if (parts.size() != 2)
            {
                continue;
            }

            settings.Set(Str::Trim(parts[0]), Str::Trim(parts[1]));
        }

        return settings;
    }

    std::string Settings::Format() const
    {
        std::vector<std::string> lines{
            std::format("master_volume={}", MasterVolume),
            std::format("music_volume={}", MusicVolume),
            std::format("effects_volume={}", EffectsVolume),
            std::format("music_filter={}", BoolText(MusicFilter)),
            std::format("mouse_sensitivity={}", Str::FromFloat(MouseSensitivity)),
            std::format("invert_mouse={}", BoolText(InvertMouse)),
            std::format("pad_sensitivity={}", Str::FromFloat(PadSensitivity)),
            std::format("invert_pad={}", BoolText(InvertPad)),
            std::format("touch_controls={}", EnumNames::Name(Touch, Names::TouchButtons)),
            std::format("touch_opacity={}", TouchOpacity),
            std::format("fullscreen={}", EnumNames::Name(Fullscreen, Names::Fullscreen)),
            std::format("window_size={}", WindowSize.ToString()),
            std::format("exclusive_size={}", ExclusiveSize.has_value() ? ExclusiveSize->ToString() : ""),
            std::format("render_scale={}", RenderScale),
            std::format("vsync={}", EnumNames::Name(VSync, Names::VSync)),
            std::format("frame_limit={}", FrameLimit),
            std::format("gpu_backend={}", EnumNames::Name(Backend, Names::GpuBackend)),
            std::format("gamma={}", Str::FromFloat(Gamma)),
            std::format("difficulty={}", EnumNames::Name(Difficulty, Names::Difficulty)),
            std::format("gore={}", BoolText(Gore)),
            std::format("graphics={}", EnumNames::Name(Graphics, Names::Graphics)),
            std::format("antialiasing={}", EnumNames::Name(AntiAliasing, Names::AntiAliasing)),
            std::format("stereo={}", EnumNames::Name(Stereo, Names::Stereo)),
            std::format("stereo_separation={}", Str::FromFloat(StereoSeparation)),
            std::format("stereo_convergence={}", Str::FromFloat(StereoConvergence)),
            std::format("head_tracking={}", EnumNames::Name(HeadTracking, Names::HeadTracking)),
            std::format("upscaler_url={}", UpscalerUrl),
            std::format("upscaler_sha256={}", UpscalerSha256),
        };
        for (const auto& [key, control] : Bindings)
        {
            lines.push_back(std::format("{}{}={}", BindPrefix, EnumNames::Name(key, Names::Key), control));
        }

        for (const auto& [key, control] : PadBindings)
        {
            lines.push_back(std::format("{}{}={}", PadPrefix, EnumNames::Name(key, Names::Key), control));
        }

        // std::map: already in the keys' order (the mods' case-insensitively).
        for (const auto& [part, place] : TouchPlaces)
        {
            lines.push_back(std::format("{}{}={}", TouchPrefix, EnumNames::Name(part, Names::TouchPart), place.ToString()));
        }

        for (const auto& [mod, state] : Mods)
        {
            lines.push_back(std::format("{}{}={}", ModPrefix, mod, EnumNames::Name(state, Names::ModState)));
        }

        return Str::Join("\n", lines) + "\n";
    }

    void Settings::Set(const std::string& name, const std::string& value)
    {
        if (Str::StartsWith(name, BindPrefix))
        {
            Key key{};
            if (EnumNames::TryParse(std::string_view(name).substr(std::char_traits<char>::length(BindPrefix)), Names::Key, key))
            {
                Bindings[key] = value;
            }

            return;
        }

        if (Str::StartsWith(name, PadPrefix))
        {
            Key key{};
            if (EnumNames::TryParse(std::string_view(name).substr(std::char_traits<char>::length(PadPrefix)), Names::Key, key))
            {
                PadBindings[key] = value;
            }

            return;
        }

        if (Str::StartsWith(name, TouchPrefix))
        {
            Engine::Platform::TouchPart part{};
            Engine::Platform::TouchPlace place{};
            if (TryParseDefined(name.substr(std::char_traits<char>::length(TouchPrefix)), Names::TouchPart, part)
                && Engine::Platform::TouchPlace::TryParse(value, place))
            {
                TouchPlaces[part] = place;
            }

            return;
        }

        if (Str::StartsWith(name, ModPrefix))
        {
            Game::Mods::ModState state{};
            if (TryParseDefined(value, Names::ModState, state))
            {
                Mods[name.substr(std::char_traits<char>::length(ModPrefix))] = state;
            }

            return;
        }

        bool flag = false;
        int number = 0;
        float real = 0;
        if (name == "master_volume")
        {
            MasterVolume = Volume(value, MasterVolume);
        }
        else if (name == "music_volume")
        {
            MusicVolume = Volume(value, MusicVolume);
        }
        else if (name == "effects_volume")
        {
            EffectsVolume = Volume(value, EffectsVolume);
        }
        else if (name == "music_filter")
        {
            MusicFilter = TryParseBool(value, flag) ? flag : MusicFilter;
        }
        else if (name == "mouse_sensitivity")
        {
            MouseSensitivity = Str::TryParseFloat(value, real) ? real : MouseSensitivity;
        }
        else if (name == "invert_mouse")
        {
            InvertMouse = TryParseBool(value, flag) ? flag : InvertMouse;
        }
        else if (name == "pad_sensitivity")
        {
            PadSensitivity = Str::TryParseFloat(value, real) ? real : PadSensitivity;
        }
        else if (name == "invert_pad")
        {
            InvertPad = TryParseBool(value, flag) ? flag : InvertPad;
        }
        else if (name == "touch_controls")
        {
            TryParseDefined(value, Names::TouchButtons, Touch);
        }
        else if (name == "touch_opacity")
        {
            TouchOpacity = Str::TryParseInt(value, number) ? std::clamp(number, MinOpacity, MaxOpacity) : TouchOpacity;
        }
        // Older settings: True or False.
        else if (name == "fullscreen" && TryParseBool(value, flag))
        {
            Fullscreen = flag ? Engine::Platform::Fullscreen::Desktop : Engine::Platform::Fullscreen::Off;
        }
        else if (name == "fullscreen")
        {
            TryParseDefined(value, Names::Fullscreen, Fullscreen);
        }
        else if (name == "window_size")
        {
            Engine::Platform::Resolution size{};
            WindowSize = Engine::Platform::Resolution::TryParse(value, size) ? size : WindowSize;
        }
        else if (name == "exclusive_size")
        {
            Engine::Platform::Resolution exclusive{};
            ExclusiveSize = Engine::Platform::Resolution::TryParse(value, exclusive) ? exclusive : ExclusiveSize;
        }
        else if (name == "render_scale")
        {
            RenderScale = Str::TryParseInt(value, number) && Contains(Engine::Render::Renderer::Scales, number) ? number : RenderScale;
        }
        else if (name == "vsync")
        {
            TryParseDefined(value, Names::VSync, VSync);
        }
        else if (name == "frame_limit")
        {
            FrameLimit = Str::TryParseInt(value, number) && Contains(Engine::Platform::FrameLimiter::Limits, number) ? number : FrameLimit;
        }
        else if (name == "gpu_backend")
        {
            TryParseDefined(value, Names::GpuBackend, Backend);
        }
        else if (name == "gamma")
        {
            Gamma = Str::TryParseFloat(value, real)
                ? std::clamp(real, Engine::Render::Renderer::MinGamma, Engine::Render::Renderer::MaxGamma) : Gamma;
        }
        else if (name == "difficulty")
        {
            Kurt::Difficulty difficulty{};
            Difficulty = EnumNames::TryParse(value, Names::Difficulty, difficulty) ? difficulty : Difficulty;
        }
        else if (name == "gore")
        {
            Gore = TryParseBool(value, flag) ? flag : Gore;
        }
        else if (name == "graphics")
        {
            TryParseDefined(value, Names::Graphics, Graphics);
        }
        else if (name == "antialiasing")
        {
            TryParseDefined(value, Names::AntiAliasing, AntiAliasing);
        }
        else if (name == "stereo")
        {
            TryParseDefined(value, Names::Stereo, Stereo);
        }
        else if (name == "stereo_separation")
        {
            StereoSeparation = Str::TryParseFloat(value, real)
                ? std::clamp(real, 0.f, Engine::Render::StereoModes::MaxSeparation) : StereoSeparation;
        }
        else if (name == "stereo_convergence")
        {
            StereoConvergence = Str::TryParseFloat(value, real)
                ? std::clamp(real, 0.f, Engine::Render::StereoModes::MaxConvergence) : StereoConvergence;
        }
        else if (name == "head_tracking")
        {
            TryParseDefined(value, Names::HeadTracking, HeadTracking);
        }
        else if (name == "upscaler_url")
        {
            UpscalerUrl = value;
        }
        else if (name == "upscaler_sha256")
        {
            UpscalerSha256 = value;
        }
        // Older settings' HD switch: now the HD textures mod's.
        else if (name == "textures" && (value == "Hd" || value == "Original"))
        {
            Mods.emplace(HdTextures::HdGenerator::ModFolder, value == "Hd" ? Game::Mods::ModState::On : Game::Mods::ModState::Off);
        }
    }

    int Settings::Volume(const std::string& value, int fallback)
    {
        int volume = 0;
        return Str::TryParseInt(value, volume) ? std::clamp(volume, 0, MaxVolume) : fallback;
    }

    void Settings::Apply(Engine::Audio::AudioDevice& audio, Engine::Platform::Window& window,
        Engine::Render::Renderer& renderer, Engine::Platform::Input& input)
    {
        renderer.AntiAliasing = AntiAliasing;
        renderer.StereoMode = Stereo;
        renderer.StereoSeparation = StereoSeparation;
        renderer.StereoConvergence = StereoConvergence;
        window.TrackHead(HeadTracking);
        window.ShowTouch(Touch);
        window.ArrangeTouch(TouchPlaces, static_cast<float>(TouchOpacity) / MaxOpacity);
        renderer.CanvasSampling = Graphics == Flow::Graphics::Enhanced ? Engine::Render::Sampling::Linear : Engine::Render::Sampling::Nearest;
        audio.MasterGain = Gain(MasterVolume);
        audio.SetBusGain(Engine::Audio::Bus::Music, Gain(MusicVolume));
        audio.SetBusGain(Engine::Audio::Bus::Effects, Gain(EffectsVolume));
        audio.MusicFilter = MusicFilter ? Engine::Audio::Filter::On : Engine::Audio::Filter::Off;
        window.Apply(Engine::Platform::DisplaySetup{Fullscreen, WindowSize, ExclusiveSize});
        renderer.RenderScale = RenderScale;
        renderer.SetVSync(VSync);
        renderer.Gamma = Gamma;
        input.MouseScale = MouseSensitivity;
        input.InvertMouse = InvertMouse;
        input.PadSensitivity = PadSensitivity;
        input.InvertPad = InvertPad;
        Bind(input);
    }

    void Settings::Bind(Engine::Platform::Input& input)
    {
        input.ResetBindings();
        for (const auto& [key, control] : Bindings)
        {
            input.Bind(key, control);
        }

        input.ResetPadBindings();
        for (const auto& [key, control] : PadBindings)
        {
            input.BindPad(key, control);
        }
    }
}
