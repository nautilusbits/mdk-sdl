#include "Mdk.Game/Scripts/Twister.h"

#include "Mdk.Game/Objects/MdkObject.h"
#include "Mdk.Game/Scripts/ScriptMath.h"
#include "Mdk.Game/Scripts/ScriptRuntime.h"

namespace Mdk::Game::Scripts
{
    using namespace ScriptMath;
    using Objects::MdkObject;

    Twister::Twister(ScriptRuntime& runtime, std::string arena, Vector3 origin, float yaw)
        : Arena(std::move(arena)), Position(origin), _runtime(&runtime), _yaw(yaw), _origin(origin)
    {
    }

    bool Twister::Tick(std::vector<std::shared_ptr<Twister>>& twisters)
    {
        if (!_chasing)
        {
            Spiral(twisters);
            return true;
        }

        // Chase, bouncing off the walls.
        auto next = Position + _velocity * ScriptRuntime::Tick;
        if (auto hit = _runtime->Raycast(Position, next))
        {
            auto normal = Facing(hit->Normal, next - Position);
            _velocity -= normal * Vector3::Dot(_velocity, normal) * 2.0f;
            next = hit->Point + normal * WallGap;
        }

        Position = next;
        Ribbon->Push(Position);
        HurtObjects();
        _life--;
        if (_life <= 0)
        {
            return false;
        }

        if (_target != nullptr && !_target->Dead && _target->Health > 0)
        {
            auto toward = Vector3::Normalize(Center(_runtime->GetWorldBounds(*_target)) - Position);
            _velocity = _velocity * Keep + toward * Pull;
        }
        else
        {
            _target = nullptr;
        }

        return true;
    }

    void Twister::Spiral(std::vector<std::shared_ptr<Twister>>& twisters)
    {
        _angle += ScriptRuntime::Tick * SpiralRate;
        auto r = _angle / SpiralEnd;
        auto direction = FromAngle(_angle + _yaw);
        MoveTo(_origin + Vector3(direction, 1.0f) * SpiralSize * r);
        Ribbon->Push(Position);
        _velocity = Vector3(direction.Y, -direction.X, 0.0f) * SpiralSpeed;
        if (_angle <= SpiralEnd)
        {
            return;
        }

        _chasing = true;
        Split(twisters);
    }

    void Twister::HurtObjects()
    {
        auto copy = _runtime->Objects;
        for (auto& obj : copy)
        {
            if (obj->Dead || obj->Arena != Arena || obj->Health == 0 || (obj->Flags & (MdkObject::FlagNotSolid | MdkObject::FlagNotTarget)) != 0)
            {
                continue;
            }

            auto bounds = _runtime->GetWorldBounds(*obj);
            if (!Contains(bounds, Position))
            {
                continue;
            }

            auto direction = Heading(_runtime->KurtPosition, Center(bounds));
            if (obj->Health < ScriptRuntime::Indestructible)
            {
                obj->Health -= _runtime->KurtDamage(*obj, Damage);
            }

            obj->HitEvent = HitEvent;
            obj->HitDirection = direction;
            _life--;
            if (obj->Health < 1)
            {
                _runtime->Kill(*obj, direction + HalfTurn);
            }
        }
    }

    void Twister::Split(std::vector<std::shared_ptr<Twister>>& twisters)
    {
        for (auto& obj : _runtime->Objects)
        {
            if (obj->Dead || obj->Arena != Arena || (obj->Flags & (MdkObject::FlagNotSolid | MdkObject::FlagNotTarget)) != 0)
            {
                continue;
            }

            if (_target == nullptr)
            {
                _target = obj;
                continue;
            }

            auto twister = std::make_shared<Twister>(*_runtime, Arena, _origin, _yaw);
            twister->Position = Position;
            twister->_velocity = _velocity;
            twister->_chasing = true;
            twister->_target = obj;
            twisters.push_back(twister);
        }
    }

    void Twister::MoveTo(Vector3 next)
    {
        if (auto hit = _runtime->Raycast(Position, next))
        {
            next = hit->Point + Facing(hit->Normal, next - Position) * WallGap;
        }

        Position = next;
    }
}
