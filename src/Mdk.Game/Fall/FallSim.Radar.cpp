#include "Mdk.Game/Fall/FallSim.h"

// The radars (0x4130ac create, 0x412af8 update): one at a time, its beam rises from the
// ground to just below Kurt, wanders between targets (Kurt, a pickup, random points) and, when its
// spot comes within 15 units of him, he's seen: missiles are queued and it retracts.
//   wait ──► Extend (3000 u/s up) ──► Track (wanders) ──seen──► Retract (1500 u/s down) ──► wait
namespace Mdk::Game::Fall
{
    void FallSim::UpdateRadar(float dt, int ticks)
    {
        if (!RadarActive && !StartRadar(ticks))
        {
            return;
        }

        switch (Radar)
        {
            case RadarState::Extend:
                Extend(dt);
                break;
            case RadarState::Track:
                Track(dt, ticks);
                break;
            default:
                Retract(dt);
                break;
        }
    }

    bool FallSim::StartRadar(int ticks)
    {
        if (Time > SteerTime)
        {
            return false;
        }

        _radarWait -= ticks;
        if (_radarWait > 0)
        {
            return false;
        }

        RadarActive = true;
        Radar = RadarState::Extend;
        _radarTarget = RandomPoint();
        _radarHeight = KurtPosition.Z - RadarBelow;
        RadarSpot = Vector3::Zero();
        _radarVelocity = Vector2::Zero();
        if (Sound)
        {
            Sound("R_START");
        }

        return true;
    }

    void FallSim::Extend(float dt)
    {
        _radarHeight = KurtPosition.Z - RadarBelow;
        auto z = MathF::Min(RadarSpot.Z + RadarExtendSpeed * dt, _radarHeight);
        auto f = z / _radarHeight;
        RadarSpot = Vector3(_radarTarget * f, z);
        RadarSpotRadius = RadarRadius * f;
        if (z < _radarHeight)
        {
            return;
        }

        Radar = RadarState::Track;
        _radarTarget = RandomPoint();
        _radarTicks = 0;
    }

    void FallSim::Track(float dt, int ticks)
    {
        _radarHeight = KurtPosition.Z - RadarBelow;
        RadarSpotRadius = RadarRadius;
        auto spot = Vector2(RadarSpot.X, RadarSpot.Y);
        auto towards = _radarTarget - spot;
        if (towards != Vector2::Zero())
        {
            towards = Vector2::Normalize(towards);
        }

        for (auto i = 0; i < ticks; i++)
        {
            _radarVelocity = RadarInertia * _radarVelocity + (1.f - RadarInertia) * BeamSpeed * towards;
        }

        spot += _radarVelocity * dt;
        RadarSpot = Vector3(spot, _radarHeight);
        _radarTicks += ticks;
        auto near = Vector2::Abs(_radarTarget - spot);
        if (_radarTicks >= RadarTargetTicks || (near.X < RadarNear && near.Y < RadarNear))
        {
            if (Sound)
            {
                Sound("R_MOVE");
            }

            _radarTarget = ChooseTarget();
            _radarTicks = 0;
        }

        auto kurt = Vector2(KurtPosition.X, KurtPosition.Y);
        if (Vector2::Distance(kurt, spot) < RadarSee && !Dying)
        {
            Detected();
        }
    }

    void FallSim::Retract(float dt)
    {
        auto z = RadarSpot.Z - RadarRetractSpeed * dt;
        if (z <= 0.f)
        {
            RadarActive = false;
            _radarWait = RadarGap + _random->Next(RadarGapRandom);
            return;
        }

        RadarSpotRadius = RadarRadius * z / _radarHeight;
        RadarSpot.Z = z;
    }

    void FallSim::Detected()
    {
        if (Sound)
        {
            Sound("K_SEEN");
        }

        Radar = RadarState::Retract;
        _missileQueue += MissilesPerDetection + _random->Next(2);
        _missileWait = Math::Min(_missileWait, 1);
        _rate = FlashRate;
        _target = MathF::Max(_target - DetectDim, DetectFloor);
    }

    Vector2 FallSim::ChooseTarget()
    {
        std::vector<Vector2> choices = { Vector2(KurtPosition.X, KurtPosition.Y) };
        for (const auto& pickup : _pickups)
        {
            if (static_cast<int>(choices.size()) < RadarChoices)
            {
                choices.push_back(Vector2(pickup->Position.X, pickup->Position.Y));
            }
        }

        for (auto i = 0; i < RadarRandomChoices; i++)
        {
            choices.push_back(RandomPoint());
        }

        return choices[_random->Next(static_cast<int>(choices.size()))];
    }
}
