#include "Mdk.Game/Kurt/Kurt.h"

#include "Mdk.Game/Angles.h"
#include "Mdk.Game/Kurt/CameraRoll.h"
#include "Mdk.Game/Kurt/Scope.h"

namespace Mdk::Game::Kurt
{
    using Engine::Platform::Input;
    using Engine::Platform::Key;
    using Collision::ArenaSpace;
    using Collision::Solids;

    Kurt::Kurt(ArenaSpace& space, Audio::SoundMixer& mixer, std::function<int(State)> frameCount)
        : CameraRoll(std::make_shared<Game::Kurt::CameraRoll>()),
          Inventory(std::make_shared<Game::Kurt::Inventory>()),
          Scope(std::make_shared<Game::Kurt::Scope>()),
          _space(space),
          _mixer(mixer),
          _frameCount(std::move(frameCount))
    {
    }

    Kurt::~Kurt() = default;

    Vector3 Kurt::Facing() const
    {
        return Vector3(MathF::Cos(DegreesToRadians(Yaw)), MathF::Sin(DegreesToRadians(Yaw)), 0.f);
    }

    void Kurt::Update(Input& input, float delta)
    {
        // The scope's own bindings (zoom, the wheel) apply while he snipes, whatever else this step does.
        input.Scoped = Sniping;
        UpdateStep(input, delta);
        // Entering or leaving the scope this step switches the bindings now, not a step late: a wheel
        // notch right after the sniper key zooms (or, rebound, does its scope action).
        input.Scoped = Sniping;
    }

    void Kurt::UpdateStep(Input& input, float delta)
    {
        _mouseTurn = MouseTurn::None;
        Invulnerable = MathF::Max(Invulnerable - delta, 0.f);
        ChuteClosing = false;
        if (Frozen)
        {
            // Cutscenes and the level's end: he stands still; the flashes still fade (0x478704).
            FadeFlashes(delta);
            StopChuteSound();
            ForwardSpeed = 0.f;
            StrafeSpeed = 0.f;
            VerticalSpeed = 0.f;
            return;
        }

        if (Current == State::Dead)
        {
            StopChuteSound();
            UpdateDeath(delta);
            return;
        }

        FadeFlashes(delta);
        CameraRoll->Settle(delta);
        if (UpdateNoclip(input, delta))
        {
            return;
        }

        if (Ride)
        {
            // No knock-down, chute or ledges while riding.
            KnockDamage = 0.f;
            StopChuteSound();
            _keeper = nullptr;
            Ride(input, delta);
            return;
        }

        UpdateKnockDamage(delta);
        UpdateChuteSound();
        auto turbo = input.IsDown(Key::Turbo);
        if (InBetaMove())
        {
            UpdateBetaMove(delta);
            return;
        }

        if (UpdateSniper(input, turbo, delta) || UpdateSlide(input, delta))
        {
            return;
        }

        if (Current == State::Hang || Current == State::HardLand)
        {
            // States from 800 on: no walking (damp_control).
            UpdateSpecial(delta);
            return;
        }

        if (StartRoll(input))
        {
            return;
        }

        UpdateTurning(input, turbo, delta);
        CheckKeeper();

        // Throwing, knocked down or kept by a platform, he doesn't walk.
        auto forward = Axis(input, Key::Forward, Key::Back);
        auto strafe = Walking == Walk::Riding ? 0.f : Axis(input, Key::StrafeRight, Key::StrafeLeft);
        if (Current == State::Throw || Current == State::Knocked || Current == State::GetUp || Kept())
        {
            forward = 0.f;
            strafe = 0.f;
        }

        auto air = OnFloor ? 1.f : AirControl;
        ForwardSpeed = Accelerate(ForwardSpeed, forward, turbo, 1.f, air, delta);
        StrafeSpeed = Accelerate(StrafeSpeed, strafe, turbo, air, air, delta);
        // Running while turning with the keys (not the mouse) rolls the view.
        CameraRoll->Walk(-Axis(input, Key::TurnLeft, Key::TurnRight), forward, delta);

        auto before = Feet;
        auto move = (Facing() * ForwardSpeed + Right() * StrafeSpeed + Vector3(_push, 0.f)) * delta;
        DrainPush(delta);
        if (move != Vector3::Zero())
        {
            Feet = Contact(_space.Move(Feet, move, ArenaSpace::Motion::Walk, WalkSlide, Nearby(Feet + move))).Feet;
        }

        if (Walking == Walk::Riding)
        {
            RideWalker(input, forward, delta);
            return;
        }

        UpdateVertical(input, move, delta);
        StayOnKeeper();
        UpdateTouched();
        if (Current == State::HardLand || GrabLedge(before, forward))
        {
            return;
        }

        UpdateItems(input);
        UpdateFiring(input);
        UpdateState(forward, strafe, delta);
        UpdateMuzzle(delta);
    }

    void Kurt::Teleport(Vector3 feet, float yaw)
    {
        // The chute closes silently.
        ChuteOpen = false;
        StopChuteSound();
        _keeper = nullptr;
        Feet = feet;
        Yaw = yaw;
        ForwardSpeed = 0.f;
        StrafeSpeed = 0.f;
        VerticalSpeed = 0.f;
        Teleports++;
    }

    Vector3 Kurt::Shove(Vector3 offset)
    {
        auto start = Feet;
        auto move = offset;
        move.Z = 0.f;
        if (move != Vector3::Zero())
        {
            Feet = Contact(_space.Move(Feet, move, ArenaSpace::Motion::Walk, WalkSlide, Nearby(Feet + move))).Feet;
        }

        return Feet - start;
    }

    void Kurt::Carry(Vector3 from, Vector3 to, float turn)
    {
        auto offset = Vector3::Transform(Feet - from, Matrix4x4::CreateRotationZ(DegreesToRadians(turn)));
        Feet = to + offset;
        Yaw += turn;
    }

    std::span<const Solids::Solid> Kurt::Nearby(Vector3 to)
    {
        if (!SolidsWithin)
        {
            return {};
        }

        auto min = Vector3::Min(Feet, to) - NearbyMargin();
        auto max = Vector3::Max(Feet, to) + NearbyMargin() + Vector3(0.f, 0.f, KurtHeight);
        return SolidsWithin(Formats::Box(min, max));
    }

    std::optional<Kurt::PlatformHit> Kurt::FindPlatform(float dz)
    {
        if (VerticalSpeed > 0.f)
        {
            return std::nullopt;
        }

        auto bottom = Feet.Z + dz;
        auto at = Feet;
        at.Z = bottom;
        auto platform = Solids::Platform(Feet, bottom, Nearby(at));
        if (!platform || bottom > platform->first)
        {
            return std::nullopt;
        }

        return PlatformHit{ platform->first, platform->second };
    }

    float Kurt::Axis(Input& input, Key positive, Key negative)
    {
        return (input.IsDown(positive) ? 1.f : 0.f) - (input.IsDown(negative) ? 1.f : 0.f);
    }

    void Kurt::UpdateTurning(Input& input, bool turbo, float delta)
    {
        auto turn = Axis(input, Key::TurnLeft, Key::TurnRight);
        if (turn == 0.f)
        {
            auto friction = MathF::Abs(TurnRate) > TurnSpeed ? TurnFrictionFast : TurnFrictionSlow;
            TurnRate = MoveToward(TurnRate, 0.f, friction * delta);
        }
        else
        {
            auto max = turbo ? TurnSpeedTurbo : TurnSpeed;
            auto acceleration = turbo ? TurnAccelerationTurbo : TurnAcceleration;
            TurnRate = Math::Clamp(TurnRate + turn * acceleration * delta, -max, max);
        }

        Yaw += TurnRate * delta - input.MouseX() * MouseDegrees;
        _mouseTurn = MouseTurn::Walk;
    }

    float Kurt::MouseLook(Input& input)
    {
        if (_mouseTurn == MouseTurn::Walk)
        {
            auto turn = -input.TakeMouseX() * MouseDegrees;
            Yaw += turn;
            return turn;
        }

        if (_mouseTurn != MouseTurn::Scope)
        {
            return 0.f;
        }

        // The mouse's share of the scope's look: no keys, no time.
        auto mouseX = input.TakeMouseX();
        auto mouse = Vector2(mouseX, input.TakeMouseY()) * MouseDegrees;
        auto scope = Scope->Look(0.f, 0.f, mouse, Game::Kurt::Scope::Turbo::Off, 0.f);
        Yaw += scope;
        return scope;
    }

    float Kurt::Accelerate(float speed, float input, bool turbo, float accelFactor, float frictionFactor, float delta)
    {
        if (input == 0.f)
        {
            auto friction = MathF::Abs(speed) > MaxSpeed ? FrictionFast : FrictionSlow;
            return MoveToward(speed, 0.f, friction * frictionFactor * delta);
        }

        auto acceleration = (turbo ? AccelerationTurbo : Acceleration) * accelFactor;
        auto max = turbo ? MaxSpeedTurbo : MaxSpeed;
        if (speed * input < 0.f)
        {
            speed = 0.f;
        }

        return Math::Clamp(speed + input * acceleration * delta, -max, max);
    }

    void Kurt::UpdateVertical(Input& input, Vector3 move, float delta)
    {
        auto jumpHeld = input.IsDown(Key::Jump);
        if (!jumpHeld)
        {
            _jumpReleased = true;
            if (_jumpTicksLeft > 0)
            {
                VerticalSpeed -= _jumpTicksLeft * JumpReleasePenalty;
                _jumpTicksLeft = 0;
            }
        }

        if (OnFloor)
        {
            ChuteOpen = false;
            if (jumpHeld && _jumpReleased)
            {
                VerticalSpeed = JumpSpeed;
                _jumpTicksLeft = JumpHoldTicks;
                _jumpReleased = false;
            }
            else
            {
                GlueDownhill(move, delta, true);
            }
        }
        else
        {
            UpdateAir(jumpHeld, delta);
        }

        UpdateUpdraft(delta);
        Fall(delta);
        _blown = false;
    }

    void Kurt::UpdateAir(bool jumpHeld, float delta)
    {
        if (_jumpTicksLeft > 0)
        {
            _jumpTicksLeft = Math::Max(0, _jumpTicksLeft - ToInt(MathF::Ceiling(delta * Ticks)));
        }

        // The wind keeps the chute shut.
        if (!ChuteOpen && jumpHeld && !_blown && (VerticalSpeed < FallStartSpeed || _pastJumpTop))
        {
            ChuteOpen = true;
            _mixer.Play("CHUTEOUT");
        }
        else if (ChuteOpen && (_blown || (!jumpHeld && !InUpdraft)))
        {
            ChuteOpen = false;
        }

        if (!ChuteOpen)
        {
            VerticalSpeed = MathF::Max(VerticalSpeed - Gravity * delta, -MaxFallSpeed);
            return;
        }

        VerticalSpeed -= ChuteGravity * delta;
        if (VerticalSpeed < ChuteFallSpeed)
        {
            VerticalSpeed = MathF::Min(VerticalSpeed + ChuteBrake * delta, ChuteFallSpeed);
        }
    }

    void Kurt::UpdateChuteSound()
    {
        if (ChuteOpen && Current == State::Chute && AnimationFrame >= ChuteOpenFrames)
        {
            _mixer.Play(ChuteOnSound, Audio::SoundMixer::Start::Once);
            return;
        }

        if (ChuteOpen || !_mixer.IsPlaying(ChuteOnSound))
        {
            return;
        }

        _mixer.Stop(ChuteOnSound);
        _mixer.Play("CHUTEIN");
    }

    void Kurt::GlueDownhill(Vector3 move, float delta, bool exact)
    {
        auto n = _floorNormal.Z < 0.f ? -_floorNormal : _floorNormal;
        auto downhill = move.X * n.X + move.Y * n.Y;
        if (n.Z > GlueNz && downhill > 0.f)
        {
            auto speed = (exact ? downhill / n.Z : downhill) / delta;
            VerticalSpeed = MathF::Min(VerticalSpeed, -MathF::Min(speed, exact ? GlueMaxSpeed : speed));
        }

        VerticalSpeed = MathF::Max(VerticalSpeed - Gravity * delta, -MaxFallSpeed);
    }

    void Kurt::Fall(float delta)
    {
        FallMove(delta);
        auto bottom = _space.Bottom(Feet);
        if (!bottom || Feet.Z > *bottom + FallOutDepth || Mortality == Game::Kurt::Mortality::God)
        {
            return;
        }

        Health = 0;
        VerticalSpeed = 0.f;
        OnFloor = true;
    }

    void Kurt::FallMove(float delta)
    {
        auto before = Feet;
        auto dz = VerticalSpeed * delta;
        auto platform = KeptPlatform(dz);
        if (!platform)
        {
            platform = FindPlatform(dz);
        }

        if (platform)
        {
            dz = platform->Top + PlatformLift - Feet.Z;
        }

        auto result = Contact(_space.Move(Feet, Vector3(0.f, 0.f, dz), ArenaSpace::Motion::Fall, FallSlide));
        Feet = result.Feet;
        Platform = nullptr;
        if (!result.Hit() && platform)
        {
            Feet.Z = platform->Top;
            Platform = platform->Owner;
            KeepOn(platform->Owner);
            Land(before, Vector3::UnitZ(), delta);
            return;
        }

        if (!result.Hit())
        {
            // Sliding down a steep face: the real fall rate.
            if (Feet.Z < before.Z)
            {
                VerticalSpeed = (Feet.Z - before.Z) / delta;
            }

            OnFloor = false;
            return;
        }

        if (Sliding)
        {
            VerticalSpeed = (Feet.Z - before.Z) / delta;
            OnFloor = true;
            _floorNormal = result.Normal();
            return;
        }

        if (VerticalSpeed > 0.f)
        {
            VerticalSpeed = 0.f;
            OnFloor = false;
            return;
        }

        ClimbOut(result);
        Land(before, result.Normal(), delta);
    }

    void Kurt::ClimbOut(const ArenaSpace::Result& result)
    {
        auto sunk = ArenaSpace::Sunk(result, Feet);
        if (sunk <= SunkMin || sunk > SunkMax || Ride)
        {
            return;
        }

        auto up = _space.Move(Feet, Vector3(0.f, 0.f, sunk), ArenaSpace::Motion::Fall, FallSlide);
        if (up.Hit())
        {
            return;
        }

        Feet = up.Feet;
    }

    void Kurt::Land(Vector3 before, Vector3 normal, float delta)
    {
        if (VerticalSpeed < HardLandingSpeed && !Ride)
        {
            HardLanding();
        }
        else if (Vector3::DistanceSquared(Feet, before) < CreepSpeed * delta * (CreepSpeed * delta))
        {
            // No creeping on a landing.
            Feet = before;
        }

        VerticalSpeed = 0.f;
        OnFloor = true;
        _floorNormal = normal;
    }

    void Kurt::HardLanding()
    {
        if (Health > 0)
        {
            Damage(HardLandingDamage);
        }

        KnockDamage = 0.f;
        if (Health == 0 || Current == State::Dead || Current == State::Knocked || Current == State::GetUp)
        {
            return;
        }

        StopFiring();
        ChuteOpen = false;
        SetState(State::HardLand);
    }

    void Kurt::UpdateSpecial(float delta)
    {
        if (Current == State::Hang)
        {
            UpdateClimb(delta);
            return;
        }

        StateTime += delta;
        VerticalSpeed = MathF::Max(VerticalSpeed - Gravity * delta, -MaxFallSpeed);
        Fall(delta);
        UpdateTouched();
        auto ticks = AnimationFrame * CrashTicksPerFrame;
        auto next = ticks + Ticks * delta;
        if (ticks <= CrashHoldTick && next >= CrashHoldTick && !OnFloor && VerticalSpeed <= 0.f && !ChuteOpen)
        {
            next = CrashHoldTick;
        }

        AnimationFrame = next / CrashTicksPerFrame;
        if (AnimationFrame >= _frameCount(State::HardLand))
        {
            SetState(State::Still);
        }
    }

    void Kurt::UpdateState(float forward, float strafe, float delta)
    {
        StateTime += delta;
        _airFor = OnFloor ? 0.f : _airFor + delta;
        if (OnFloor || (VerticalSpeed > 0.f && !ChuteOpen))
        {
            _pastJumpTop = false;
        }
        if (UpdateCombatState(delta))
        {
            return;
        }

        auto done = AnimationFrame >= _frameCount(Current) - 1;
        auto jumping = Current == State::Jump || Current == State::RunJump;
        if (!OnFloor)
        {
            if (ChuteOpen)
            {
                SetState(State::Chute);
            }
            else if (Current == State::Chute)
            {
                // Closed in the air: its closing frame, then the fall.
                ChuteClosing = true;
                SetState(State::Fall);
                return;
            }
            else if (VerticalSpeed > 0.f && !jumping && Current != State::Fall)
            {
                SetState(MathF::Abs(ForwardSpeed) > MovingSpeed ? State::RunJump : State::Jump);
            }
            // A jump falls when its frames run out (Animate), as the original's priorities have it.
            else if (VerticalSpeed < FallStartSpeed && !jumping && (_airFor >= FallGrace || Current == State::Fall))
            {
                SetState(State::Fall);
            }
        }
        else if (jumping || Current == State::Fall || Current == State::Chute)
        {
            _mixer.Play("LAND");
            SetState(State::Land);
        }
        else if (Current == State::Land && !done && forward == 0.f && strafe == 0.f)
        {
        }
        else if (forward != 0.f || MathF::Abs(ForwardSpeed) > MovingSpeed)
        {
            SetState(Firing ? State::RunFire : State::Run);
        }
        else if (strafe != 0.f || MathF::Abs(StrafeSpeed) > MovingSpeed)
        {
            SetState(State::Side);
        }
        else if (MathF::Abs(TurnRate) > MovingSpeed)
        {
            SetState(State::Turn);
        }
        else if (Firing)
        {
            SetState(State::Shot);
        }
        else if (Current == State::Idle && !done)
        {
        }
        else if (Current == State::Still && StateTime > IdleDelay)
        {
            SetState(State::Idle);
        }
        else if (Current != State::Still)
        {
            SetState(State::Still);
        }

        Animate(delta);
    }

    void Kurt::Animate(float delta)
    {
        switch (Current)
        {
            case State::Run:
            case State::RunFire:
            {
                auto count = _frameCount(Current);
                auto previous = Wrap(ToInt(MathF::Floor(AnimationFrame)), count);
                AnimationFrame += RunRate() * Ticks * delta;
                auto current = Wrap(ToInt(MathF::Floor(AnimationFrame)), count);
                Footstep(previous, current);
                break;
            }
            case State::Turn:
                AnimationFrame = Yaw / TurnFrameDegrees * _frameCount(State::Turn);
                break;
            case State::Jump:
            case State::RunJump:
                AnimateJump(delta);
                break;
            default:
                AnimationFrame += Ticks * delta;
                break;
        }
    }

    void Kurt::AnimateJump(float delta)
    {
        auto standing = Current == State::Jump;
        auto timed = standing ? JumpTimedFrames : RunJumpTimedFrames;
        auto frames = standing ? JumpFrames : RunJumpFrames;
        if (AnimationFrame < timed)
        {
            AnimationFrame = MathF::Min(AnimationFrame + Ticks * delta, static_cast<float>(timed));
            return;
        }

        auto frame = ToInt(AnimationFrame);
        while (frame < frames && VerticalSpeed <= JumpSpeeds[frame])
        {
            frame++;
        }

        AnimationFrame = static_cast<float>(frame);
        _pastJumpTop |= VerticalSpeed <= 0.f;
        if (frame >= frames)
        {
            SetState(State::Fall);
        }
    }

    void Kurt::Footstep(int previous, int current)
    {
        if (current == previous)
        {
            return;
        }

        auto firing = Current == State::RunFire;
        auto left = firing ? FireLeftStep : LeftStep;
        auto right = firing ? FireRightStep : RightStep;
        if (current == left)
        {
            _mixer.Play(_footstepPair ? "FOOT3" : "FOOT1");
        }
        else if (current == right)
        {
            _mixer.Play(_footstepPair ? "FOOT4" : "FOOT2");
            _footstepPair = !_footstepPair;
        }
    }

    float Kurt::RunRate() const
    {
        auto u = MathF::Abs(ForwardSpeed) / Ticks * 1.5f;
        auto rate = u <= 1.f ? 0.75f * u + 0.25f : 0.25f * u + 0.75f;
        return ForwardSpeed < 0.f && !BackingUp() ? -rate : rate;
    }

    void Kurt::SetState(State state)
    {
        if (state == Current && StateTime > 0.f)
        {
            return;
        }

        Current = state;
        StateTime = 0.f;
        AnimationFrame = 0.f;
    }
}
