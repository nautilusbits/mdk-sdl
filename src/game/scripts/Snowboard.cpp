#include "game/scripts/Snowboard.h"

#include <cmath>
#include <string>
#include <vector>

#include "core/DotNet.h"
#include "game/audio/SoundMixer.h"
#include "game/kurt/CameraRoll.h"
#include "game/kurt/Kurt.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/ObjectMotion.h"
#include "game/scripts/Rides.h"
#include "game/scripts/ScriptMath.h"
#include "game/scripts/ScriptRuntime.h"

namespace Mdk::Game::Scripts
{
    using Engine::Platform::Input;
    using Engine::Platform::Key;
    using Objects::MdkObject;

    const float Snowboard::Ticks = Game::Kurt::Kurt::Ticks;

    Snowboard::Snowboard(ScriptRuntime& runtime, std::shared_ptr<MdkObject> board)
        : Board(std::move(board)), _runtime(&runtime), _kurt(runtime.Kurt)
    {
        _heading = Board->Yaw;
        _pathHeading = Board->Yaw;
    }

    void Snowboard::Update(Input& input, float delta)
    {
        auto dt = delta * Ticks;
        auto controls = (Board->Flags & Rides::FlagLocked) == 0;
        _pitch -= MathF::Abs(_steer) * CarveTilt;

        _steer = Steer(_steer, controls ? TurnInput(input, dt) : 0.0f, _grounded, _turnTicks, dt);
        if (_grounded)
        {
            UpdateSpeed(controls ? Axis(input, Key::Forward, Key::Back) : 0.0f, controls, dt);
        }
        else
        {
            _side = MoveToward(_side, 0.0f, SideAirBrake * dt);
        }

        // Forward and sideways, sticking to downhill floors, falling.
        auto h = ScriptMath::ToRadians(_heading);
        auto move = Vector3(MathF::Cos(h), MathF::Sin(h), 0.0f) * _speed + Vector3(MathF::Sin(h), -MathF::Cos(h), 0.0f) * _side;
        auto previous = _kurt->Feet;
        _kurt->Glide(move * Ticks, delta);

        UpdatePitch(delta);
        if (_grounded)
        {
            _speed = SlopeSpeed(_speed, _pitch, dt);
        }

        Jump(input, controls, dt);
        UpdateSounds(dt);
        if (_pose == Pose::Jump)
        {
            _kurt->StopFiring();
        }
        else
        {
            _kurt->UpdateGun(input, delta);
        }

        FollowPath(previous, dt);
        Turn(delta, dt);
        Place(delta);
        Ram();
        Animate(dt);
    }

    void Snowboard::GetOff()
    {
        for (auto sound : Sounds)
        {
            _runtime->Mixer->Stop(std::string(sound));
        }

        auto y = ScriptMath::ToRadians(_kurt->Yaw);
        auto throwing = Vector3(_speed * MathF::Cos(y) + _side * MathF::Sin(y), _speed * MathF::Sin(y) - _side * MathF::Cos(y), 0.0f);
        auto velocity = throwing * Ticks * ThrowFactor;
        velocity.Z = _kurt->VerticalSpeed;
        Board->Velocity = velocity;
        Board->Gravity = Gravity;
        Board->Friction = 0.0f;
        Board->Position = _kurt->Feet;
        _kurt->GetOffBoard(_speed * Ticks, _side * Ticks);
    }

    float Snowboard::Steer(float steer, float right, bool grounded, float& turnTicks, float dt)
    {
        if (right == 0.0f)
        {
            turnTicks = 0.0f;
            return MoveToward(steer, 0.0f, SteerReturn * dt);
        }

        turnTicks += dt;
        if ((right > 0.0f && steer > 0.0f) || (right < 0.0f && steer < 0.0f))
        {
            return 0.0f;
        }

        auto rate = right * (grounded && turnTicks < SteerRampTicks ? turnTicks * 2.0f / Ticks : 1.0f);
        return Math::Clamp(steer - rate * dt, -SteerMax, SteerMax);
    }

    void Snowboard::UpdateSpeed(float forward, bool controls, float dt)
    {
        if (_steer != 0.0f)
        {
            _side = _steer * SideFactor * _speed;
        }

        _speed = Accelerate(_speed, controls ? forward : 1.0f, dt);
    }

    float Snowboard::Accelerate(float speed, float forward, float dt)
    {
        if (forward > 0.0f)
        {
            return speed < Fast ? MathF::Min(speed + Acceleration * dt, Fast) : speed;
        }

        if (forward < 0.0f)
        {
            return speed > Slow ? MathF::Max(speed - Acceleration * dt, Slow) : speed;
        }

        return speed < Cruise ? MathF::Min(speed + Acceleration * dt, Cruise) : MathF::Max(speed - Decay * dt, Cruise);
    }

    float Snowboard::SlopeSpeed(float speed, float pitch, float dt)
    {
        auto k = MathF::Sin(ScriptMath::ToRadians(pitch));
        if (pitch > HalfTurn && speed < Downhill)
        {
            return MathF::Min(speed - k * SlopeAcceleration * dt, Downhill);
        }

        if (pitch > 0.0f && pitch <= HalfTurn && speed > Slow)
        {
            return MathF::Max(speed - k * SlopeAcceleration * dt, Slow);
        }

        return speed;
    }

    void Snowboard::UpdatePitch(float delta)
    {
        auto p = _kurt->Feet;
        auto yaw = ScriptMath::ToRadians(_heading + _steer);
        auto forward = Vector3(MathF::Cos(yaw), MathF::Sin(yaw), 0.0f) * BoardHalf;
        auto front = Ground(p + forward);
        auto back = Ground(p - forward);
        _grounded = front.has_value() || back.has_value();
        if (!_grounded)
        {
            return;
        }

        Vector3 v;
        if (front.has_value() && back.has_value())
        {
            v = *front - *back;
        }
        else if (front.has_value())
        {
            v = *front - p;
        }
        else
        {
            v = p - *back;
        }

        auto target = Wrap360(ScriptMath::ToDegrees(MathF::Atan2(v.Z, Vector2(v.X, v.Y).Length())));
        if (_pitch > PitchLimit && _pitch < HalfTurn)
        {
            target = PitchLimit;
        }
        else if (_pitch >= HalfTurn && _pitch < FullTurn - PitchLimit)
        {
            target = FullTurn - PitchLimit;
        }

        auto step = PitchRate * delta;
        _pitch = Wrap360(WrapHalf(_pitch) + Math::Clamp(WrapHalf(target - _pitch), -step, step));
    }

    std::optional<Vector3> Snowboard::Ground(Vector3 end)
    {
        auto hit = _runtime->Raycast(end + Vector3(0.0f, 0.0f, RayHeight), end - Vector3(0.0f, 0.0f, RayBelow));
        if (!hit.has_value())
        {
            return std::nullopt;
        }

        return hit->Point;
    }

    void Snowboard::Jump(Input& input, bool controls, float dt)
    {
        auto blocked = _runtime->CurrentArena == NoJumpArena && _kurt->Feet.Y >= NoJumpY;
        if (!controls || !input.IsDown(Key::Jump) || blocked)
        {
            _jumpTicks = 0.0f;
            return;
        }

        if (_grounded && _jumpTicks <= JumpBuffer)
        {
            _kurt->VerticalSpeed = JumpSpeed;
            _kurt->Feet += Vector3(0.0f, 0.0f, JumpLift);
            _kurt->OnFloor = false;
            _grounded = false;
            _jumpTicks = JumpHeld;
            _pose = Pose::Jump;
            _frame = 0.0f;
        }

        _jumpTicks += dt;
    }

    void Snowboard::UpdateSounds(float dt)
    {
        auto& mixer = *_runtime->Mixer;
        if (!_grounded)
        {
            _airTicks += dt;
            if (_airTicks >= QuietTicks && mixer.IsPlaying("SKI"))
            {
                mixer.Stop("SKI");
                mixer.Stop("SKITURN");
            }

            return;
        }

        if (_airTicks >= LandTicks)
        {
            mixer.Play("SKILAND", Audio::SoundMixer::Start::Restart);
        }

        _airTicks = 0.0f;
        mixer.Play("SKI", Audio::SoundMixer::Start::Once);
        if (MathF::Abs(_steer) > TurnSound)
        {
            mixer.Play("SKITURN", Audio::SoundMixer::Start::Once);
        }
        else
        {
            mixer.Stop("SKITURN");
        }
    }

    void Snowboard::FollowPath(Vector3 previous, float dt)
    {
        auto& motion = *_runtime->Motion;
        if (Board->Path == 0)
        {
            return;
        }

        auto last = static_cast<float>(motion.PathKeyFrame(Board->Path, motion.PathKeyCount(Board->Path) - 1));
        auto t = Board->PathTime;
        if (t >= last)
        {
            return;
        }

        auto kurt = Vector2(_kurt->Feet.X, _kurt->Feet.Y);
        auto start = t;
        for (auto step : PathSteps)
        {
            while (t + step < last && Distance(t + step, kurt) < Distance(t, kurt))
            {
                t += step;
            }
        }

        auto moved = Vector2(_kurt->Feet.X - previous.X, _kurt->Feet.Y - previous.Y).Length();
        auto stuck = _speed >= Slow && dt > 0.0f && moved / dt < UnstickSpeed;
        if (t == start)
        {
            // Stuck where the path time can't move on (against a wall beside it): towards the path ahead.
            if (stuck)
            {
                Nudge(PathPoint(t + PathAhead), kurt);
            }

            return;
        }

        auto here = PathPoint(start);
        auto ahead = PathPoint(t + PathAhead);
        Board->PathTime = t;
        Board->PathStop = ToInt(MathF::Round(t));
        _pathHeading = ScriptMath::ToDegrees(MathF::Atan2(ahead.Y - here.Y, ahead.X - here.X));
        if (stuck)
        {
            Nudge(here, kurt);
        }
    }

    void Snowboard::Nudge(Vector3 to, Vector2 kurt)
    {
        auto way = Vector2(to.X, to.Y) - kurt;
        if (way == Vector2::Zero())
        {
            return;
        }

        // Against the walls: the path may run inside a bank (MEAT_4, y 9360), and a step set
        // straight into Feet put Kurt in it, over no floor.
        auto step = Vector2::Normalize(way) * UnstickStep;
        _kurt->Shove(Vector3(step.X, step.Y, 0.0f));
    }

    Vector3 Snowboard::PathPoint(float t)
    {
        return _runtime->Motion->PathPosition(Board->Path, t) + Board->PathOrigin;
    }

    float Snowboard::Distance(float t, Vector2 kurt)
    {
        auto p = PathPoint(t);
        return Vector2::DistanceSquared(Vector2(p.X, p.Y), kurt);
    }

    void Snowboard::Turn(float delta, float dt)
    {
        auto rate = HeadingAir;
        if (_grounded)
        {
            auto d = WrapHalf(_pathHeading - _heading);
            rate = HeadingRate + (d >= 0.0f ? _steer : -_steer) * HeadingCarve;
        }

        _heading = TurnTowards(_heading, _pathHeading, MathF::Max(rate, 0.0f) * dt);
        _kurt->Yaw = TurnTowards(_kurt->Yaw, _heading, KurtTurn * delta);
    }

    float Snowboard::TurnTowards(float from, float to, float step)
    {
        return Wrap360(from + Math::Clamp(WrapHalf(to - from), -step, step));
    }

    void Snowboard::Place(float delta)
    {
        _pitch = Wrap360(_pitch + MathF::Abs(_steer) * CarveTilt);
        Board->Position = _kurt->Feet - Vector3(0.0f, 0.0f, BoardDrop);
        Board->Yaw = Wrap360(_heading + _steer);
        Board->Pitch = WrapHalf(_pitch);
        // The view rolls with the board's bank (0x46ac4c).
        _kurt->CameraRoll->Follow(WrapHalf(Board->Roll), delta);
    }

    void Snowboard::Ram()
    {
        // A copy: killing may change what Kurt touches (C#'s foreach would throw).
        auto touched = _kurt->Touched();
        for (auto* obj : touched)
        {
            if (obj == nullptr || obj == Board.get() || obj->Dead || (obj->Flags & RamSpared) != 0 || obj->Health <= 0
                || obj->Health >= ScriptRuntime::Indestructible)
            {
                continue;
            }

            _runtime->Kill(*obj);
            _runtime->HurtKurt(RamDamage);
        }
    }

    void Snowboard::Animate(float dt)
    {
        _frame += dt;
        switch (_pose)
        {
            case Pose::Surf:
                _kurt->CameraPivot = Game::Kurt::Kurt::DefaultPivot;
                _kurt->Pose = Game::Kurt::Kurt::PoseFrame{std::string(SurfFrames), ToInt(_frame) % SurfLoop};
                return;
            case Pose::Jump:
                _frame = MathF::Min(_frame, static_cast<float>(JumpHoldFrame));
                if (_grounded && _kurt->VerticalSpeed <= 0.0f)
                {
                    _pose = _frame > LateLanding ? Pose::Land : Pose::Surf;
                    _frame = _pose == Pose::Land ? static_cast<float>(JumpLandFrame) : 0.0f;
                }

                break;
            case Pose::Land:
                if (_frame >= LandEnd)
                {
                    _pose = Pose::Surf;
                    _frame = 0.0f;
                }

                break;
        }

        _kurt->CameraPivot = JumpPivot(_frame, _kurt->CameraPivot, dt / Ticks);
        _kurt->Pose = Game::Kurt::Kurt::PoseFrame{std::string(_pose == Pose::Surf ? SurfFrames : JumpFrames), ToInt(_frame)};
    }

    float Snowboard::JumpPivot(float frame, float pivot, float seconds)
    {
        return frame < JumpHoldFrame ? Game::Kurt::Kurt::DefaultPivot - ToInt(frame) * PivotDip
                                     : MathF::Min(pivot + seconds, Game::Kurt::Kurt::DefaultPivot);
    }

    float Snowboard::TurnInput(Input& input, float dt)
    {
        if (input.MouseX() != 0.0f && dt > 0.0f)
        {
            return SteerRate * Math::Clamp(input.MouseX() * MouseUnits / dt, -MouseLimit, MouseLimit);
        }

        auto turn = Axis(input, Key::TurnRight, Key::TurnLeft);
        auto strafe = Axis(input, Key::StrafeRight, Key::StrafeLeft);
        return SteerRate * (MathF::Abs(strafe) < MathF::Abs(turn) ? turn : strafe);
    }

    float Snowboard::Axis(Input& input, Key positive, Key negative)
    {
        return (input.IsDown(positive) ? 1.0f : 0.0f) - (input.IsDown(negative) ? 1.0f : 0.0f);
    }

    float Snowboard::MoveToward(float from, float to, float step)
    {
        return MathF::Abs(to - from) <= step ? to : from + MathF::Sign(to - from) * step;
    }

    float Snowboard::Wrap360(float degrees)
    {
        return std::fmod(std::fmod(degrees, FullTurn) + FullTurn, FullTurn);
    }

    float Snowboard::WrapHalf(float degrees)
    {
        return Wrap360(degrees + HalfTurn) - HalfTurn;
    }
}
