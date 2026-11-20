#include "Mdk.Game/Fall/FallMissile.h"

namespace Mdk::Game::Fall
{
    FallMissile::FallMissile(float spread, Mdk::Random& random)
    {
        auto a = random.NextSingle() * MathF::Tau;
        Velocity = Vector3(MathF::Sin(a), MathF::Cos(a), 1.f) * Speed;

        // x first: C++ leaves the order of constructor arguments open.
        auto x = Spread(random, spread);
        auto y = Spread(random, spread);
        _offset = Vector2(x, y);
    }

    float FallMissile::Spread(Mdk::Random& random, float spread)
    {
        return (random.NextSingle() * 2.f - 1.f) * spread;
    }

    FallMissile::Event FallMissile::Update(float dt, int ticks, Vector3 kurt)
    {
        Position += Velocity * dt;
        if (Position.Z < BoostHeight * kurt.Z)
        {
            Position.Z += Boost * Velocity.Z * dt;
        }

        for (auto i = 0; i < ticks; i++)
        {
            _trail.push_back(Position);
            _headings.push_back(Velocity);
        }

        if (static_cast<int>(_trail.size()) > TrailPoints)
        {
            _trail.erase(_trail.begin(), _trail.end() - TrailPoints);
            _headings.erase(_headings.begin(), _headings.end() - TrailPoints);
        }

        switch (_phase)
        {
            case Phase::Free:
                _ticks -= ticks;
                if (_ticks <= 0.f)
                {
                    _phase = Phase::Homing;
                }

                return Event::None;
            case Phase::Homing:
                return Home(kurt, dt);
            default:
                _ticks += ticks;
                return _ticks > GoneTicks ? Event::Gone : Event::None;
        }
    }

    FallMissile::Event FallMissile::Home(Vector3 kurt, float dt)
    {
        auto dz = kurt.Z - Position.Z;
        if (dz <= -PassHeight)
        {
            _phase = Phase::Passed;
            _ticks = 0.f;
            return Event::Passed;
        }

        auto aim = kurt + Vector3(_offset.X, _offset.Y, -MathF::Min(dz / AimDepth, AimMax) * AimSpeed);
        auto towards = aim - Position;
        if (towards != Vector3::Zero())
        {
            towards = Vector3::Normalize(towards);
        }

        // The original blends once per 1/30 s tick: blending once per frame made the steering far
        // stiffer at 60-120 fps, so a missile that missed locked on and hovered by Kurt.
        auto keep = MathF::Pow(1.f - Homing, dt * TicksPerSecond);
        Velocity = keep * Velocity + (1.f - keep) * Speed * towards;
        return Event::None;
    }
}
