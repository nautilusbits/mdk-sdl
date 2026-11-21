#include "game/scripts/ObjectMotion.h"

#include "core/DotNet.h"
#include "formats/BinReader.h"
#include "formats/Cmi.h"
#include "game/level/LevelData.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/Fans.h"
#include "game/scripts/Items.h"
#include "game/scripts/ObjectBehaviors.h"
#include "game/scripts/ScriptMath.h"

namespace Mdk::Game::Scripts
{
    using namespace ScriptMath;
    using Formats::Bin;
    using Objects::MdkObject;

    void ObjectMotion::Update(MdkObject& obj)
    {
        if (obj.EffectFrames > 0)
        {
            // Effects only play their texture animation, one frame per tick (object_anim_update).
            obj.EffectTime += Ticks;
            if (obj.EffectTime >= obj.EffectFrames)
            {
                _runtime->Remove(obj);
            }
            else
            {
                obj.TextureFrame = ToInt(obj.EffectTime);
            }

            return;
        }

        if (obj.Path != 0)
        {
            if ((obj.Flags & MdkObject::FlagPathSpeedByKurt) != 0)
            {
                UpdatePathSpeed(obj);
            }

            UpdatePath(obj);
        }

        UpdateCommand(obj);
        if (obj.Dead)
        {
            return;
        }

        if ((obj.Flags & MdkObject::FlagSwinging) != 0)
        {
            Swing(obj);
        }

        if ((obj.Flags & MdkObject::FlagGravity) != 0)
        {
            obj.Velocity.Z -= obj.Gravity * Dt;
            Updraft(obj);
            obj.Velocity.Z = MathF::Max(obj.Velocity.Z, TerminalVelocity);
        }

        ApplyVelocity(obj);
        if (obj.Dead)
        {
            return;
        }

        if ((obj.Flags & MdkObject::FlagChangesArena) != 0)
        {
            _runtime->FollowArenas(obj);
        }

        // Kurt's items and effects (0x1000), or else pickups (0x200000).
        if (obj.ThrownKind > 0)
        {
            _runtime->Items->UpdateThrown(obj);
            if (obj.Dead)
            {
                return;
            }
        }
        else if ((obj.Flags & MdkObject::FlagPickup) != 0)
        {
            _runtime->Behaviors->UpdatePickup(obj);
        }
        else if ((obj.Flags & MdkObject::FlagCow) != 0)
        {
            _runtime->Behaviors->UpdateCow(obj);
            if (obj.Dead)
            {
                return;
            }
        }

        auto time = obj.AnimationTime;
        obj.AdvanceAnimation(Dt);
        if (!obj.FrameSound.empty() && obj.Animation != nullptr && time < obj.FrameSoundFrame
            && obj.FrameSoundFrame <= time + Dt * obj.AnimationRate * obj.Animation->Speed)
        {
            _runtime->PlaySound(obj, obj.FrameSound, 0, Operand{});
            obj.FrameSound = "";
        }

        if ((obj.Flags & MdkObject::FlagRolling) != 0)
        {
            Roll(obj);
        }

        Bank(obj);
        obj.PreviousPosition = obj.Position;
    }

    int ObjectMotion::PathKeyCount(int path)
    {
        return static_cast<int>(Bin::U32(*_runtime->Cmi->Bytes(), path));
    }

    int ObjectMotion::PathKeyFrame(int path, int key)
    {
        return Bin::S32(*_runtime->Cmi->Bytes(), path + PathCountSize + key * PathKeySize);
    }

    Vector3 ObjectMotion::PathVector(int path, int key, int field)
    {
        const auto& bytes = *_runtime->Cmi->Bytes();
        auto offset = path + PathCountSize + key * PathKeySize + PathFrameSize + field * PathVectorSize;
        return Vector3(Bin::F32(bytes, offset), Bin::F32(bytes, offset + 4), Bin::F32(bytes, offset + 8));
    }

    void ObjectMotion::Updraft(MdkObject& obj)
    {
        if (_runtime->Level->Number == LevelSix && (obj.Flags & MdkObject::FlagRolling) != 0)
        {
            return;
        }

        auto vz = _runtime->Fans->Query(obj.Arena, obj.Position, obj.Velocity.Z, Fans::MaskObjects, Dt);
        if (std::isnan(vz))
        {
            return;
        }

        obj.Velocity.Z = vz;
        if ((obj.Flags & ThrownFlagsMask) == Items::FlagThrown && obj.ThrownKind != MortarItem)
        {
            obj.Velocity.X *= UpdraftBrake;
            obj.Velocity.Y *= UpdraftBrake;
        }
    }

    void ObjectMotion::UpdatePathSpeed(MdkObject& obj)
    {
        constexpr float Around = 5.0f;
        auto heading = FromAngle(obj.Yaw);
        auto ahead = (obj.Position.X - _runtime->KurtPosition.X) * heading.X + (obj.Position.Y - _runtime->KurtPosition.Y) * heading.Y;
        const auto& speeds = obj.PathSpeeds;
        auto distance = speeds[0];
        auto target = speeds[1];
        if (ahead <= distance + Around)
        {
            target = ahead >= distance - Around ? speeds[2] : speeds[3];
        }

        auto step = (speeds[3] - speeds[1]) * Dt * 0.5f;
        obj.PathSpeed = obj.PathSpeed <= target ? MathF::Min(obj.PathSpeed + step, target) : MathF::Max(obj.PathSpeed - step, target);
    }

    Vector3 ObjectMotion::PathPosition(int path, float time)
    {
        auto key = PathKeyCount(path) - 2;
        while (key > 0 && PathKeyFrame(path, key) >= time)
        {
            key--;
        }

        key = Math::Max(key, 0);
        auto start = PathKeyFrame(path, key);
        auto length = PathKeyFrame(path, key + 1) - start;
        auto u = length != 0 ? (time - start) / length : 0.0f;
        auto p0 = PathVector(path, key, 0);
        auto p1 = PathVector(path, key + 1, 0);
        auto m0 = PathVector(path, key, 2);
        auto m1 = PathVector(path, key + 1, 1);
        auto d = p1 - p0;
        auto a = m0 + m1 - d * 2.0f;
        auto b = d * 3.0f - m0 * 2.0f - m1;
        return ((a * u + b) * u + m0) * u + p0;
    }

    void ObjectMotion::UpdatePath(MdkObject& obj)
    {
        auto path = obj.Path;
        if (obj.PathStop >= 0 && ScriptMath::Round(obj.PathTime) == obj.PathStop)
        {
            return;
        }

        auto step = Ticks * obj.PathSpeed;
        auto last = PathKeyFrame(path, PathKeyCount(path) - 1);
        float stop = static_cast<float>(obj.PathStop);
        auto once = (obj.Flags & MdkObject::FlagPathOnce) != 0;
        if (step < 0.0f)
        {
            if (obj.PathStop < 0 || obj.PathTime < stop || stop < obj.PathTime + step)
            {
                obj.PathTime += step;
            }
            else
            {
                obj.PathTime = stop;
            }

            if (obj.PathTime < 0.0f)
            {
                if (once)
                {
                    obj.Path = 0;
                    obj.PathTime = 0.0f;
                }
                else
                {
                    obj.PathTime += last - 1;
                }
            }
        }
        else
        {
            if (obj.PathStop < 0 || stop < obj.PathTime || obj.PathTime + step < stop)
            {
                obj.PathTime += step;
            }
            else
            {
                obj.PathTime = stop;
            }

            if (once)
            {
                if (obj.PathTime >= last - 2)
                {
                    obj.Path = 0;
                    obj.PathTime = static_cast<float>(last - 2);
                }
            }
            else if (obj.PathTime >= last - 1)
            {
                obj.PathTime -= last - 1;
            }
        }

        auto old = obj.Position;
        auto p = PathPosition(path, obj.PathTime) + obj.PathOrigin;
        auto pushes = (obj.Flags & MdkObject::FlagPathPushes) != 0;
        if (pushes)
        {
            p.Z = old.Z;
        }

        if ((obj.Flags & MdkObject::FlagNoTurning) == 0 && MathF::Abs(p.X - old.X) + MathF::Abs(p.Y - old.Y) > Dt * 0.5f)
        {
            obj.Yaw = Wrap360(Heading(old, p) + obj.PathYawOffset);
        }

        if (!pushes)
        {
            obj.Position = p;
            return;
        }

        // The path drives the horizontal velocity instead of the position.
        obj.Push.X += (p.X - old.X) / Dt;
        obj.Push.Y += (p.Y - old.Y) / Dt;
    }

    void ObjectMotion::StartPath(MdkObject& obj, int path, float time, Vector3 origin)
    {
        obj.Path = path;
        obj.PathTime = time;
        obj.PathOrigin = origin;
        obj.PathStop = -1;
        obj.Position = PathPosition(path, time) + origin;
    }

    // Movement commands.

    void ObjectMotion::UpdateCommand(MdkObject& obj)
    {
        constexpr float ChaseAbove = 8.0f;
        constexpr int AlarmEvery = 31;
        constexpr int AlarmTicks = 10;
        switch (obj.MoveCommand)
        {
            case 1:
                FollowFormation(obj);
                break;
            case 6:
            {
                auto target = _runtime->TargetPosition;
                target.Z = target.Z + ChaseAbove;
                Chase(obj, target);
                break;
            }
            case 43:
            case 78:
            case 197:
            {
                auto yaw = obj.Yaw;
                auto walking = (obj.Flags & MdkObject::FlagGravity) != 0;
                if (walking)
                {
                    Walk(obj);
                }
                else
                {
                    Fly(obj);
                }

                if (obj.MoveCommand != 0)
                {
                    DetectStuck(obj, walking ? Moving::Walking : Moving::Flying);
                    HandleStuck(obj, MathF::Abs(WrapAngle(obj.Yaw - yaw)));
                }

                break;
            }
            case 61:
                FlyProjectile(obj);
                break;
            case 74:
                FollowAttachment(obj);
                break;
            case 88:
                MoveForward(obj);
                break;
            case 30:
                UpdateChain(obj);
                break;
            case 15:
                // The alarm: ALERT every 32 ticks, and if_alarm holds for 10 ticks.
                if (obj.Arena != _runtime->CurrentArena)
                {
                    break;
                }

                if ((_runtime->TickCount() & AlarmEvery) == 0)
                {
                    _runtime->PlaySoundAt("ALERT", obj.Position);
                }

                _runtime->AlarmTicks = AlarmTicks;
                break;
            case 229:
                obj.ParameterTimer += Dt;
                if (obj.ParameterTimer >= obj.Parameter * 0.5f && (obj.ContactFlags & MdkObject::ContactFloor) != 0)
                {
                    obj.MoveCommand = 0;
                    obj.Velocity = Vector3::Zero();
                }

                break;
        }
    }

    void ObjectMotion::Roll(MdkObject& obj)
    {
        auto moved = Vector2(obj.Position.X - obj.PreviousPosition.X, obj.Position.Y - obj.PreviousPosition.Y);
        auto distance = moved.Length();
        if (distance <= 0.0f)
        {
            return;
        }

        auto radius = obj.RollRadius > 0.0f ? obj.RollRadius : 1.0f;
        auto axis = Vector3(-moved.Y, moved.X, 0.0f) / distance;
        obj.RollingBasis = Orthonormal(obj.RollingBasis * Matrix4x4::CreateFromAxisAngle(axis, distance / radius));
    }

    Matrix4x4 ObjectMotion::Orthonormal(const Matrix4x4& m)
    {
        auto x = Vector3::Normalize(Vector3(m.M11, m.M12, m.M13));
        auto y = Vector3(m.M21, m.M22, m.M23);
        y = Vector3::Normalize(y - Vector3::Dot(y, x) * x);
        auto z = Vector3::Cross(x, y);
        return Matrix4x4(x.X, x.Y, x.Z, 0.0f, y.X, y.Y, y.Z, 0.0f, z.X, z.Y, z.Z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    void ObjectMotion::TurnAndJump(MdkObject& obj, float rate)
    {
        constexpr int JumpCommand = 229;
        constexpr float AboveHigher = 10.0f;
        constexpr float AboveLower = 30.0f;
        auto heading = obj.YawTo(obj.MoveDestination);
        auto diff = WrapAngle(heading - obj.Yaw);
        if (MathF::Abs(diff) > rate * Dt)
        {
            obj.Yaw = Wrap360(obj.Yaw + MathF::Sign(diff) * rate * Dt);
            return;
        }

        obj.Yaw = heading;
        obj.MoveCommand = JumpCommand;
        auto z = obj.Position.Z;
        auto goal = obj.MoveDestination.Z;
        auto top = goal < z ? MathF::Max(z + AboveHigher, goal + AboveLower) : MathF::Max(goal + AboveHigher, z + AboveLower);
        auto g = obj.Gravity;
        auto up = MathF::Sqrt((top - z) * 2.0f * g);
        auto time = (up + MathF::Sqrt(MathF::Max(up * up + (z - goal) * g * 2.0f, 0.0f))) / g;
        obj.Parameter = time;
        obj.ParameterTimer = 0.0f;
        auto offset = Vector2(obj.MoveDestination.X - obj.Position.X, obj.MoveDestination.Y - obj.Position.Y);
        auto distance = offset.Length();
        auto speed = (obj.Friction * 0.5f * time * time + distance) / time;
        auto direction = distance > 0.0f ? offset / distance : Vector2::Zero();
        obj.Velocity = Vector3(direction.X * speed, direction.Y * speed, up);
    }

    void ObjectMotion::UpdateChain(MdkObject& link)
    {
        constexpr float LinkTurn = 90.0f;
        auto leader = link.Leader;
        if (leader == nullptr || leader->Dead)
        {
            link.Leader = nullptr;
            link.MoveCommand = 0;
            return;
        }

        auto links = Links(*leader);
        for (int i = 0; i < link.InstanceId; i++)
        {
            if (i >= static_cast<int>(links.size()) || links[i]->InstanceId != i)
            {
                link.Leader = nullptr;
                link.MoveCommand = 0;
                return;
            }
        }

        if (link.InstanceId != 0)
        {
            return;
        }

        MdkObject* previous = leader.get();
        for (int i = 0; i < static_cast<int>(links.size()) && links[i]->InstanceId == i; i++)
        {
            auto& current = *links[i];
            current.Yaw = Wrap360(leader->Yaw + LinkTurn * i);
            auto hook = previous->Model != nullptr && previous->Model->ReferencePoints.size() < 2 ? 0 : 1;
            current.Position += previous->ReferencePoint(hook) - current.ReferencePoint(0);
            previous = &current;
        }
    }

    std::vector<std::shared_ptr<MdkObject>> ObjectMotion::Links(MdkObject& leader)
    {
        _links.clear();
        for (const auto& other : _runtime->Objects)
        {
            if (other->Leader.get() != &leader || other->Dead)
            {
                continue;
            }

            auto at = _links.size();
            while (at > 0 && _links[at - 1]->InstanceId > other->InstanceId)
            {
                at--;
            }

            _links.insert(_links.begin() + static_cast<std::ptrdiff_t>(at), other);
        }

        return _links;
    }

    void ObjectMotion::Swing(MdkObject& obj)
    {
        constexpr float MaxTurn = 15.0f;
        constexpr float QuarterTurn = 90.0f;
        constexpr float PlaneTurnSpeed = 10.0f;
        auto oldSpeed = obj.SwingSpeed;
        obj.SwingSpeed -= MathF::Sin(ToRadians(obj.Pitch)) * obj.SwingGain * Ticks;
        obj.Pitch += obj.SwingSpeed * obj.SwingGain * Ticks;
        auto [sin, cos] = MathF::SinCos(ToRadians(obj.Pitch));
        auto heading = FromAngle(obj.Yaw);
        obj.Position = obj.SwingPivot + Vector3(heading.X * sin, heading.Y * sin, -cos) * obj.SwingLength;
        if (obj.SwingSpeed * oldSpeed <= 0.0f)
        {
            obj.Yaw = Wrap360(obj.Yaw);
            auto diff = WrapAngle(obj.YawTo(_runtime->TargetPosition) - obj.Yaw);
            if (diff < -QuarterTurn)
            {
                diff += HalfTurn;
            }
            else if (diff > QuarterTurn)
            {
                diff -= HalfTurn;
            }

            obj.SwingYaw = obj.Yaw + MathF::Clamp(diff, -MaxTurn, MaxTurn);
        }

        obj.Yaw = MoveToward(obj.Yaw, obj.SwingYaw, PlaneTurnSpeed * Dt);
        obj.RopeColor = 1;
        obj.RopeMask = 1;
        obj.RopePoints[0] = obj.Position;
        obj.RopePoints[1] = obj.SwingPivot;
    }

    void ObjectMotion::PlanMove(MdkObject& obj)
    {
        constexpr int Detours = 11;
        obj.Waypoint = obj.MoveDestination;
        obj.StuckCount = 0;
        obj.StuckTicks = 0.0f;
        obj.StuckMoved = Vector3::Zero();
        obj.ContactFlags &= ~MdkObject::ContactStuck;
        auto up = Vector3(0.0f, 0.0f, PlanHeight);
        auto a = obj.Position + up;
        auto b = obj.MoveDestination + up;
        if (!_runtime->Raycast(a, b))
        {
            return;
        }

        auto middle = (a + b) * 0.5f;
        for (int i = 0; i < Detours; i++)
        {
            for (auto side : Sides)
            {
                auto point = Detour(a, b, middle, 0.1f * (i + 1) * side);
                if (Clear(a, point, b))
                {
                    obj.Waypoint = point;
                    return;
                }
            }
        }
    }

    void ObjectMotion::Replan(MdkObject& obj)
    {
        constexpr int Detours = 7;
        if (obj.Waypoint != obj.MoveDestination)
        {
            obj.Waypoint = obj.MoveDestination;
            obj.StuckTicks = 0.0f;
            obj.StuckMoved = Vector3::Zero();
            obj.ContactFlags &= ~MdkObject::ContactStuck;
            return;
        }

        auto up = Vector3(0.0f, 0.0f, PlanHeight);
        auto a = obj.Position + up;
        auto b = obj.MoveDestination + up;
        for (auto origin : {b * 0.75f + a * 0.25f, a})
        {
            for (int i = 0; i < Detours; i++)
            {
                for (auto side : Sides)
                {
                    auto point = Detour(a, b, origin, (0.1f + 0.3f * i) * side);
                    if (Clear(a, point, b))
                    {
                        obj.Waypoint = point;
                        return;
                    }
                }
            }
        }
    }

    Vector3 ObjectMotion::Detour(Vector3 a, Vector3 b, Vector3 origin, float amount)
    {
        auto [sin, cos] = MathF::SinCos(MathF::Atan2(b.Y - a.Y, b.X - a.X));
        return origin + Vector3(sin, cos, 0.0f) * amount * Vector3::Distance(a, b);
    }

    bool ObjectMotion::Clear(Vector3 a, Vector3 point, Vector3 b)
    {
        return !_runtime->Raycast(a, point) && !_runtime->Raycast(point, b);
    }

    void ObjectMotion::DetectStuck(MdkObject& obj, Moving moving)
    {
        constexpr float StuckSpeed = 2.0f;
        constexpr float Window = 16.0f;
        constexpr float AdvanceWindow = 9.0f;
        constexpr float CoverStuckTicks = 30.0f;
        auto collided = (obj.ContactFlags & MdkObject::ContactCollided) != 0;
        auto walking = moving == Moving::Walking;
        if (walking && obj.MoveCommand == 43)
        {
            if (collided)
            {
                obj.ContactFlags |= MdkObject::ContactStuck;
                obj.StuckTicks = CoverStuckTicks;
                return;
            }

            ResetStuck(obj);
            return;
        }

        if (!collided || obj.Speed <= StuckSpeed)
        {
            ResetStuck(obj);
            return;
        }

        auto window = walking && obj.MoveCommand == 197 ? AdvanceWindow : Window;
        if (obj.StuckTicks < window)
        {
            obj.StuckMoved += obj.Position - obj.PreviousPosition;
            obj.StuckTicks += Ticks;
            return;
        }

        auto moved = obj.StuckMoved;
        if (MathF::Abs(moved.X) + MathF::Abs(moved.Y) + MathF::Abs(moved.Z) >= obj.Speed * 0.5f)
        {
            obj.StuckTicks = 0.0f;
            obj.StuckMoved = Vector3::Zero();
            return;
        }

        obj.ContactFlags |= MdkObject::ContactStuck;
    }

    void ObjectMotion::ResetStuck(MdkObject& obj)
    {
        obj.StuckCount = 0;
        obj.StuckTicks = 0.0f;
        obj.StuckMoved = Vector3::Zero();
    }

    void ObjectMotion::HandleStuck(MdkObject& obj, float turned)
    {
        constexpr float Turning = 3.0f;
        constexpr int Replans = 3;
        if ((obj.ContactFlags & MdkObject::ContactStuck) == 0 || turned >= Turning)
        {
            return;
        }

        obj.StuckCount++;
        if (obj.MoveCommand == 197)
        {
            Stop(obj);
        }
        else if (obj.StuckCount < Replans)
        {
            Replan(obj);
            obj.StuckCount++;
        }
        else if (obj.MoveCommand == 78)
        {
            auto toWaypoint = obj.Waypoint - obj.Position;
            auto step = obj.Speed * Dt;
            obj.Position += Vector3::Clamp(toWaypoint, Vector3(-step), Vector3(step));
        }
        else
        {
            obj.MoveCommand = 0;
            obj.Speed = 0.0f;
        }
    }

    void ObjectMotion::Bank(MdkObject& obj)
    {
        constexpr float MaxTurn = 2.0f;
        constexpr float MaxRoll = 10.0f;
        constexpr float Damping = 0.95f;
        constexpr int NoRoll = 1;
        auto turn = MathF::Clamp(WrapAngle(obj.Yaw - obj.BankingYaw) / Ticks, -MaxTurn, MaxTurn);
        obj.BankingYaw = obj.Yaw;
        if ((obj.Flags & (MdkObject::FlagNoBanking | NoRoll | MdkObject::FlagRolling)) != 0 || obj.ThrownKind > 0)
        {
            return;
        }

        obj.Roll = MathF::Clamp((WrapAngle(obj.Roll) - turn) * Damping, -MaxRoll, MaxRoll);
    }

    float ObjectMotion::TurnTowards(MdkObject& obj, Vector3 point)
    {
        auto targetYaw = obj.Yaw;
        if (point.X != obj.Position.X || point.Y != obj.Position.Y)
        {
            targetYaw = obj.YawTo(point);
        }

        auto diff = WrapAngle(targetYaw - obj.Yaw);
        auto maxStep = TurnSpeed * Dt;
        obj.Yaw = Wrap360(obj.Yaw + MathF::Clamp(diff, -maxStep, maxStep));
        return diff;
    }

    void ObjectMotion::Accelerate(MdkObject& obj, float targetSpeed)
    {
        if (obj.Speed > targetSpeed)
        {
            obj.Speed = MathF::Max(obj.Speed - obj.Deceleration * Dt, targetSpeed);
        }
        else if (obj.Speed < targetSpeed)
        {
            obj.Speed = MathF::Min(obj.Speed + obj.Acceleration * Dt, targetSpeed);
        }
    }

    void ObjectMotion::Stop(MdkObject& obj)
    {
        obj.MoveCommand = 0;
        obj.Speed = 0.0f;
        obj.ContactFlags &= ~MdkObject::ContactStuck;
    }

    float ObjectMotion::TurnSpeedFactor(float diff)
    {
        return MathF::Abs(diff) > SlowTurnAngle ? 0.5f : 1.0f;
    }

    void ObjectMotion::Walk(MdkObject& obj)
    {
        constexpr float Arrived = 5.0f;
        auto final = obj.Waypoint == obj.MoveDestination;
        if (MathF::Abs(obj.Waypoint.X - obj.Position.X) + MathF::Abs(obj.Waypoint.Y - obj.Position.Y) < Arrived)
        {
            if (final)
            {
                Stop(obj);
                return;
            }

            obj.Waypoint = obj.MoveDestination;
        }

        auto diff = TurnTowards(obj, obj.Waypoint);
        Accelerate(obj, obj.MaxSpeed * TurnSpeedFactor(diff));
        auto direction = FromAngle(obj.Yaw);
        obj.Push.X += obj.Speed * direction.X;
        obj.Push.Y += obj.Speed * direction.Y;
    }

    // Vector3's components by index (C#'s indexer).
    static float& Component(Vector3& v, int axis)
    {
        return axis == 0 ? v.X : (axis == 1 ? v.Y : v.Z);
    }

    void ObjectMotion::Fly(MdkObject& obj)
    {
        constexpr float TurnFrom = 3.0f;
        constexpr float ArrivedAcross = 4.0f;
        constexpr float ArrivedUp = 3.0f;
        auto final = obj.Waypoint == obj.MoveDestination;
        auto toWaypoint = obj.Waypoint - obj.Position;
        auto diff = 0.0f;
        auto noTurning = (obj.Flags & MdkObject::FlagNoTurning) != 0;
        if (!noTurning && MathF::Abs(toWaypoint.X) + MathF::Abs(toWaypoint.Y) > TurnFrom)
        {
            diff = TurnTowards(obj, obj.Waypoint);
        }

        Accelerate(obj, obj.MaxSpeed * TurnSpeedFactor(diff));
        if (!noTurning)
        {
            auto horizontal = Vector2(toWaypoint.X, toWaypoint.Y).Length();
            auto vertical = MathF::Abs(toWaypoint.Z);
            auto climb = horizontal + vertical > 0.0f ? vertical / (horizontal + vertical) : 0.0f;
            auto direction = FromAngle(obj.Yaw) * obj.Speed * (1.0f - climb);
            obj.Push += Vector3(direction.X, direction.Y, obj.Speed * climb * MathF::Sign(toWaypoint.Z));
            if (horizontal < ArrivedAcross && vertical < ArrivedUp)
            {
                if (final)
                {
                    Stop(obj);
                }
                else
                {
                    obj.Waypoint = obj.MoveDestination;
                }
            }

            return;
        }

        auto distance = MathF::Abs(toWaypoint.X) + MathF::Abs(toWaypoint.Y) + MathF::Abs(toWaypoint.Z);
        if (distance < 1.0f)
        {
            if (final)
            {
                obj.Position = obj.Waypoint;
                Stop(obj);
                return;
            }

            obj.Waypoint = obj.MoveDestination;
        }

        if (distance <= 0.0f)
        {
            return;
        }

        auto step = toWaypoint * (obj.Speed * Dt / distance);
        for (int axis = 0; axis < 3; axis++)
        {
            if (MathF::Abs(Component(step, axis)) > MathF::Abs(Component(toWaypoint, axis)))
            {
                Component(step, axis) = Component(toWaypoint, axis);
            }
        }

        obj.Push += step / Dt;
    }

    void ObjectMotion::Chase(MdkObject& obj, Vector3 point)
    {
        constexpr float Behind = 90.0f;
        constexpr float Far = 30.0f;
        constexpr float Ahead = 22.5f;
        constexpr float Gain = 0.0444444f / 6.0f;
        constexpr float MaxChase = 2.3333333f;
        constexpr float Brake = 0.05f;
        constexpr float MinChase = 0.3333333f;
        constexpr float Climb = 0.25f;
        auto targetYaw = obj.Yaw;
        if (point.X != obj.Position.X || point.Y != obj.Position.Y)
        {
            targetYaw = obj.YawTo(point);
        }

        auto offAngle = MathF::Abs(WrapAngle(obj.Yaw - targetYaw));
        auto chaseSpeed = obj.Speed;
        if (offAngle < Behind || MathF::Abs(obj.Position.X - point.X) > Far || MathF::Abs(obj.Position.Y - point.Y) > Far)
        {
            TurnTowards(obj, point);
            chaseSpeed = offAngle <= Ahead
                ? MathF::Min(chaseSpeed + (Ahead - offAngle) * Ticks * Gain, MaxChase)
                : MathF::Max(chaseSpeed - Ticks * Brake, MinChase);
            obj.Speed = chaseSpeed;
        }

        auto step = chaseSpeed * Ticks;
        auto direction = FromAngle(obj.Yaw) * step;
        obj.Position.X += direction.X;
        obj.Position.Y += direction.Y;
        obj.Position.Z = MoveToward(obj.Position.Z, point.Z, step * Climb);
    }

    void ObjectMotion::FollowFormation(MdkObject& obj)
    {
        auto leader = obj.Leader;
        if (leader == nullptr || leader->Dead)
        {
            obj.MoveCommand = 0;
            return;
        }

        auto destination = FormationPosition(*leader, obj.Waypoint);
        obj.MoveDestination = destination;
        auto toDestination = destination - obj.Position;
        auto maxStep = obj.MaxSpeed * Dt;
        if (toDestination.LengthSquared() < maxStep * maxStep)
        {
            obj.Position = destination;
            obj.Yaw = MoveToward(obj.Yaw, leader->Yaw, TurnSpeed * Dt);
            return;
        }

        TurnTowards(obj, destination);
        auto direction = FromAngle(obj.Yaw) * obj.MaxSpeed;
        obj.Push.X += direction.X;
        obj.Push.Y += direction.Y;
        if (obj.Position.Z <= destination.Z - Ticks)
        {
            obj.Push.Z += Ticks;
        }
        else if (obj.Position.Z >= destination.Z + Ticks)
        {
            obj.Push.Z -= Ticks;
        }
        else
        {
            obj.Push.Z = destination.Z - obj.Position.Z;
        }
    }

    Vector3 ObjectMotion::FormationPosition(MdkObject& leader, Vector3 offset)
    {
        auto [s, c] = MathF::SinCos(ToRadians(leader.Yaw));
        auto p = leader.Position;
        return Vector3(p.X + offset.X * s - offset.Y * c, p.Y - offset.Y * s - offset.X * c, p.Z + offset.Z);
    }

    void ObjectMotion::MoveForward(MdkObject& obj)
    {
        auto targetSpeed = obj.MoveParameter != 0 ? obj.MaxSpeed : 0.0f;
        Accelerate(obj, targetSpeed);
        auto direction = FromAngle(obj.Yaw);
        if ((obj.Flags & MdkObject::FlagGravity) != 0)
        {
            obj.Push.X += obj.Speed * direction.X;
            obj.Push.Y += obj.Speed * direction.Y;
        }
        else
        {
            auto [sin, cos] = MathF::SinCos(ToRadians(obj.Pitch));
            obj.Push += Vector3(direction.X * cos, direction.Y * cos, sin) * obj.Speed;
        }

        if (obj.MoveParameter == 0 && obj.Speed == targetSpeed)
        {
            obj.MoveCommand = 0;
        }
    }

    void ObjectMotion::FollowAttachment(MdkObject& obj)
    {
        auto other = obj.Leader;
        if (other == nullptr || other->Dead || other->Health == 0)
        {
            obj.MoveCommand = 0;
            return;
        }

        obj.Position += other->ReferencePoint(obj.AttachPoints.B) - obj.ReferencePoint(obj.AttachPoints.A);
        obj.Yaw = other->Yaw;
        obj.Pitch = other->Pitch;
    }

    void ObjectMotion::FlyProjectile(MdkObject& obj)
    {
        constexpr float KurtMargin = 1.0f;
        auto old = obj.Position;
        auto [yawSin, yawCos] = MathF::SinCos(ToRadians(obj.Yaw));
        auto [pitchSin, pitchCos] = MathF::SinCos(ToRadians(obj.Pitch));
        obj.Position += Vector3(yawCos * pitchCos, yawSin * pitchCos, pitchSin) * obj.Speed * Dt;
        obj.Parameter -= Dt;
        if (obj.Parameter <= 0.0f)
        {
            _runtime->Kill(obj);
            return;
        }

        if (auto hit = _runtime->Raycast(old, obj.Position))
        {
            obj.Position = hit->Point;
            _runtime->Kill(obj);
            return;
        }

        auto touch = SegmentEntry(Grow(_runtime->GetKurtBox(), KurtMargin), old, obj.Position);
        if (touch)
        {
            obj.ContactFlags |= MdkObject::ContactTouchedKurt;
            obj.Position = *touch;
            if (obj.TouchDamage > 0)
            {
                _runtime->HurtKurt(obj.TouchDamage);
                _runtime->Kill(obj);
            }
        }
        else
        {
            obj.ContactFlags &= ~MdkObject::ContactTouchedKurt;
        }
    }

    // Velocity.

    void ObjectMotion::ApplyVelocity(MdkObject& obj)
    {
        obj.ContactFlags &= ~(MdkObject::ContactCollided | MdkObject::ContactFloor | ContactCleared);
        auto gravity = (obj.Flags & MdkObject::FlagGravity) != 0;
        auto moving = obj.Velocity;
        if (gravity)
        {
            moving.Z = 0.0f;
        }

        auto length = moving.Length();
        if (length > 0.0f)
        {
            auto factor = MathF::Max(length - obj.Friction * Dt, 0.0f) / length;
            obj.Velocity.X *= factor;
            obj.Velocity.Y *= factor;
            if (!gravity)
            {
                obj.Velocity.Z *= factor;
            }
        }

        auto motion = (obj.Velocity + obj.Push) * Dt;
        obj.Push = Vector3::Zero();
        if (motion == Vector3::Zero())
        {
            return;
        }

        if ((obj.Flags & MdkObject::FlagCollides) == 0)
        {
            obj.Position += motion;
        }
        else if (auto normal = Sweep(obj, motion))
        {
            obj.ContactFlags |= MdkObject::ContactCollided;
            if (motion.Z <= 0.0f)
            {
                obj.ContactFlags |= MdkObject::ContactFloor;
            }

            if ((obj.Flags & MdkObject::FlagBounces) != 0)
            {
                obj.Velocity += *normal * Vector3::Dot(obj.Velocity, *normal) * Bounce;
            }
            else
            {
                obj.Velocity.Z = 0.0f;
            }
        }

        if (obj.Position.Z < _runtime->GetArenaFloor(obj.Arena) - FallOutDepth)
        {
            _runtime->FallOut(obj);
        }
    }

    std::optional<Vector3> ObjectMotion::Sweep(MdkObject& obj, Vector3 motion)
    {
        constexpr float WidthFactor = 0.25f;
        constexpr float MinHeight = 0.5f;
        constexpr float VerticalLift = 0.05f;
        constexpr float Lift = 0.5f;
        auto bsp = _runtime->BspOf(obj.Arena);
        if (bsp == nullptr)
        {
            obj.Position += motion;
            return std::nullopt;
        }

        auto bounds = obj.Model != nullptr ? obj.PoseBounds() : DefaultBounds;
        auto [s, c] = MathF::SinCos(ToRadians(obj.Yaw));
        s = MathF::Abs(s);
        c = MathF::Abs(c);
        auto size = Size(bounds) * obj.Scale;
        auto half = Vector3((c * size.X + s * size.Y) * WidthFactor, (s * size.X + c * size.Y) * WidthFactor,
            MathF::Max(bounds.Max.Z * obj.Scale + obj.Lift(), MinHeight) * 0.5f);
        auto center = RotatedZ(Center(bounds), obj.Yaw) * obj.Scale;
        center.Z = half.Z + (motion.Z != 0.0f ? VerticalLift : Lift);

        // A vertical move reaches down to z − height offset, so the object stops that high above
        // the floor (0x45e810), e.g. LEVEL8's centred XT 8 above. Rolling objects are drawn lifted instead.
        if (motion.Z != 0.0f)
        {
            auto drop = obj.HeightOffset - obj.Lift();
            half.Z += drop * 0.5f;
            center.Z -= drop * 0.5f;
        }

        auto a = obj.Position + center;

        // The first contact counts, even if the box slid off it and ended free.
        _firstContact = std::nullopt;
        _sweeping = bsp;
        if (!_onContact)
        {
            _onContact = [this](Collision::Bsp::Contact contact) { FirstContact(contact); };
        }

        Vector3 end;
        int node = 0;
        bsp->SweepBox(a, a + motion, half, SweepIterations, SweepSlide, end, node, _onContact);
        obj.Position += end - a;
        return _firstContact;
    }

    void ObjectMotion::FirstContact(Collision::Bsp::Contact contact)
    {
        if (_firstContact)
        {
            return;
        }

        _firstContact = contact.Node >= 0 ? _sweeping->Arena->Nodes[contact.Node].Normal : Vector3::UnitZ();
    }
}
