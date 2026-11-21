#include "game/kurt/Kurt.h"

#include "game/Angles.h"
#include "game/kurt/CameraRoll.h"

// Sliding on his back down level 6's wind tunnels (damp_buttslide 0x468db8, kurt.gd start_slide,
// _update_slide; godot-mdk docs/gameplay.md "Sliding"). The wind zones (opcode 224) start it and push him.
//   wind zone ──► SLIP (K_SLIP) ──► SLIDE │ SLIDE_FAST (forward) │ SLIDE_BRAKE (back)
//   velocity += slope² + wind² (slide_accel), heading = velocity's, speed ≤ cap (15-80)
//   ends: 20 ticks in the air or 15 in a new arena ──► FALL │ at rest on the floor ──► gets up (K_BFLIP)

namespace Mdk::Game::Kurt
{
    using Engine::Platform::Input;
    using Engine::Platform::Key;
    using Collision::ArenaSpace;

    void Kurt::StartSlide()
    {
        if (Sliding || Health == 0)
        {
            return;
        }

        Sliding = true;
        _slideVelocity = Vector2::Zero();
        _slideCap = SlideCap;
        _slideNormal = Vector3::UnitZ();
        _slidePush = Vector2::Zero();
        _slideAir = 0.f;
        _slideLeft = 0.f;
        StopFiring();
        ChuteOpen = false;
        SetState(State::Slip);
    }

    void Kurt::EnterArena()
    {
        if (!Sliding || _slideLeft > 0.f)
        {
            return;
        }

        _slideLeft = SlideArenaTicks;
    }

    void Kurt::Blow(float dt)
    {
        _blown = true;
        VerticalSpeed -= SlideGravity * dt;
    }

    void Kurt::StopSlide()
    {
        Sliding = false;
        _slideVelocity = Vector2::Zero();
        _mixer.StopVoice(_slideVoice);
        _slideVoice = 0;
        _slideSound = "";
    }

    void Kurt::SlideAccel(Vector2 push, float dt)
    {
        _slideVelocity = Vector2(Accel(_slideVelocity.X, push.X, dt), Accel(_slideVelocity.Y, push.Y, dt));
    }

    float Kurt::Accel(float speed, float push, float dt)
    {
        if (push > SlidePushDead)
        {
            return speed + push * push * dt;
        }

        if (push < -SlidePushDead)
        {
            return speed - push * push * dt;
        }

        return MoveToward(speed, 0.f, SlideFriction * dt);
    }

    bool Kurt::UpdateSlide(Input& input, float delta)
    {
        if (!Sliding)
        {
            return false;
        }

        if (Health == 0)
        {
            StopSlide();
            return false;
        }

        // 15 ticks after an arena change he falls.
        if (_slideLeft > 0.f)
        {
            _slideLeft -= Ticks * delta;
            if (_slideLeft <= 0.f)
            {
                StopSlide();
                SetState(State::Fall);
                return false;
            }
        }

        if (OnFloor)
        {
            _slideNormal = Vector3(
                SlideNormalKeep * _slideNormal.X + (1.f - SlideNormalKeep) * _floorNormal.X,
                SlideNormalKeep * _slideNormal.Y + (1.f - SlideNormalKeep) * _floorNormal.Y,
                SlideNormalKeepZ * _slideNormal.Z + (1.f - SlideNormalKeepZ) * _floorNormal.Z);
            _slidePush = Vector2(_slideNormal.X, _slideNormal.Y) * SlideSlope;
            _slideAir = 0.f;
            if (_slidePush.LengthSquared() < SlideRest && _slideVelocity == Vector2::Zero())
            {
                // At rest he gets up (state 901 from K_BFLIP).
                StopSlide();
                SetState(State::Knocked);
                AnimationFrame = static_cast<float>(_frameCount(State::Knocked));
                return true;
            }
        }
        else
        {
            _slideAir += Ticks * delta;
            if (_slideAir > SlideAirTicks)
            {
                StopSlide();
                SetState(State::Fall);
                return false;
            }
        }

        SlideAccel(_slidePush, delta);
        CameraRoll->Slide(_slideNormal, Vector2(Facing().X, Facing().Y), delta);
        auto forward = Axis(input, Key::Forward, Key::Back);
        auto speed = SlideSpeed(forward, delta);
        Steer(input, speed, delta);
        SlideMove(delta);
        VerticalSpeed -= SlideGravity * delta;
        VerticalSpeed = MathF::Max(VerticalSpeed - Gravity * delta, -MaxFallSpeed);
        Fall(delta);
        UpdateTouched();
        UpdateSlideState(forward, delta);
        UpdateMuzzle(delta);
        return true;
    }

    float Kurt::SlideSpeed(float forward, float delta)
    {
        auto speed = MathF::Min(_slideVelocity.Length(), _slideCap);
        if (forward > 0.f)
        {
            if (speed >= SlideMinSpeed)
            {
                speed += SlideAcceleration * forward * delta;
            }

            _slideCap = MathF::Min(_slideCap + SlideCapRise * delta, SlideCapMax);
        }
        else if (forward < 0.f)
        {
            if (speed > SlideMinSpeed)
            {
                speed = MathF::Max(speed + SlideBrake * forward * delta, SlideMinSpeed);
            }

            _slideCap = MathF::Max(_slideCap - SlideCapFall * delta, SlideCapMin);
        }
        else
        {
            _slideCap = MoveToward(_slideCap, SlideCap, SlideCapRelax * delta);
        }

        return MathF::Min(speed, _slideCap);
    }

    void Kurt::Steer(Input& input, float speed, float delta)
    {
        if (MathF::Abs(_slideVelocity.X) + MathF::Abs(_slideVelocity.Y) > SlideHeading)
        {
            Yaw = RadiansToDegrees(MathF::Atan2(_slideVelocity.Y, _slideVelocity.X));
        }

        auto mouse = SlideMouseTurn * delta;
        Yaw += Axis(input, Key::TurnLeft, Key::TurnRight) * SlideTurn * delta - Math::Clamp(input.MouseX() * MouseDegrees, -mouse, mouse);
        _slideVelocity = Vector2(Facing().X, Facing().Y) * speed;
    }

    void Kurt::SlideMove(float delta)
    {
        auto planned = Vector3(_slideVelocity * delta, 0.f);
        if (planned == Vector3::Zero())
        {
            return;
        }

        auto start = Feet;
        Feet = Contact(_space.Move(Feet, planned, ArenaSpace::Motion::Walk, WalkSlide, Nearby(Feet + planned))).Feet;
        auto moved = Feet - start;
        if (!Stopped(planned.X, moved.X) && !Stopped(planned.Y, moved.Y))
        {
            return;
        }

        auto actual = Vector2(moved.X, moved.Y);
        auto direction = actual == Vector2::Zero() ? Vector2::Normalize(_slideVelocity) : Vector2::Normalize(actual);
        auto speed = _slideVelocity.Length() * SlideWallShare + actual.Length() / delta * (1.f - SlideWallShare);
        _slideVelocity = direction * speed;
    }

    void Kurt::UpdateSlideState(float forward, float delta)
    {
        StateTime += delta;
        AnimationFrame += Ticks * delta;
        if (Current != State::Slip || AnimationFrame >= _frameCount(State::Slip))
        {
            auto wanted = forward > 0.f ? State::SlideFast : forward < 0.f ? State::SlideBrake : State::Slide;
            if (wanted != Current)
            {
                SetState(wanted);
            }
        }

        std::string sound = Current == State::SlideBrake ? BrakeSound : SlideSound;
        if (sound != _slideSound)
        {
            _mixer.StopVoice(_slideVoice);
            _slideVoice = _mixer.PlayLooped(sound);
            _slideSound = sound;
        }

        _mixer.SetPitch(_slideVoice, Current == State::SlideFast ? SlideFastPitch : 1.f);
    }
}
