#pragma once

#include <optional>
#include <string>

#include "core/Numerics.h"

namespace Mdk::Engine::Platform
{
    class Input;
}

namespace Mdk::Game::Kurt
{
    class Kurt;
}

namespace Mdk::Game::Scripts
{
    class ScriptRuntime;
}

namespace Mdk::Game
{
    struct ViewerOptions;

    // The tests' sniper keys (like the Godot port's --sniper and --strike): sniper mode once
    // Kurt stands, at a zoom and pitch; the zoom key held for a while; one shot when the clip is ready;
    // Bones' full-screen strike.
    //   lands ──► EnterSniper (zoom, pitch) ──► zoom in for --zoom seconds
    //                                      └──1 s──► fire for 0.1 s (--sniper-fire)
    class SniperTest
    {
    public:
        explicit SniperTest(const ViewerOptions& options) : _options(options) {}

        // Before each game step at game time `time`.
        void Step(Kurt::Kurt& kurt, Scripts::ScriptRuntime& scripts, Engine::Platform::Input& input, float time);

        // Sniper mode's zoom and pitch from --sniper=zoom,pitch (both optional).
        static Vector2 Setup(const std::string& text);

    private:
        // Kurt has stood for this long before sniping (he lands first).
        static constexpr float SettleTime = 0.5f;
        // The clip is loaded 0.75 s after entering: the shot comes after 1 s, for 0.1 s.
        static constexpr float ShotStart = 1.f;
        static constexpr float ShotTime = 0.1f;
        // The strike starts after 1 s, once Kurt's arena is known.
        static constexpr float StrikeStart = 1.f;

        const ViewerOptions& _options;
        std::optional<float> _entered;
        bool _struck = false;
    };
}
