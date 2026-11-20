#include "Mdk.Game/Scripts/AirStrike.h"

#include <algorithm>
#include <cmath>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Formats/Arena.h"
#include "Mdk.Game/Audio/SoundMixer.h"
#include "Mdk.Game/Kurt/Inventory.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Level/LevelData.h"
#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/GameStats.h"
#include "Mdk.Game/Scripts/Items.h"
#include "Mdk.Game/Scripts/ScriptMath.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"
#include "Mdk.Game/Scripts/StrikeScene.h"

namespace Mdk::Game::Scripts
{
    using Objects::MdkObject;

    bool AirStrike::IsActive() const
    {
        return _strike != nullptr && !_strike->Dead;
    }

    std::optional<Vector3> AirStrike::FindTarget(Vector3 eye, Vector3 forward)
    {
        auto hit = _runtime->Raycast(eye, eye + forward * Range);
        if (!hit)
        {
            return std::nullopt;
        }

        auto target = hit->Point;
        if (_runtime->Raycast(target + Vector3::UnitZ(), target + Vector3(0.0f, 0.0f, Sky)))
        {
            return std::nullopt;
        }

        return target;
    }

    bool AirStrike::Call(Vector3 eye, Vector3 forward)
    {
        if (IsActive())
        {
            return false;
        }

        auto target = FindTarget(eye, forward);
        if (!target || UsedUp)
        {
            _runtime->Mixer->Play("RASPBER", Audio::SoundMixer::Start::Restart);
            return false;
        }

        auto flight = GameStats::IndexOf(_runtime->Level->Number) > LastStrikeLevel ? Flight::Dive : Flight::Over;
        UsedUp |= flight == Flight::Dive;

        // Bones in his plane, full screen (without him when he dives).
        _runtime->PlayStrikeScene(StrikeScene::Kind::Bones, flight == Flight::Dive ? StrikeScene::Plane::Only : StrikeScene::Plane::WithPilot);
        Start(*target, flight);
        return true;
    }

    void AirStrike::Start(Vector3 target, Flight flight)
    {
        auto dive = flight == Flight::Dive;
        std::shared_ptr<Formats::Arena> arena;
        for (const auto& a : _runtime->Level->Arenas)
        {
            if (a->Name == _runtime->CurrentArena)
            {
                arena = a;
                break;
            }
        }

        auto top = target.Z;
        if (arena != nullptr && !arena->Vertices.empty())
        {
            top = arena->Vertices[0].Z;
            for (const auto& v : arena->Vertices)
            {
                top = MathF::Max(top, v.Z);
            }
        }

        auto cruise = top + Height;
        auto kurt = _runtime->KurtPosition;
        auto away = Vector2::Normalize(Vector2(kurt.X - target.X, kurt.Y - target.Y)) * Behind;
        if (std::isnan(away.X))
        {
            away = Vector2::Zero();
        }

        auto start = Vector3(kurt.X + away.X, kurt.Y + away.Y, cruise);
        auto over = target + Vector3(0.0f, 0.0f, OverTarget);
        auto end = over * 2.0f - start;
        if (!dive)
        {
            end.Z = cruise;
        }

        auto before = ClearPoint(start, over, target);
        auto after = dive ? (over + end) * 0.5f : ClearPoint(end, over, target);
        _curve = std::make_shared<BezierPath>(std::vector<Vector3>{start, before, over, after, end});
        _targetOffset = _curve->ClosestOffset(over);
        _diveOffset = dive ? (_curve->ClosestOffset(before) + _targetOffset) * 0.5f : std::numeric_limits<float>::infinity();
        _offset = 0.0f;
        _teeth = 0;
        _diving = false;
        auto controller = _runtime->GetArenaState(_runtime->CurrentArena)->Controller;
        _strike = _runtime->Spawn(*controller, "X_STRIKE", start, 0.0f, -1, 0, ScriptRuntime::Spawning::Plain);
        if (_strike == nullptr)
        {
            return;
        }

        _strike->Health = StrikeHealth;
        _strike->Flags = StrikeFlags;
    }

    Vector3 AirStrike::ClearPoint(Vector3 from, Vector3 over, Vector3 target)
    {
        for (int i = 1; i <= ClearSteps; i++)
        {
            auto point = Vector3::Lerp(from, over, ClearStep * i);
            if (!_runtime->Raycast(target + Vector3(0.0f, 0.0f, TargetEye), point))
            {
                return point;
            }
        }

        return Vector3::Lerp(from, over, ClearFallback);
    }

    void AirStrike::Update(float ticks)
    {
        if (!IsActive() || _curve == nullptr)
        {
            _strike = nullptr;
            return;
        }

        auto& strike = *_strike;
        if (_diving)
        {
            Crash(strike);
            return;
        }

        auto previous = strike.Position;
        _offset += Speed / Kurt::Kurt::Ticks * ticks;
        if (_offset >= _curve->Length())
        {
            _runtime->Remove(strike);
            return;
        }

        strike.Position = _curve->Sample(_offset);
        auto motion = strike.Position - previous;
        if (motion != Vector3::Zero())
        {
            strike.Yaw = std::fmod(ScriptMath::ToDegrees(MathF::Atan2(motion.Y, motion.X)) + 360.0f, 360.0f);
        }

        if (_offset >= _diveOffset)
        {
            // It leaves the path with its velocity, falling, until it hits something.
            _diving = true;
            strike.Velocity = motion * Kurt::Kurt::Ticks / ticks;
            strike.Flags |= MdkObject::FlagGravity | MdkObject::FlagCollides;
            return;
        }

        auto tooth = ToInt(MathF::Round((_offset - _targetOffset) / ToothSpacing + ToothBias));
        while (_teeth < tooth && _teeth < Teeth && std::isinf(_diveOffset) && _diveOffset > 0.0f)
        {
            _teeth++;
            DropTooth(strike);
        }
    }

    void AirStrike::Crash(MdkObject& strike)
    {
        if ((strike.ContactFlags & (MdkObject::ContactCollided | MdkObject::ContactFloor)) == 0)
        {
            return;
        }

        auto center = strike.Position;
        _runtime->Items->Blast(center, DiveDamage, DiveRadius, TargetsAll, HitDive, &strike);
        _runtime->Items->Blast(center, DiveKurtDamage, DiveRadius, TargetKurt, HitDive, &strike);
        _runtime->SpawnExplosion(strike.Arena, center, ExplosionScale);
        _runtime->PlaySoundAt(std::string(ScriptRuntime::ExplodeSound), center);
        _runtime->Remove(strike);
    }

    void AirStrike::DropTooth(MdkObject& strike)
    {
        auto controller = _runtime->GetArenaState(_runtime->CurrentArena)->Controller;
        auto tooth = _runtime->Spawn(*controller, "X_TOOTH", strike.Position, strike.Yaw, -1, 0, ScriptRuntime::Spawning::Plain);
        if (tooth == nullptr)
        {
            return;
        }

        tooth->Flags |= ThrownFlags;
        tooth->ThrownKind = static_cast<int>(Kurt::Inventory::Item::Grenade);
        tooth->ItemTicks = ToothTicks;
        tooth->Friction = 0.0f;
        tooth->Velocity = Vector3(0.0f, 0.0f, ToothFall);
        tooth->LoopSound = _runtime->Mixer->PlayOn("DROP", [tooth] { return tooth->Position; });
    }

    BezierPath::BezierPath(const std::vector<Vector3>& points)
    {
        _points.push_back(points[0]);
        _lengths.push_back(0.0f);
        int count = static_cast<int>(points.size());
        for (int i = 0; i + 1 < count; i++)
        {
            auto a = points[i];
            auto b = points[i + 1];
            auto outHandle = (b - points[Math::Max(i - 1, 0)]) * HandleFactor;
            auto inHandle = -(points[Math::Min(i + 2, count - 1)] - a) * HandleFactor;
            for (int s = 1; s <= SamplesPerSegment; s++)
            {
                auto point = Cubic(a, a + outHandle, b + inHandle, b, static_cast<float>(s) / SamplesPerSegment);
                _lengths.push_back(_lengths.back() + Vector3::Distance(_points.back(), point));
                _points.push_back(point);
            }
        }
    }

    Vector3 BezierPath::Cubic(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
    {
        auto u = 1.0f - t;
        return u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3;
    }

    Vector3 BezierPath::Sample(float offset) const
    {
        // List.BinarySearch: the index when found, else the complement of the next larger one.
        auto key = MathF::Clamp(offset, 0.0f, Length());
        auto it = std::lower_bound(_lengths.begin(), _lengths.end(), key);
        int position = static_cast<int>(it - _lengths.begin());
        if (it != _lengths.end() && *it == key)
        {
            return _points[position];
        }

        int count = static_cast<int>(_points.size());
        auto next = Math::Min(position, count - 1);
        auto previous = Math::Max(next - 1, 0);
        auto span = _lengths[next] - _lengths[previous];
        auto t = span > 0.0f ? (offset - _lengths[previous]) / span : 0.0f;
        return Vector3::Lerp(_points[previous], _points[next], t);
    }

    float BezierPath::ClosestOffset(Vector3 point) const
    {
        auto best = 0.0f;
        auto bestDistance = std::numeric_limits<float>::max();
        for (size_t i = 0; i + 1 < _points.size(); i++)
        {
            auto a = _points[i];
            auto segment = _points[i + 1] - a;
            auto squared = segment.LengthSquared();
            auto t = squared > 0.0f ? MathF::Clamp(Vector3::Dot(point - a, segment) / squared, 0.0f, 1.0f) : 0.0f;
            auto distance = Vector3::DistanceSquared(point, a + segment * t);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = _lengths[i] + (_lengths[i + 1] - _lengths[i]) * t;
            }
        }

        return best;
    }
}
