#include "Mdk.Game/Stream/Flight.h"

#include <algorithm>
#include <cmath>

#include "Mdk.Game/Angles.h"

namespace Mdk::Game::Stream
{
    Flight::Flight(std::shared_ptr<StreamTube> tube, StreamTube::Kind kind, Game::Kurt::Difficulty difficulty, std::shared_ptr<WatcomRandom> random,
        int health, Clip kurt, Clip rescue, Clip other)
        : _tube(std::move(tube)), _kind(kind), _difficulty(difficulty), _random(std::move(random)),
          _kurtClip(kurt), _rescueClip(rescue), _otherClip(other)
    {
        Kurt = std::make_shared<Thing>();
        Kurt->T = KurtStart;
        Kurt->Yaw = NeutralYaw;
        Kurt->Speed = Speed;

        Health = health;
        LightCount = AddSpawns();
        // Gunter's yaw (90 at 0x433b50) never turns him: 0x435a34 takes the tube's frame (0x4366f4).
        Other = std::make_shared<Thing>();
        if (kind == StreamTube::Kind::Gunter)
        {
            Other->T = GuntaStart;
            Other->Speed = Speed;
        }
        else
        {
            Other->T = BonusStart;
            Other->Speed = BonusSpeed;
        }

        UpdateCamera();
    }

    void Flight::Update(float delta, Steering steering)
    {
        Events.clear();
        Sounds.clear();
        if (Ended)
        {
            return;
        }

        auto dt = MathF::Min(delta, MaxDelta);
        Time += dt;
        _tickFraction += dt * TicksPerSecond;
        auto ticks = ToInt(_tickFraction);
        _tickFraction -= ticks;
        Blink = (Blink + ticks) & BlinkMask;

        CheckEnd();
        if (!UpdateFade(dt))
        {
            return;
        }

        UpdateKurt(dt, steering);
        UpdateOthers(dt);
        UpdateCamera();
    }

    void Flight::CheckEnd()
    {
        if (Health <= 0 || (_tube->Tail <= RescueSegment && Health != LowHealth))
        {
            return;
        }

        if (_kind == StreamTube::Kind::Gunter)
        {
            _ending |= _tube->Head >= StreamTube::LastSegment;
            return;
        }

        if (Bones == nullptr)
        {
            StartRescue();
        }

        if (Bones->Frame > RescueEndFrame)
        {
            _ending = true;
        }
    }

    bool Flight::UpdateFade(float dt)
    {
        if (_ending)
        {
            _fade -= dt;
            if (_fade < 0.f)
            {
                Ended = true;
                Events.push_back(FlightEvent::Ended);
                return false;
            }
        }
        else if (_fade < 1.f)
        {
            _fade = MathF::Min(_fade + dt, 1.f);
        }

        auto f = Math::Clamp(_fade, 0.f, FadeDeath);
        auto dead = Health <= 0;
        if (dead && f > 1.f)
        {
            Fade = Stream::Fade{1.f, 1.f, FadeDeath - f};
        }
        else if (dead)
        {
            Fade = Stream::Fade{1.f, f, 1.f};
        }
        else if (_kind == StreamTube::Kind::Gunter)
        {
            Fade = Stream::Fade{1.f, MathF::Min(f, 1.f), 0.f};
        }
        else
        {
            Fade = Stream::Fade{MathF::Min(f, 1.f), 1.f, 0.f};
        }

        return true;
    }

    int Flight::AddSpawns()
    {
        auto spawns = _tube->TakeSpawns();
        for (const auto& spawn : spawns)
        {
            auto thing = std::make_shared<Thing>();
            thing->T = spawn.T;
            thing->X = spawn.X;
            thing->Z = spawn.Z;
            thing->Speed = spawn.Speed;
            _sprites.push_back(Sprite{spawn, thing});
        }

        return static_cast<int>(spawns.size());
    }

    bool Flight::Move(Thing& thing, float dt)
    {
        thing.T += thing.Speed * dt;
        thing.Frame += dt * TicksPerSecond;
        return _tube->Contains(thing.T);
    }

    Matrix4x4 Flight::KurtTransform() const
    {
        auto turn = TubeBasis::Rotation(Vector3::UnitZ(), Kurt->Yaw) * TubeBasis::Rotation(-Vector3::UnitY(), Pitch);
        return (_tube->Frame(Kurt->T, _screenUp) * turn).At(KurtPosition);
    }

    void Flight::UpdateOthers(float dt)
    {
        std::erase_if(_sprites, [&](const Sprite& s) { return !Move(*s.Thing, dt); });
        if (Other != nullptr && !Move(*Other, dt))
        {
            Other = nullptr;
        }

        if (Other != nullptr && _kind == StreamTube::Kind::Normal)
        {
            TakeBonus();
        }

        if (Bones != nullptr)
        {
            Bones->Frame += dt * TicksPerSecond * _rescueClip.Speed;
        }
    }

    void Flight::TakeBonus()
    {
        if (Vector3::Distance(KurtPosition, _tube->Place(Other->T, Other->X, Other->Z)) >= BonusReach)
        {
            return;
        }

        Health = BonusHealth;
        Sounds.push_back("APPLE");
        Events.push_back(FlightEvent::BonusTaken);
        Other = nullptr;
    }

    void Flight::UpdateKurt(float dt, Steering steering)
    {
        Kurt->Speed = MathF::Min(Kurt->Speed + SpeedRecovery * dt, Speed);
        Kurt->T += Kurt->Speed * dt;

        // A new segment each time Kurt passes one.
        if (Kurt->T - KurtStart > _tube->Tail + 1)
        {
            _tube->Advance();
            AddSpawns();
        }

        auto clip = KurtClip();
        Kurt->Frame += dt * TicksPerSecond * clip.Speed;
        if (Bones == nullptr && clip.FrameCount > 0)
        {
            Kurt->Frame = std::fmod(Kurt->Frame, static_cast<float>(clip.FrameCount));
        }

        auto frame = _tube->Frame(Kurt->T, _screenUp);
        DriftKurt(frame, dt);
        Steer(dt, Controlled() ? steering : Steering{});

        auto centre = _tube->Centre(Kurt->T);
        KurtPosition = centre + frame.X * Kurt->X + frame.Z * Kurt->Z;
        if (Controlled())
        {
            Collide(centre, frame);
        }
    }

    void Flight::DriftKurt(const TubeBasis& frame, float dt)
    {
        if (!Controlled())
        {
            Kurt->X = MoveToward(Kurt->X, 0.f, AxisReturn * dt);
            Kurt->Z = MoveToward(Kurt->Z, 0.f, AxisReturn * dt);
            return;
        }

        _direction = Vector3::Normalize(DirectionLag * _direction + (1.f - DirectionLag) * frame.Y);
        Kurt->X += Drift * dt * Vector3::Dot(_direction, frame.X);
        Kurt->Z += Drift * dt * Vector3::Dot(_direction, frame.Z);
    }

    void Flight::Steer(float dt, Steering steering)
    {
        if (steering.Right != 0)
        {
            Kurt->Yaw = Math::Clamp(Kurt->Yaw - steering.Right * TurnRate * dt, NeutralYaw - TurnLimit, NeutralYaw + TurnLimit);
        }
        else
        {
            Kurt->Yaw = MoveToward(Kurt->Yaw, NeutralYaw, TurnRate * dt);
        }

        if (steering.Up != 0)
        {
            Pitch = Math::Clamp(Pitch + steering.Up * TurnRate * dt, -TurnLimit, TurnLimit);
        }
        else
        {
            Pitch = MoveToward(Pitch, 0.f, TurnRate * dt);
        }

        Kurt->X += MathF::Cos(DegreesToRadians(Kurt->Yaw)) * SideSpeed * dt;
        Kurt->Z += MathF::Sin(DegreesToRadians(Pitch)) * SideSpeed * dt;
    }

    void Flight::Collide(Vector3 centre, const TubeBasis& frame)
    {
        auto k = _tube->WallHit(KurtPosition, Kurt->T, WallMargin);
        if (k < 0.f)
        {
            return;
        }

        // Facing the axis at full turn, e.g. at the right wall (x > 0) he turns left (yaw 135).
        auto r = Vector2(Kurt->X, Kurt->Z).Length();
        if (r > 0.f)
        {
            Kurt->Yaw = NeutralYaw + TurnLimit * Kurt->X / r;
            Pitch = -TurnLimit * Kurt->Z / r;
        }

        Kurt->X *= 1.f - k;
        Kurt->Z *= 1.f - k;
        KurtPosition = centre + frame.X * Kurt->X + frame.Z * Kurt->Z;
        Kurt->Speed = MathF::Max(Kurt->Speed * SpeedLoss, SpeedMin);

        Events.push_back(FlightEvent::WallHit);
        Sounds.push_back(HurtSounds[_random->Next() % static_cast<int>(HurtSounds.size())]);
        Hurt();
    }

    void Flight::Hurt()
    {
        if (Health <= 0)
        {
            return;
        }

        auto d = static_cast<int>(_difficulty);
        Health -= HitDamage[d] + HitExtra[d] * (_random->Next() & 1);
        if (Health >= LowHealth)
        {
            return;
        }

        if (_kind == StreamTube::Kind::Normal)
        {
            Health = LowHealth;
            return;
        }

        Health = 0;
        _ending = true;
        _fade = FadeDeath;
        Events.push_back(FlightEvent::KurtDied);
    }

    void Flight::StartRescue()
    {
        Bones = std::make_shared<Thing>();
        Sounds.push_back("RESCUE");
        Events.push_back(FlightEvent::BonesCame);
        Kurt->Frame = 0.f;
    }

    void Flight::UpdateCamera()
    {
        auto target = _tube->Centre(Kurt->T + CameraAhead);
        auto past = target + CameraPast * (KurtPosition - target);
        auto eye = CameraBlend * _tube->Centre(Kurt->T - CameraBack) + (1.f - CameraBlend) * past;
        auto forward = Vector3::Normalize(target - eye);
        auto right = Vector3::Normalize(Vector3::Cross(forward, _screenUp));
        _screenUp = Vector3::Cross(right, forward);
        Eye = eye;
        Forward = forward;
        Right = right;

        // The background scrolls with the view's turns (0x438bfc).
        auto down = -_screenUp;
        if (_viewRows.has_value() && _viewRows->size() == 3)
        {
            const auto& r0 = (*_viewRows)[0];
            const auto& u0 = (*_viewRows)[1];
            const auto& f0 = (*_viewRows)[2];
            auto y = PositiveMod(BackgroundOffset.Y + ScrollY * (down.Z * f0.Z - forward.Z * u0.Z), BackgroundHeight);
            auto x = PositiveMod(BackgroundOffset.X + ScrollX * (right.X * u0.X - down.X * r0.X), BackgroundWidth);
            BackgroundOffset = Vector2(x, y);
        }

        _viewRows = std::vector<Vector3>{right, down, forward};
    }
}
