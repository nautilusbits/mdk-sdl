#include "Mdk.Game/Scripts/SniperRounds.h"

#include <cmath>
#include <limits>
#include <string>

#include "Mdk.Core/DotNet.h"
#include "Mdk.Game/FrameBlend.h"
#include "Mdk.Game/Kurt/Kurt.h"
#include "Mdk.Game/Kurt/Scope.h"
#include "Mdk.Game/Scripts/GameStats.h"
#include "Mdk.Game/Scripts/Items.h"
#include "Mdk.Game/Scripts/ObjectMotion.h"
#include "Mdk.Game/Scripts/ScriptMath.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"

namespace Mdk::Game::Scripts
{
    using Objects::MdkObject;

    SniperRounds::SniperRounds(ScriptRuntime& runtime)
        : _runtime(&runtime), _rounds{std::make_shared<Round>(), std::make_shared<Round>(), std::make_shared<Round>()}
    {
    }

    const std::vector<std::shared_ptr<MdkObject>>& SniperRounds::Visuals()
    {
        _visuals.clear();
        for (const auto& round : _rounds)
        {
            if (round->State == State::Flying && round->Visual != nullptr)
            {
                _visuals.push_back(round->Visual);
            }
        }

        return _visuals;
    }

    bool SniperRounds::Fire(int type, Vector3 eye, float yaw, float pitch, std::shared_ptr<MdkObject> target)
    {
        std::shared_ptr<Round> found;
        for (const auto& r : _rounds)
        {
            if (r->State == State::Free)
            {
                found = r;
                break;
            }
        }

        if (found == nullptr)
        {
            return false;
        }

        auto& round = *found;
        auto kind = static_cast<Type>(type);
        auto homing = kind == Type::Homing || kind == Type::HomingGrenade;
        round.State = State::Flying;
        round.Type = kind;
        round.Position = eye;
        round.Yaw = yaw;
        round.Pitch = pitch;
        round.Roll = 0.0f;
        round.Life = static_cast<float>(Life[type]);
        round.Linger = 0.0f;
        round.Speed = Speed[type];
        round.Vertical = 0.0f;
        round.Path = 0;
        round.YawRate = 0.0f;
        round.Target = homing ? target : nullptr;
        round.TargetPart = round.Target != nullptr ? HeadOf(*round.Target) : -1;
        round.CameraDistance = 0.0f;
        round.CameraPosition = eye;
        round.Watch = 0.0f;
        if (kind == Type::Mortar)
        {
            round.Speed = Speed[type] * MathF::Cos(ScriptMath::ToRadians(pitch));
            round.Vertical = -Speed[type] * MathF::Sin(ScriptMath::ToRadians(pitch));
        }

        round.Visual = MakeVisual(kind);
        round.Ticked.reset();
        return true;
    }

    SniperRounds::RoundCamera SniperRounds::Camera(int index, float alpha) const
    {
        const auto& round = *_rounds[index];
        if (round.State == State::Free)
        {
            return RoundCamera{Shot::Empty, {}, {}, 0.0f};
        }

        if (round.State == State::Flying || round.Watch > 0.0f)
        {
            if (!round.Ticked.has_value() || alpha >= 1.0f)
            {
                return RoundCamera{Shot::Watching, round.CameraPosition, Direction(round), 0.0f};
            }

            const auto& from = *round.Ticked;
            auto forward = Game::Kurt::Scope::Direction(FrameBlend::LerpAngle(from.Yaw, round.Yaw, alpha),
                FrameBlend::LerpAngle(from.Pitch, round.Pitch, alpha));
            return RoundCamera{Shot::Watching, Vector3::Lerp(from.Camera, round.CameraPosition, alpha), forward, 0.0f};
        }

        auto shot = Shot::Kill;
        if (round.State == State::Hit)
        {
            shot = Shot::Hit;
        }
        else if (round.State == State::Dead)
        {
            shot = Shot::Miss;
        }

        return RoundCamera{shot, {}, {}, MissLinger - round.Linger};
    }

    void SniperRounds::GuideLastMortar(int path)
    {
        if (_lastMortar == nullptr || _lastMortar->State != State::Flying || _lastMortar->Type != Type::Mortar || _lastMortar->Path != 0)
        {
            return;
        }

        _lastMortar->Path = path;
        _lastMortar->PathTime = 0.0f;
    }

    Matrix4x4 SniperRounds::TransformOf(const MdkObject& visual, float alpha) const
    {
        for (const auto& round : _rounds)
        {
            if (round->Visual.get() != &visual || !round->Ticked.has_value() || alpha >= 1.0f)
            {
                continue;
            }

            const auto& from = *round->Ticked;
            auto basis = VisualBasis(FrameBlend::LerpAngle(from.Roll, round->Roll, alpha), FrameBlend::LerpAngle(from.Pitch, round->Pitch, alpha),
                FrameBlend::LerpAngle(from.Yaw, round->Yaw, alpha));
            auto at = Vector3::Lerp(from.Position, round->Position, alpha) + Vector3::UnitZ() * visual.Lift();
            return Matrix4x4::CreateScale(visual.Scale) * basis * Matrix4x4::CreateTranslation(at);
        }

        return visual.Transform();
    }

    int SniperRounds::HeadOf(const MdkObject& obj)
    {
        if (obj.Model == nullptr)
        {
            return -1;
        }

        const auto& parts = obj.Model->PartList;
        for (auto i = 0; i < static_cast<int>(parts.size()); i++)
        {
            if (Str::ContainsIgnoreCase(parts[i]->Name, HeadPart))
            {
                return i;
            }
        }

        return -1;
    }

    std::shared_ptr<MdkObject> SniperRounds::MakeVisual(Type type)
    {
        auto name = std::string(Models[static_cast<int>(type)]);
        auto model = _runtime->FindModel(_runtime->CurrentArena, name);
        if (model == nullptr)
        {
            return nullptr;
        }

        // The orientation is set as a whole (see VisualBasis).
        auto visual = std::make_shared<MdkObject>();
        visual->TypeName = name;
        visual->Model = model;
        visual->Arena = _runtime->CurrentArena;
        visual->Flags = MdkObject::FlagRolling;
        return visual;
    }

    void SniperRounds::Update(float ticks)
    {
        auto dt = ticks / Game::Kurt::Kurt::Ticks;
        for (const auto& item : _rounds)
        {
            auto& round = *item;
            if (round.State == State::Free)
            {
                continue;
            }

            round.Ticked = TickedState{round.Position, round.CameraPosition, round.Yaw, round.Pitch, round.Roll};
            if (round.State != State::Flying)
            {
                round.Linger -= ticks;
                round.Watch -= ticks;
                if (round.Linger <= 0.0f)
                {
                    round.State = State::Free;
                }

                continue;
            }

            Fly(round, ticks, dt);
        }
    }

    void SniperRounds::Fly(Round& round, float ticks, float dt)
    {
        round.Life -= ticks;
        if (round.Type != Type::Mortar)
        {
            round.Roll = std::fmod(round.Roll + Spin * dt, 360.0f);
        }

        if (round.Path != 0)
        {
            FollowPath(round, ticks);
        }
        else if (round.Type == Type::Mortar)
        {
            MoveMortar(round, dt);
        }
        else
        {
            if (round.Target != nullptr)
            {
                Steer(round, dt);
            }

            MoveStraight(round, dt);
        }

        if (round.State == State::Flying && round.Life <= 0.0f)
        {
            if (round.Type >= Type::Grenade)
            {
                Explode(round, round.Type == Type::Mortar ? MortarBlastRadius : BlastRadius, nullptr);
            }
            else
            {
                End(round, State::Dead, MissLinger);
            }
        }

        if (round.State != State::Flying)
        {
            return;
        }

        round.CameraDistance = MathF::Min(round.CameraDistance + CameraBackSpeed * dt, CameraBack);
        round.CameraPosition = round.Position - Direction(round) * round.CameraDistance;
        if (round.Visual != nullptr)
        {
            round.Visual->Position = round.Position;
            round.Visual->RollingBasis = VisualBasis(round.Roll, round.Pitch, round.Yaw);
        }
    }

    Matrix4x4 SniperRounds::VisualBasis(float roll, float pitch, float yaw)
    {
        return Matrix4x4::CreateRotationZ(ScriptMath::ToRadians(roll))
            * Matrix4x4::CreateRotationY(MathF::PI / 2.0f + ScriptMath::ToRadians(pitch))
            * Matrix4x4::CreateRotationZ(ScriptMath::ToRadians(yaw));
    }

    Vector3 SniperRounds::Direction(const Round& round)
    {
        return Game::Kurt::Scope::Direction(round.Yaw, round.Pitch);
    }

    void SniperRounds::MoveStraight(Round& round, float dt)
    {
        auto& runtime = *_runtime;
        auto start = round.Position;
        auto end = start + Direction(round) * round.Speed * dt;
        auto hit = TestObjects(start, end);
        auto wall = runtime.Raycast(start, end);
        if (hit.has_value() && (!wall.has_value() || Vector3::Distance(start, hit->Point) < Vector3::Distance(start, wall->Point)))
        {
            round.Position = hit->Point;
            HitObject(round, *hit->Object, hit->Part, hit->Point);
            return;
        }

        if (wall.has_value())
        {
            const auto& stop = *wall;

            // Taken back a unit off the wall; a reacting group gets one orange spark, a wall 3 grey ones.
            round.Position = stop.Point + Facing(stop.Normal, end - start);
            auto reacted = (runtime.HitGroupAt(stop, round.Type < Type::Grenade ? Damage : 0, ScriptRuntime::HitShot, static_cast<int>(round.Type)) & 1) != 0;
            runtime.SparkAt(round.Position, reacted ? 1 : 3, "", reacted ? ScriptRuntime::Spark::Group : ScriptRuntime::Spark::Hard);
            if (round.Type >= Type::Grenade)
            {
                Explode(round, BlastRadius, nullptr);
                return;
            }

            End(round, State::Dead, MissLinger);
            return;
        }

        round.Position = end;
        if (round.Position.Z < runtime.GetArenaFloor(runtime.CurrentArena) - FallOutDepth)
        {
            End(round, State::Dead, MissLinger);
        }
    }

    void SniperRounds::Steer(Round& round, float dt)
    {
        auto& runtime = *_runtime;
        auto& target = *round.Target;
        if (target.Dead)
        {
            round.Target = nullptr;
            return;
        }

        if (round.Life > HomingStart)
        {
            return;
        }

        auto bounds = runtime.GetWorldBounds(target);
        const auto& parts = target.PartBounds();
        if (round.TargetPart >= 0 && round.TargetPart < static_cast<int>(parts.size()) && (target.HiddenParts & (1 << (round.TargetPart & 31))) == 0
            && parts[round.TargetPart].has_value())
        {
            bounds = runtime.GetWorldBounds(target, *parts[round.TargetPart]);
        }

        auto aim = ScriptMath::Center(bounds) - round.Position;
        auto yawError = WrapAngle(ScriptMath::ToDegrees(MathF::Atan2(aim.Y, aim.X)) - round.Yaw);
        if (round.YawRate * yawError < 0.0f)
        {
            round.YawRate = 0.0f;
        }

        round.YawRate = Math::Clamp(round.YawRate + MathF::Sign(yawError) * HomingYawAcceleration * dt, -HomingYawRate, HomingYawRate);
        auto yawStep = round.YawRate * dt;
        if (MathF::Abs(yawStep) > MathF::Abs(yawError))
        {
            yawStep = yawError;
        }

        round.Yaw = Wrap360(round.Yaw + yawStep);
        auto pitchGoal = -ScriptMath::ToDegrees(MathF::Atan2(aim.Z, Vector2(aim.X, aim.Y).Length()));
        auto pitchError = WrapAngle(pitchGoal - round.Pitch);
        round.Pitch += Math::Clamp(pitchError, -HomingPitchRate * dt, HomingPitchRate * dt);
        auto goal = MathF::Abs(yawError) + MathF::Abs(pitchError) <= HomingAim ? HomingFast : HomingSlow;
        auto rate = goal > round.Speed ? HomingSpeedUp : HomingSlowDown;
        round.Speed = MathF::Abs(goal - round.Speed) <= rate * dt ? goal : round.Speed + MathF::Sign(goal - round.Speed) * rate * dt;
    }

    void SniperRounds::MoveMortar(Round& round, float dt)
    {
        auto& runtime = *_runtime;
        auto fraction = round.Speed > 0.0f ? round.Speed / (MathF::Abs(round.Vertical) + round.Speed) : 0.0f;
        round.Speed = MathF::Max(round.Speed - fraction * MortarDrag * dt, 0.0f);
        if (round.Vertical > 0.0f)
        {
            round.Vertical = MathF::Max(round.Vertical - (1.0f - fraction) * MortarDrag * dt, 0.0f);
        }

        round.Vertical = MathF::Max(round.Vertical - MortarGravity * dt, -MortarMaxFall);
        auto yaw = ScriptMath::ToRadians(round.Yaw);
        auto velocity = Vector3(MathF::Cos(yaw) * round.Speed, MathF::Sin(yaw) * round.Speed, round.Vertical);
        auto start = round.Position;
        auto end = start + velocity * dt;
        auto hit = TestObjects(start, end);
        if (hit.has_value())
        {
            round.Position = hit->Point;
            Explode(round, MortarBlastRadius, hit->Object.get());
            return;
        }

        // A box swept as the original's, not a ray: faces seen from their back don't stop it.
        auto wall = runtime.Sweep(start, end, MortarBox);
        if (!wall.has_value())
        {
            round.Position = end;
            return;
        }

        auto normal = wall->Normal;
        round.Position = wall->Point;
        for (const auto& r : _rounds)
        {
            if (r.get() == &round)
            {
                _lastMortar = r;
            }
        }

        runtime.HitGroupAt(*wall, 0, ScriptRuntime::HitShot, static_cast<int>(Type::Mortar));
        if (round.Path != 0)
        {
            return;
        }

        velocity -= normal * Vector3::Dot(velocity, normal) * MortarBounce;
        if (velocity.Z > 0.0f && velocity.Z < MortarSettleLift)
        {
            velocity.Z = 0.0f;
        }

        round.Speed = Vector2(velocity.X, velocity.Y).Length();
        round.Vertical = velocity.Z;
        if (round.Speed > 0.0f)
        {
            round.Yaw = ScriptMath::ToDegrees(MathF::Atan2(velocity.Y, velocity.X));
        }

        if (round.Life > MortarSettleTicks && round.Speed < MortarStillSpeed && round.Vertical < MortarStillRise)
        {
            round.Life = MortarSettleTicks;
        }
    }

    void SniperRounds::FollowPath(Round& round, float ticks)
    {
        auto& motion = *_runtime->Motion;
        auto last = motion.PathKeyFrame(round.Path, motion.PathKeyCount(round.Path) - 1) - 1.0f;
        round.PathTime += ticks;
        if (round.PathTime < last)
        {
            round.Life = GuidedLife;
        }
        else
        {
            round.PathTime = last;
            round.Life = 0.0f;
        }

        round.Position = motion.PathPosition(round.Path, round.PathTime);
    }

    std::optional<SniperRounds::ObjectHit> SniperRounds::TestObjects(Vector3 start, Vector3 end)
    {
        auto& runtime = *_runtime;
        std::optional<ObjectHit> best;
        auto bestDistance = std::numeric_limits<float>::max();
        for (const auto& obj : runtime.Objects)
        {
            if (obj->Dead || obj->Arena != runtime.CurrentArena || obj->Health == 0 || (obj->Flags & Untouchable) != 0)
            {
                continue;
            }

            auto entry = ScriptMath::SegmentEntry(runtime.GetWorldBounds(*obj), start, end);
            if (!entry.has_value())
            {
                continue;
            }

            auto partHit = obj->Model != nullptr ? HitPart(*obj, start, end) : PartHit{entry, -1};
            if (!partHit.Point.has_value())
            {
                continue;
            }

            auto p = *partHit.Point;
            auto distance = Vector3::Distance(start, p);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = ObjectHit{obj, p, partHit.Part};
            }
        }

        return best;
    }

    SniperRounds::PartHit SniperRounds::HitPart(MdkObject& obj, Vector3 start, Vector3 end)
    {
        Matrix4x4 toModel;
        if (!Matrix4x4::Invert(obj.Transform(), toModel))
        {
            return PartHit{std::nullopt, -1};
        }

        // An affine map keeps the fraction along the segment.
        auto from = Vector3::Transform(start, toModel);
        auto to = Vector3::Transform(end, toModel);
        auto nearest = 1.0f;
        auto part = -1;
        const auto& pose = obj.PoseParts();
        for (auto i = 0; i < static_cast<int>(pose.size()); i++)
        {
            if ((obj.HiddenParts & (1 << (i & 31))) != 0)
            {
                continue;
            }

            const auto& vertices = pose[i];
            const auto& indices = obj.Model->PartList[i]->TriangleIndices;
            for (auto t = 0; t < static_cast<int>(indices.size()); t += 3)
            {
                auto hit = SegmentFace(from, to, vertices[indices[t]], vertices[indices[t + 1]], vertices[indices[t + 2]]);
                if (hit < nearest)
                {
                    nearest = hit;
                    part = i;
                }
            }
        }

        return part < 0 ? PartHit{std::nullopt, -1} : PartHit{Vector3::Lerp(start, end, nearest), part};
    }

    float SniperRounds::SegmentFace(Vector3 from, Vector3 to, Vector3 a, Vector3 b, Vector3 c)
    {
        constexpr float Parallel = 1e-9f;
        constexpr float Miss = std::numeric_limits<float>::infinity();
        auto along = to - from;
        auto ab = b - a;
        auto ac = c - a;
        auto p = Vector3::Cross(along, ac);
        auto det = Vector3::Dot(ab, p);
        if (MathF::Abs(det) < Parallel)
        {
            return Miss;
        }

        // Barycentric u, v and the fraction t (Moller-Trumbore).
        auto inverse = 1.0f / det;
        auto s = from - a;
        auto u = Vector3::Dot(s, p) * inverse;
        if (u < 0.0f || u > 1.0f)
        {
            return Miss;
        }

        auto q = Vector3::Cross(s, ab);
        auto v = Vector3::Dot(along, q) * inverse;
        if (v < 0.0f || u + v > 1.0f)
        {
            return Miss;
        }

        auto t = Vector3::Dot(ac, q) * inverse;
        return t >= 0.0f && t <= 1.0f ? t : Miss;
    }

    void SniperRounds::HitObject(Round& round, MdkObject& obj, int part, Vector3 point)
    {
        constexpr int WholeObject = -2;
        auto& runtime = *_runtime;
        obj.HitEvent = part >= 0 ? part + 1 : WholeObject;
        obj.HitType = static_cast<int>(round.Type);
        obj.HitDirection = round.Yaw;
        obj.ShotPart = part + 1;
        obj.ShotPoint = point;
        runtime.Stats->SniperHits++;
        if (round.Type >= Type::Grenade)
        {
            Explode(round, BlastRadius, &obj);
            return;
        }

        if (obj.Health < Unhurt)
        {
            obj.Health -= runtime.KurtDamage(obj, Damage);
        }

        if (obj.Health > 0)
        {
            runtime.SparkAt(point, 3, obj.Labels[1], obj.Indestructible ? ScriptRuntime::Spark::Hard : ScriptRuntime::Spark::Flesh);
            End(round, State::Hit, HitLinger);
            return;
        }

        obj.Health = 0;
        runtime.Stats->CountEnemy(obj.TypeName, GameStats::Kill::Killed);
        runtime.Kill(obj, round.Yaw + 180.0f);
        End(round, State::Killed, HitLinger);
    }

    void SniperRounds::Explode(Round& round, float radius, MdkObject* source)
    {
        auto& runtime = *_runtime;
        runtime.Items->Blast(round.Position, ExplosionDamage, radius, TargetsAll, HitExplosion, source);
        runtime.Items->Blast(round.Position, ExplosionKurtDamage, radius, TargetKurt, HitExplosion, source);
        runtime.SpawnExplosion(runtime.CurrentArena, round.Position, ExplosionScale);
        runtime.PlaySoundAt(std::string(ScriptRuntime::ExplodeSound), round.Position);
        End(round, State::Exploded, MissLinger);
    }

    void SniperRounds::End(Round& round, State state, float linger)
    {
        round.State = state;
        round.Linger = linger;
        round.Watch = state == State::Killed || state == State::Hit ? WatchTicks : 0.0f;
    }

    float SniperRounds::WrapAngle(float degrees)
    {
        return std::fmod(std::fmod(degrees + 180.0f, 360.0f) + 360.0f, 360.0f) - 180.0f;
    }

    float SniperRounds::Wrap360(float degrees)
    {
        return std::fmod(std::fmod(degrees, 360.0f) + 360.0f, 360.0f);
    }
}
