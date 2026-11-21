#pragma once

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

    // Where --teleport=ARENA,x,y,z sends Kurt (teleport_player).
    struct TeleportTarget
    {
        std::string Arena;
        Vector3 Position;

        bool operator==(const TeleportTarget&) const = default;

        // From ARENA,x,y,z.
        static TeleportTarget Parse(const std::string& text);
    };

    // How the tests' --bomber ends: on the XE, or also dropping a bomb.
    enum class BomberTest { Ride, Drop };

    // The tests' rides (like the Godot port's --teleport, --ride, --kill and --bomber), after
    // the delay: a teleport, getting on a walker, killing an object, and the XE: the comm device of
    // DANT_5 is hit, the XE it calls comes, Kurt drops onto it and (drop) fires a bomb once unlocked.
    //   delay ──► teleport ──► kill ──► ride
    //         └─1 s──► hit group 16 ──► XE rideable ──► Kurt above it ──► unlocked ──► fire (2 steps)
    class RideTest
    {
    public:
        explicit RideTest(const ViewerOptions& options) : _options(options) {}

        // Before each game step at game time `time`.
        void Step(Kurt::Kurt& kurt, Scripts::ScriptRuntime& scripts, Engine::Platform::Input& input, float time);

        // The profile's lines about the ride.
        static void Report(Scripts::ScriptRuntime& scripts);

    private:
        static constexpr const char* BomberArena = "DANT_5";
        static constexpr int BomberCallGroup = 16;
        static constexpr float BomberCallDelay = 1.f;
        static constexpr float AboveXe = 1.f;
        static constexpr int FireSteps = 2;
        static constexpr float TeleportYaw = 90.f;

        const ViewerOptions& _options;
        bool _started = false;
        bool _called = false;
        bool _boarded = false;
        int _fireSteps = -1;

        void Start(Scripts::ScriptRuntime& scripts);

        void StepBomber(Kurt::Kurt& kurt, Scripts::ScriptRuntime& scripts, Engine::Platform::Input& input, float time,
            BomberTest test);
    };
}
