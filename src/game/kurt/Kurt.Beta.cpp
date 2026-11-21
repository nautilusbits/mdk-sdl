#include "game/kurt/Kurt.h"

// The 1996 demo's moves (kurt.gd beta_moves, godot-mdk docs/beta96.md "Kurt"), drawn by its
// damp_animate (0x36cf4): states 701 and 702 roll Kurt to his left or right, 703 puts the sniper
// helmet on and 900 takes it off; backing up shows K_BCKUP.
//   Z / C ──► roll: K_ROLLL / K_ROLLR once, sideways, turning 90° the other way ──► still
//   sniper key ──► helmet on (K_HELM) ──► sniper mode ──► sniper key ──► helmet off (backwards)

namespace Mdk::Game::Kurt
{
    using Engine::Platform::Input;
    using Engine::Platform::Key;
    using Collision::ArenaSpace;

    bool Kurt::StartRoll(Input& input)
    {
        if (!BetaMoves || !OnFloor || Health == 0 || Knocked() || Current == State::Throw || Walking != Walk::Normal)
        {
            return false;
        }

        auto left = input.IsDown(Key::RollLeft);
        auto roll = left ? State::RollLeft : State::RollRight;
        if ((!left && !input.IsDown(Key::RollRight)) || _frameCount(roll) == 0)
        {
            return false;
        }

        StopFiring();
        ForwardSpeed = 0.f;
        StrafeSpeed = 0.f;
        _mixer.Play("ROLL");
        SetState(roll);
        return true;
    }

    void Kurt::PutHelmetOn()
    {
        StopFiring();
        ForwardSpeed = 0.f;
        StrafeSpeed = 0.f;
        TurnRate = 0.f;
        SetState(State::HelmetOn);
    }

    void Kurt::UpdateBetaMove(float delta)
    {
        auto frames = Math::Max(_frameCount(Current), 1);
        AnimationFrame += Ticks * delta;
        StateTime += delta;
        if (Current == State::RollLeft || Current == State::RollRight)
        {
            auto side = Current == State::RollLeft ? -1.f : 1.f;
            Yaw += side * RollTurn / frames * Ticks * delta;
            auto move = Right() * (side * RollSpeed * delta);
            Feet = Contact(_space.Move(Feet, move, ArenaSpace::Motion::Walk, WalkSlide, Nearby(Feet + move))).Feet;
        }

        VerticalSpeed = MathF::Max(VerticalSpeed - Gravity * delta, -MaxFallSpeed);
        Fall(delta);
        UpdateTouched();
        if (AnimationFrame < frames - 1)
        {
            return;
        }

        auto helmetOn = Current == State::HelmetOn;
        SetState(State::Still);
        if (helmetOn)
        {
            EnterSniper();
        }
    }
}
