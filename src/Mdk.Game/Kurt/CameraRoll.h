#pragma once

#include "Mdk.Core/Numerics.h"
#include "Mdk.Game/Kurt/Kurt.h"

namespace Mdk::Game::Kurt
{
    // The camera's roll (0x573910, degrees; positive banks the view right), set by Kurt's
    // moves each step (camera_roll.gd; godot-mdk docs/gameplay.md "Horizontal").
    //   running + turning (keys) ──► ±0.25°/tick up to ±10° (damp_move 0x467fa4)
    //   sliding                  ──► 0.9·roll + 0.1·(90° − slope angle) per tick (0x468db8)
    //   snowboard                ──► towards the board's bank at 45°/s (0x46ac4c)
    //   none of these            ──► back to 0 by clamp(0.35·|roll|, 0.05, 2.5)°/tick (damp_control)
    class CameraRoll
    {
    public:
        float Roll = 0;

        // Running and turning with the keys (positive: right, forward). Without both it doesn't change.
        void Walk(float right, float forward, float delta);

        // Sliding: eases towards 90° − atan2(n.z, n.x·f.y − n.y·f.x) for the floor normal
        // and the facing, a tenth per tick.
        void Slide(Vector3 normal, Vector2 facing, float delta);

        // On the board: towards its bank at 45°/s.
        void Follow(float bank, float delta);

        // At the start of a step: levels out unless the last step set it.
        void Settle(float delta);

    private:
        static constexpr float WalkRate = 0.25f * Kurt::Ticks;
        static constexpr float WalkLimit = 10.f;
        // Turning the other way first jumps 2° back towards level.
        static constexpr float WalkReverse = 2.f;
        static constexpr float DecayFactor = 0.35f;
        static constexpr float DecayMin = 0.05f;
        static constexpr float DecayMax = 2.5f;
        static constexpr float SlideKeep = 0.9f;
        static constexpr float SlideLevel = 90.f;
        static constexpr float BoardRate = 45.f;

        // Something set the roll this step, so it doesn't level out (0x57ff74).
        bool _held = false;
    };
}
