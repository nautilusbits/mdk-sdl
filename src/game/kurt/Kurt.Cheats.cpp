#include "game/kurt/Kurt.h"

// The console's cheats: god (no damage, no death by falling out) and noclip (Kurt flies through
// everything: the walking keys move him, jump and Q rise and sink, turbo is faster).

namespace Mdk::Game::Kurt
{
    using Engine::Platform::Input;
    using Engine::Platform::Key;

    bool Kurt::UpdateNoclip(Input& input, float delta)
    {
        if (Clipping == Game::Kurt::Clipping::On)
        {
            return false;
        }

        auto turbo = input.IsDown(Key::Turbo);
        UpdateTurning(input, turbo, delta);
        auto forward = Axis(input, Key::Forward, Key::Back);
        auto strafe = Axis(input, Key::StrafeRight, Key::StrafeLeft);
        auto rise = Axis(input, Key::Jump, Key::Down);
        auto speed = NoclipSpeed * (turbo ? NoclipTurbo : 1.f);
        Feet += (Facing() * forward + Right() * strafe + Vector3::UnitZ() * rise) * speed * delta;
        ForwardSpeed = 0.f;
        StrafeSpeed = 0.f;
        VerticalSpeed = 0.f;
        OnFloor = false;
        return true;
    }
}
