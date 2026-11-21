#include "game/kurt/Kurt.h"

#include "game/kurt/Scope.h"

// Sniper mode (kurt.gd _enter_sniper, leave_sniper, _update_sniper; godot-mdk docs/gameplay.md
// "Sniper mode"): Kurt stands, sidesteps and looks through the scope; the rounds are the scripts
// runtime's (SniperFire).
//   sniper key ──on the floor──► SNIPERON, BREATH ──► look, zoom, sidestep, clip, fire
//        ▲                                                │
//        └── sniper key (SNIPEROFF), falling, knock-down, death, cutscenes (StopFiring)

namespace Mdk::Game::Kurt
{
    using Engine::Platform::Input;
    using Engine::Platform::Key;
    using Collision::ArenaSpace;
    using ScopeType = Game::Kurt::Scope;

    Vector3 Kurt::SniperEye() const
    {
        return Scope->Eye(Feet, Facing());
    }

    Vector3 Kurt::SniperForward() const
    {
        return Scope->Forward(Yaw);
    }

    void Kurt::EnterSniper()
    {
        if (!CanSnipe())
        {
            return;
        }

        StopFiring();
        Sniping = true;
        ForwardSpeed = 0.f;
        StrafeSpeed = 0.f;
        TurnRate = 0.f;
        Scope->Reset();
        SetState(State::Still);
        _mixer.Play("SNIPERON");
        _breathVoice = _mixer.PlayLooped("BREATH");
    }

    void Kurt::LeaveSniper(SniperExit exit)
    {
        if (!Sniping)
        {
            return;
        }

        Sniping = false;
        StrafeSpeed = 0.f;
        _mixer.StopVoice(_breathVoice);
        _mixer.StopVoice(_zoomVoice);
        _breathVoice = 0;
        _zoomVoice = 0;
        if (exit == SniperExit::WithSound)
        {
            _mixer.Play("SNIPEROFF");
        }
    }

    bool Kurt::UpdateSniper(Input& input, bool turbo, float delta)
    {
        if (Pressed(input, Key::Sniper))
        {
            if (Sniping)
            {
                LeaveSniper(SniperExit::WithSound);
                if (!BetaMoves || _frameCount(State::HelmetOff) == 0)
                {
                    return false;
                }

                // The demo's helmet comes off (state 900).
                SetState(State::HelmetOff);
                return true;
            }

            if (BetaMoves && CanSnipe() && _frameCount(State::HelmetOn) != 0)
            {
                PutHelmetOn();
                return true;
            }

            EnterSniper();
        }

        if (Sniping && (Health == 0 || (!OnFloor && (VerticalSpeed < SniperFallSpeed || VerticalSpeed > SniperRiseSpeed))))
        {
            LeaveSniper();
        }

        if (!Sniping)
        {
            return false;
        }

        SniperControls(input, turbo, delta);
        SniperMove(input, turbo, delta);
        static const std::function<bool(int)> AlwaysFires = [](int) { return true; };
        auto trigger = input.IsDown(Key::Fire) ? ScopeType::Trigger::Held : ScopeType::Trigger::Released;
        auto clip = Scope->UpdateClip(*Inventory, trigger, SniperFire ? SniperFire : AlwaysFires, delta);
        if (Any(clip & ScopeType::Clip::Loaded))
        {
            _mixer.Play("SNIPRELD");
        }

        if (Any(clip & ScopeType::Clip::Fired))
        {
            _mixer.Play("SNIPERSHOT");
        }

        StateTime += delta;
        return true;
    }

    void Kurt::SniperControls(Input& input, bool turbo, float delta)
    {
        auto turn = Axis(input, Key::TurnRight, Key::TurnLeft);
        auto tilt = Axis(input, Key::Back, Key::Forward);
        auto mouse = Vector2(input.MouseX(), input.MouseY()) * MouseDegrees;
        Yaw += Scope->Look(turn, tilt, mouse, turbo ? ScopeType::Turbo::On : ScopeType::Turbo::Off, delta);
        _mouseTurn = MouseTurn::Scope;

        Scope->Wheel(input.ZoomWheel());
        auto zooming = Scope->UpdateZoom(Axis(input, Key::ZoomOut, Key::ZoomIn), delta);
        if (zooming == ScopeType::Zooming::Moving && !_mixer.IsVoicePlaying(_zoomVoice))
        {
            _zoomVoice = _mixer.PlayLooped("ZOOM");
        }
        else if (zooming == ScopeType::Zooming::Still && _zoomVoice != 0)
        {
            _mixer.StopVoice(_zoomVoice);
            _zoomVoice = 0;
        }

        // The wheel zooms here (unless it was bound to something else), so it doesn't pick ammo too
        // (kurt.gd _wheel_used).
        if (input.ZoomWheel() != 0.f)
        {
            return;
        }

        if (Pressed(input, Key::ItemNext))
        {
            Scope->SelectAmmo(*Inventory, 1);
        }

        if (Pressed(input, Key::ItemPrevious))
        {
            Scope->SelectAmmo(*Inventory, -1);
        }
    }

    void Kurt::SniperMove(Input& input, bool turbo, float delta)
    {
        auto max = (turbo ? MaxSpeedTurbo : MaxSpeed) * SniperStrafe;
        ForwardSpeed = 0.f;
        StrafeSpeed = Math::Clamp(Accelerate(StrafeSpeed, Axis(input, Key::StrafeRight, Key::StrafeLeft), turbo, 1.f, 1.f, delta), -max, max);
        auto move = (Right() * StrafeSpeed + Vector3(_push, 0.f)) * delta;
        DrainPush(delta);
        if (move != Vector3::Zero())
        {
            Feet = _space.Move(Feet, move, ArenaSpace::Motion::Walk, WalkSlide, Nearby(Feet + move)).Feet;
        }

        VerticalSpeed = MathF::Max(VerticalSpeed - Gravity * delta, -MaxFallSpeed);
        Fall(delta);
    }
}
