#include "game/scripts/Bomber.h"

#include "core/DotNet.h"
#include "game/Angles.h"
#include "game/audio/SoundMixer.h"
#include "game/kurt/Kurt.h"
#include "game/scripts/Items.h"
#include "game/scripts/Rides.h"
#include "game/scripts/ScriptMath.h"
#include "game/scripts/ScriptRuntime.h"

namespace Mdk::Game::Scripts
{
    using Engine::Platform::Input;
    using Engine::Platform::Key;
    using Objects::MdkObject;

    Bomber::Bomber(ScriptRuntime& runtime, std::shared_ptr<MdkObject> xe)
        : _runtime(&runtime), _xe(std::move(xe))
    {
    }

    bool Bomber::Locked() const
    {
        return (_xe->Flags & Rides::FlagLocked) != 0;
    }

    std::optional<Hud::BomberSight> Bomber::Shown() const
    {
        if (Locked())
        {
            return std::nullopt;
        }

        return Hud::BomberSight{Cursor, BombsLeft};
    }

    void Bomber::Update(Input& input, float delta)
    {
        auto& kurt = *_runtime->Kurt;
        kurt.Feet = _xe->Position;
        kurt.Yaw = _xe->Yaw;
        kurt.VerticalSpeed = 0.0f;
        ViewHeight = MathF::Max(ViewHeight - CameraDescent * delta, 0.0f);
        kurt.TopDownHeight = ViewHeight;
        if (HidesXe())
        {
            _xe->Flags |= MdkObject::FlagNotSolid;
        }

        auto fire = input.IsDown(Key::Fire);
        if (Locked())
        {
            _speed.Y = 0.0f;
        }
        else
        {
            MoveCursor(input, delta);
            if (fire && !_fireHeld && BombsLeft > 0)
            {
                Drop();
            }

            RefillBombs(delta);
        }

        _fireHeld = fire;
        PassDamage();
    }

    void Bomber::MoveCursor(Input& input, float delta)
    {
        auto ticks = delta * Game::Kurt::Kurt::Ticks;
        auto turn = Axis(input, Key::TurnRight, Key::TurnLeft);
        auto strafe = Axis(input, Key::StrafeRight, Key::StrafeLeft);
        auto axis = Vector2(MathF::Abs(turn) >= MathF::Abs(strafe) ? turn : strafe, Axis(input, Key::Back, Key::Forward));
        _speed = Vector2(CursorStep(_speed.X, axis.X, ticks), CursorStep(_speed.Y, axis.Y, ticks));

        // The mouse moves it only while no key does.
        auto mouse = Vector2(input.MouseX(), input.MouseY()) * MouseScale;
        if (axis == Vector2::Zero() && mouse != Vector2::Zero())
        {
            Cursor += mouse;
            _speed = Vector2::Zero();
        }

        Cursor = Vector2::Clamp(Cursor + _speed * ticks, CursorMin, CursorMax);
    }

    float Bomber::CursorStep(float speed, float axis, float ticks)
    {
        if (axis == 0.0f)
        {
            return MathF::Abs(speed) <= CursorBrake * ticks ? 0.0f : speed - MathF::Sign(speed) * CursorBrake * ticks;
        }

        auto step = axis * CursorAccel * ticks;
        auto result = speed == 0.0f || MathF::Sign(speed) == MathF::Sign(step) ? speed + step : step;
        auto limit = MathF::Abs(axis) * CursorSpeed;
        return Math::Clamp(result, -limit, limit);
    }

    void Bomber::RefillBombs(float delta)
    {
        if (BombsLeft >= Bombs)
        {
            _refill = RefillTime;
            return;
        }

        _refill -= delta;
        if (_refill <= 0.0f)
        {
            BombsLeft++;
            _refill = RefillTime;
        }
    }

    // A bomb from under the XE, thrown so it falls onto the point under the cursor.
    //
    //        Kurt ●───────► (vx, vy, 0)
    //             ╎  ╲ falls by gravity for t = √(2h / g)
    //             ╎     ╲
    //   ──────────┴───────✕ hit point (ray through the cursor)
    void Bomber::Drop()
    {
        BombsLeft--;
        auto kurt = _runtime->KurtPosition;
        auto start = kurt - Vector3(0.0f, 0.0f, DropBelow);
        bool hit;
        auto target = Aim(kurt, Cursor, _xe->Yaw, hit);
        auto time = hit ? FallTime(start.Z - target.Z) : MissTime;

        auto controller = _runtime->GetArenaState(_runtime->CurrentArena)->Controller;
        auto bomb = _runtime->Spawn(*controller, std::string(Bomb), start, _xe->Yaw, -1, 0, ScriptRuntime::Spawning::Plain);
        if (bomb == nullptr)
        {
            return;
        }

        bomb->Flags |= BombFlags;
        bomb->ThrownKind = Items::KindBomb;
        bomb->ItemTicks = BombTicks;
        bomb->Friction = 0.0f;
        bomb->Velocity = Vector3((target.X - start.X) / time, (target.Y - start.Y) / time, 0.0f);
        bomb->LoopSound = _runtime->Mixer->PlayOn(std::string(DropSound), [bomb] { return bomb->Position; });
    }

    Vector3 Bomber::Aim(Vector3 kurt, Vector2 cursor, float yaw, bool& hit)
    {
        auto end = kurt + AimDirection(cursor, yaw) * RayLength;
        auto target = FirstObject(kurt, end).value_or(end);
        hit = target != end;
        if (auto ray = _runtime->Raycast(kurt, target))
        {
            hit = true;
            return ray->Point;
        }

        return target;
    }

    Vector3 Bomber::AimDirection(Vector2 cursor, float yaw)
    {
        auto offset = cursor - CursorCentre;
        auto s = MathF::Sin(DegreesToRadians(yaw));
        auto c = MathF::Cos(DegreesToRadians(yaw));
        return Vector3(s * offset.X - c * offset.Y, -c * offset.X - s * offset.Y, -Focal);
    }

    float Bomber::FallTime(float height)
    {
        auto time = MathF::Sqrt(MathF::Max(height, 0.0f) * 2.0f / FallGravity);
        return time <= 0.0f ? MissTime : time;
    }

    std::optional<Vector3> Bomber::FirstObject(Vector3 start, Vector3 end)
    {
        std::optional<Vector3> nearest;
        for (auto& obj : _runtime->Objects)
        {
            if (obj->Dead || obj->Health == 0 || obj->Arena != _runtime->CurrentArena || obj->Model == nullptr || (obj->Flags & Unaimed) != 0
                || !ScriptMath::SegmentEntry(_runtime->GetWorldBounds(*obj), start, end).has_value())
            {
                continue;
            }

            const auto& parts = obj->PartBounds();
            for (int i = 0; i < static_cast<int>(parts.size()); i++)
            {
                if ((obj->HiddenParts & (1 << i)) != 0 || !parts[i].has_value())
                {
                    continue;
                }

                auto point = ScriptMath::SegmentEntry(_runtime->GetWorldBounds(*obj, *parts[i]), start, end);
                if (!point.has_value())
                {
                    continue;
                }

                if (!nearest.has_value() || Vector3::Distance(start, *point) < Vector3::Distance(start, *nearest))
                {
                    nearest = *point;
                }
            }
        }

        return nearest;
    }

    void Bomber::PassDamage()
    {
        if (_xe->Health == Health)
        {
            return;
        }

        auto& kurt = *_runtime->Kurt;
        kurt.Hurt(Health - _xe->Health);
        _xe->Health = Health;
        if (kurt.Health < 1)
        {
            _xe->Health = 0;
            _runtime->Kill(*_xe);
        }
    }

    float Bomber::Axis(Input& input, Key positive, Key negative)
    {
        return (input.IsDown(positive) ? 1.0f : 0.0f) - (input.IsDown(negative) ? 1.0f : 0.0f);
    }
}
