#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Dictionary.h"
#include "Mdk.Engine/Platform/Display.h"
#include "Mdk.Engine/Platform/HeadTracker.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Engine/Platform/TouchControls.h"
#include "Mdk.Engine/Platform/TouchLayout.h"
#include "Mdk.Engine/Platform/Window.h"
#include "Mdk.Engine/Render/GpuBackend.h"
#include "Mdk.Engine/Render/Renderer.h"
#include "Mdk.Engine/Upscale/UpscalerSource.h"
#include "Mdk.Game/Kurt/Inventory.h"
#include "Mdk.Game/Mods/ModCatalog.h"

namespace Mdk::Engine::Audio
{
    class AudioDevice;
}

namespace Mdk::Game::Flow
{
    // The look of the levels: the original's, or enhanced (lit, filtered, shadows, haze).
    enum class Graphics { Original, Enhanced };

    // The player's settings (settings.gd): volumes, the music filter, the mouse, the window
    // and the frame (display mode, size, render scale, VSync, frame limit, GPU backend),
    // anti-aliasing, the difficulty, gore, the graphics, the mods switched off and the key bindings
    // (one key or mouse button per action, by name). Saved as name=value lines in the user's folder;
    // applied at start and whenever they change.
    //   master_volume=80
    //   bind.Forward=W
    //   mod.hd-textures=Off
    //   touch.Fire=Right,0.0113,Bottom,0.2,9.8,9.8
    class Settings
    {
    public:
        static constexpr int MaxVolume = 100;
        // The on-screen controls' opacity (%): never quite invisible.
        static constexpr int MinOpacity = 25;
        static constexpr int MaxOpacity = 100;

        // The window's size until another is chosen (the original's 640 x 480, doubled).
        static constexpr Engine::Platform::Resolution DefaultWindow{1280, 960};

        // The game's actions in the order the controls screen lists them, with their names.
        static const std::vector<std::pair<Engine::Platform::Key, std::string>>& Actions();

        // The actions the gamepad's buttons rebind: the sticks walk and look.
        static const std::vector<std::pair<Engine::Platform::Key, std::string>>& PadActions();

        // Volumes from 0 to 100.
        int MasterVolume = 80;
        int MusicVolume = 50;
        int EffectsVolume = 70;
        bool MusicFilter = true;
        // Mouse sensitivity multiplier (0.25-3) and inverted vertical look.
        float MouseSensitivity = 1.f;
        bool InvertMouse = false;
        // The gamepad's look: its sensitivity (as the mouse's) and inverted vertical look.
        float PadSensitivity = 1.f;
        bool InvertPad = false;
        // The window (Options, Display): windowed at WindowSize, fullscreen on the desktop, or
        // exclusive at ExclusiveSize (null: the desktop's mode).
        Engine::Platform::Fullscreen Fullscreen = Engine::Platform::Fullscreen::Off;
        Engine::Platform::Resolution WindowSize = DefaultWindow;
        std::optional<Engine::Platform::Resolution> ExclusiveSize;
        // The frame's size, % of the window's (Renderer.Scales); Android's default is Auto (a frame
        // of about 1080p's pixels: big tablets' GPUs can't fill theirs).
#if defined(__ANDROID__)
        int RenderScale = Engine::Render::Renderer::AutoScale;
#else
        int RenderScale = Engine::Render::Renderer::FullScale;
#endif
        Engine::Render::VSync VSync = Engine::Render::VSync::On;
        // Frames a second at most with VSync off or adaptive (FrameLimiter.Limits).
        int FrameLimit = Engine::Platform::FrameLimiter::Off;
        // SDL_GPU's driver, from the next start.
        Engine::Render::GpuBackend Backend = Engine::Render::GpuBackend::Auto;
        // The display's gamma (Renderer.Gamma): above 1 brighter.
        float Gamma = Engine::Render::Renderer::DefaultGamma;
        Kurt::Difficulty Difficulty = Kurt::Difficulty::Normal;
        // Gore (0x5742dc, on by default): green sparks, slime, blown off parts, the head shots row.
        bool Gore = true;
        // The levels' look, from the next level on; the 2D screens are filtered at once.
        Flow::Graphics Graphics = Flow::Graphics::Original;
        Engine::Render::AntiAliasing AntiAliasing = Engine::Render::AntiAliasing::Off;
        // How the frame holds the two eyes (Stereo), the eyes' distance apart (MDK units) and the
        // distance their images converge at (0: infinity).
        Engine::Render::Stereo Stereo = Engine::Render::Stereo::Off;
        float StereoSeparation = 0.25f;
        float StereoConvergence = 10.f;
        // The phone's gyroscope turns the view (a VR viewer).
        Engine::Platform::HeadTracking HeadTracking = Engine::Platform::HeadTracking::Off;
        // When the touch screen's on-screen controls show (Android).
        Engine::Platform::TouchButtons Touch = Engine::Platform::TouchButtons::Auto;
        // The on-screen controls the player moved or resized (the others keep their defaults),
        // and the controls' opacity (%).
        std::map<Engine::Platform::TouchPart, Engine::Platform::TouchPlace> TouchPlaces;
        int TouchOpacity = MaxOpacity;
        // Rebound actions: action → key or mouse button name (see Input.Bind).
        Dictionary<Engine::Platform::Key, std::string> Bindings;
        // Rebound actions: action → gamepad button name (see Input.BindPad).
        Dictionary<Engine::Platform::Key, std::string> PadBindings;
        // Mods by folder; those not listed are on (ModCatalog).
        std::map<std::string, Game::Mods::ModState, Game::Mods::FolderLess> Mods;
        // The HD textures' upscaler archive and its SHA-256; empty: the platform's default.
        std::string UpscalerUrl;
        std::string UpscalerSha256;

        Engine::Upscale::UpscalerSource Upscaler() const
        {
            return Engine::Upscale::UpscalerSource::Of(UpscalerUrl, UpscalerSha256);
        }

        Game::Mods::ModState StateOf(const std::string& mod) const;

        // The settings file in a user folder.
        static std::string PathIn(const std::string& folder);

        static Settings Load(const std::string& path);

        void Save(const std::string& path) const;

        // Settings from their lines; unknown or bad lines keep the defaults.
        static Settings Parse(const std::string& text);

        std::string Format() const;

        // The volumes and filter, the window, the frame's sampling, the mouse and the bindings.
        void Apply(Engine::Audio::AudioDevice& audio, Engine::Platform::Window& window,
            Engine::Render::Renderer& renderer, Engine::Platform::Input& input);

        // The defaults, then the rebound actions (files without an action keep its default).
        void Bind(Engine::Platform::Input& input);

    private:
        static constexpr const char* FileName = "settings.cfg";
        static constexpr const char* BindPrefix = "bind.";
        static constexpr const char* PadPrefix = "pad.";
        static constexpr const char* ModPrefix = "mod.";
        static constexpr const char* TouchPrefix = "touch.";

        void Set(const std::string& name, const std::string& value);

        static int Volume(const std::string& value, int fallback);

        // 0-100 to a linear gain (the volume as a fraction, silent at 0).
        static float Gain(int volume) { return static_cast<float>(volume) / MaxVolume; }
    };
}
