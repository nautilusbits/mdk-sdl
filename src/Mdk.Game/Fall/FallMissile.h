#pragma once

#include <vector>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Numerics.h"

namespace Mdk::Game::Fall
{
    // A missile of the fall (fall_missile.gd; launch 0x412568, each frame 0x411ee8). It flies
    // free for 60 ticks, then homes on Kurt; once it's more than 5 units above him it has passed
    // (M_PASS): it stops homing, flies on and goes 60 ticks later.
    //   Free (60 ticks) ──► Homing ──(5 units above Kurt)──► Passed (60 ticks) ──► gone
    class FallMissile
    {
    public:
        // What happened this frame.
        enum class Event { None, Passed, Gone };

        static constexpr int TrailPoints = 32;

        Vector3 Position{};
        Vector3 Velocity{};

        const std::vector<Vector3>& Trail() const { return _trail; }
        // The missile's velocity at each Trail point.
        const std::vector<Vector3>& TrailHeadings() const { return _headings; }

        // Launched from the origin in a random direction, climbing, with a random aim offset
        // of ±spread.
        FallMissile(float spread, Mdk::Random& random);

        // Moves the missile for a frame, Kurt being at kurt.
        Event Update(float dt, int ticks, Vector3 kurt);

    private:
        enum class Phase { Free, Homing, Passed };

        static constexpr float Speed = 250.f;
        static constexpr float TicksPerSecond = 30.f;
        static constexpr int FreeTicks = 60;
        static constexpr float PassHeight = 5.f;
        static constexpr int GoneTicks = 60;
        // Homing: v = 0.8 v + 0.2 × 250 × unit(aim - position).
        static constexpr float Homing = 0.2f;
        // Below 0.75 × Kurt's height it climbs 3 times as fast.
        static constexpr float BoostHeight = 0.75f;
        static constexpr float Boost = 3.f;
        // The aim is below Kurt by min(depth / 225, 10) × 66.67.
        static constexpr float AimDepth = 225.f;
        static constexpr float AimMax = 10.f;
        static constexpr float AimSpeed = 2000.f / 30.f;

        // The aim's offset from Kurt (x, y).
        Vector2 _offset{};
        // The last points, for the smoke trail.
        std::vector<Vector3> _trail;
        // The velocity at each trail point: the ribbon there keeps the missile's turn (0x4392a4).
        std::vector<Vector3> _headings;
        Phase _phase = Phase::Free;
        // Ticks left free, or since it passed.
        float _ticks = FreeTicks;

        static float Spread(Mdk::Random& random, float spread);

        // Steers towards a point below Kurt (deeper the farther below him it is), until it's 5
        // units above him.
        Event Home(Vector3 kurt, float dt);
    };
}
