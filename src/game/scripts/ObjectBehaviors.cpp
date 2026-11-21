#include "game/scripts/ObjectBehaviors.h"

#include <algorithm>
#include <utility>

#include "game/audio/SoundMixer.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/Items.h"
#include "game/scripts/ScriptMath.h"
#include "game/scripts/ScriptRuntime.h"

namespace Mdk::Game::Scripts
{
    using namespace ScriptMath;
    using Objects::MdkObject;

    void ObjectBehaviors::UpdateDoor(MdkObject& obj)
    {
        // A door seen from its other side moves into Kurt's arena (0x43cc68), so it stays live,
        // solid and drawn there after the arena behind it is put away.
        if (obj.Arena != _runtime->CurrentArena && obj.Connects == _runtime->CurrentArena)
        {
            MoveDoor(obj);
        }

        auto state = obj.DoorState;
        if ((state & DoorOpening) != 0)
        {
            if (obj.IsAnimationDone())
            {
                state = (state & DoorOpenKept) | DoorOpen;
                Play(obj, obj.DoorSounds[2]);
            }
        }
        else if ((state & DoorClosing) != 0 && obj.IsAnimationDone())
        {
            state = (state & DoorScriptBits) | DoorClosed;
            Play(obj, obj.DoorSounds[3]);

            // The arena behind it goes.
            _runtime->ShowArena("");
        }

        if (obj.DistanceTo(_runtime->KurtPosition) >= obj.DoorDistance)
        {
            if ((state & (DoorClosing | DoorClosed | DoorStaysOpen)) == 0)
            {
                obj.RestartAnimation(_runtime->GetAnimation(obj, obj.DoorAnimations[1]), MdkObject::Looping::Once);
                state = (state & DoorScriptBits) | DoorClosing;
                Play(obj, obj.DoorSounds[1]);
            }
        }
        else if ((state & (DoorOpen | DoorOpening | DoorLocked)) == 0)
        {
            obj.RestartAnimation(_runtime->GetAnimation(obj, obj.DoorAnimations[0]), MdkObject::Looping::Once);
            state = (state & DoorScriptBits) | DoorOpening;
            Play(obj, obj.DoorSounds[0]);
            ShowBehind(obj);
        }

        obj.DoorState = state;

        // Parts named LOCK show a locked, closed door; parts named HC... are hidden while it's closed.
        auto hidden = obj.HiddenParts;
        if ((state & DoorClosed) == 0)
        {
            hidden = (hidden | obj.LockParts) & ~obj.HatchParts;
        }
        else
        {
            hidden |= obj.HatchParts;
            if ((state & DoorLocked) != 0 && (state & DoorLockHidden) == 0)
            {
                hidden &= ~obj.LockParts;
            }
            else
            {
                hidden |= obj.LockParts;
            }
        }

        obj.HiddenParts = hidden;
        if ((state & DoorOpenNotSolid) == 0)
        {
            return;
        }

        obj.Flags = (state & DoorOpen) != 0 ? obj.Flags | MdkObject::FlagNotSolid : obj.Flags & ~MdkObject::FlagNotSolid;
    }

    void ObjectBehaviors::Shut(MdkObject& door)
    {
        if ((door.DoorState & (DoorClosed | DoorStaysOpen)) != 0)
        {
            return;
        }

        door.RestartAnimation(_runtime->GetAnimation(door, door.DoorAnimations[1]), MdkObject::Looping::Once);
        for (int tick = 0; tick < MaxShutTicks && !door.IsAnimationDone(); tick++)
        {
            door.AdvanceAnimation(Dt);
        }

        door.DoorState = (door.DoorState & DoorScriptBits) | DoorClosed;
    }

    void ObjectBehaviors::MoveDoor(MdkObject& door)
    {
        std::swap(door.Arena, door.Connects);
        door.Yaw += DoorTurn;
        if (door.Yaw >= FullTurn)
        {
            door.Yaw -= FullTurn;
        }
    }

    void ObjectBehaviors::SetupDoor(MdkObject& obj)
    {
        // New doors are closed (0x45cdec, spawn flag 1); starting mid-way they'd close and hide the arena behind.
        obj.DoorState = DoorClosed;
        obj.LockParts = 0;
        obj.HatchParts = 0;
        if (obj.Model == nullptr)
        {
            return;
        }

        for (int i = 0; i < static_cast<int>(obj.Model->PartList.size()); i++)
        {
            const auto& name = obj.Model->PartList[i]->Name;
            if (name == LockPart)
            {
                obj.LockParts |= 1 << i;
            }
            else if (Str::StartsWith(name, HatchPrefix))
            {
                obj.HatchParts |= 1 << i;
            }
        }
    }

    void ObjectBehaviors::UpdatePickup(MdkObject& obj)
    {
        if ((obj.Flags & MdkObject::FlagCollected) != 0)
        {
            obj.ParameterTimer -= 1.0f;
            obj.Scale = MathF::Max(obj.ParameterTimer / ShrinkTicks, 0.0f);
            obj.Yaw = Wrap360(obj.Yaw + Dt * PickupTurnSpeed * CollectedSpin);
            if (obj.ParameterTimer <= 0.0f)
            {
                _runtime->Remove(obj);
            }

            return;
        }

        if ((obj.Flags & (MdkObject::FlagGravity | MdkObject::FlagLanded)) == MdkObject::FlagGravity)
        {
            Fall(obj);
            return;
        }

        // The chute shrinks away once the pickup has landed.
        auto chute = obj.Attached;
        if (chute != nullptr && chute->Scale > ChuteGone)
        {
            chute->Scale -= Dt;
            if (chute->Scale <= ChuteGone)
            {
                _runtime->Remove(*chute);
                obj.Attached = nullptr;
            }
        }

        if (obj.TypeName == Runner && obj.Attached == nullptr)
        {
            UpdateRunner(obj);
        }

        if (std::find(Still.begin(), Still.end(), obj.TypeName) == Still.end())
        {
            obj.Yaw = Wrap360(obj.Yaw + Dt * PickupTurnSpeed);
        }
    }

    void ObjectBehaviors::Fall(MdkObject& obj)
    {
        if ((obj.ContactFlags & MdkObject::ContactFloor) != 0)
        {
            auto hover = obj.TypeName == Runner ? 0.0f : PickupHover;
            obj.HeightOffset = hover;
            obj.Flags |= MdkObject::FlagLanded;
            obj.Position.Z += hover;
            return;
        }

        if (obj.Velocity.Z >= PickupChuteSpeed)
        {
            return;
        }

        obj.Velocity.Z = PickupChuteSpeed;
        if (obj.Attached != nullptr)
        {
            return;
        }

        auto chute = _runtime->Spawn(obj, std::string(Chute), obj.Position, obj.Yaw, -1, 0, ScriptRuntime::Spawning::Plain);
        if (chute == nullptr)
        {
            return;
        }

        chute->Flags = ChuteFlags;
        chute->Leader = obj.shared_from_this();
        chute->AttachPoints = {0, 0};
        chute->MoveCommand = AttachCommand;
        obj.Attached = chute;
    }

    void ObjectBehaviors::UpdateRunner(MdkObject& obj)
    {
        auto run = _runtime->Items->GetAnimation(std::string(Items::HamsterRun));
        auto kurt = _runtime->KurtPosition;
        if (obj.Animation != run)
        {
            if ((obj.Animation == nullptr || obj.IsAnimationDone()) && _runtime->Rng->Next(ChanceRange) < RunnerIdleChance)
            {
                obj.RestartAnimation(_runtime->Items->GetAnimation(std::string(Items::HamsterIdle)), MdkObject::Looping::Once);
                return;
            }

            if (Vector3::DistanceSquared(obj.Position, kurt) < RunnerStart * RunnerStart)
            {
                obj.RestartAnimation(run, MdkObject::Looping::Loop);
                _runtime->Mixer->Play("RUNNER", Audio::SoundMixer::Start::Once);
            }

            return;
        }

        auto ahead = FromAngle(obj.Yaw) * RunnerSpeed;
        obj.Push.X += ahead.X;
        obj.Push.Y += ahead.Y;
        auto away = Heading(obj.Position, kurt) + HalfTurn;
        auto turn = Math::Clamp(WrapAngle(away - obj.Yaw), -RunnerTurn * Dt, RunnerTurn * Dt);
        obj.Yaw = Wrap360(obj.Yaw + turn);
        obj.Flags |= MdkObject::FlagGravity;
        if (Vector2(kurt.X - obj.Position.X, kurt.Y - obj.Position.Y).LengthSquared() > RunnerStopSquared)
        {
            obj.RestartAnimation(_runtime->Items->GetAnimation(std::string(Items::HamsterIdle)), MdkObject::Looping::Once);
        }
    }

    void ObjectBehaviors::UpdateCow(MdkObject& obj)
    {
        auto target = obj.CowTarget != nullptr && !obj.CowTarget->Dead ? obj.CowTarget->Position : _runtime->KurtPosition;
        auto landed = (obj.ContactFlags & MdkObject::ContactFloor) != 0;
        if ((obj.Flags & MdkObject::FlagNotTarget) == 0)
        {
            CowHits(obj);
            if (!landed)
            {
                obj.Position.X = MoveToward(obj.Position.X, target.X, CowSpeed * Dt);
                obj.Position.Y = MoveToward(obj.Position.Y, target.Y, CowSpeed * Dt);
            }
        }

        if (!landed)
        {
            return;
        }

        obj.Flags |= MdkObject::FlagNotTarget;
        obj.ParameterTimer -= Dt;
        if (obj.ParameterTimer < 0.0f)
        {
            _runtime->Kill(obj);
        }
    }

    void ObjectBehaviors::CowHits(MdkObject& obj)
    {
        auto bounds = _runtime->GetWorldBounds(obj);
        auto hit = false;
        auto copy = _runtime->Objects;
        for (auto& other : copy)
        {
            if (other.get() == &obj || other->Dead || other->Arena != obj.Arena || (other->Flags & (MdkObject::FlagNotTarget | MdkObject::FlagNotSolid2)) != 0)
            {
                continue;
            }

            if (!Intersects(bounds, _runtime->GetWorldBounds(*other)))
            {
                continue;
            }

            hit = true;
            other->HitEvent = CowHitEvent;
            other->HitType = CowHitType;
            if (other->Health >= ScriptRuntime::Indestructible)
            {
                continue;
            }

            other->Health -= CowDamage;
            if (other->Health <= 0)
            {
                _runtime->Kill(*other);
            }
        }

        if (Intersects(bounds, _runtime->GetKurtBox()))
        {
            hit = true;
            _runtime->HurtKurt(CowKurtDamage);
        }

        if (hit)
        {
            obj.Flags |= MdkObject::FlagNotTarget;
        }
    }

    void ObjectBehaviors::ShowBehind(MdkObject& obj)
    {
        for (const auto& side : {obj.Connects, obj.Arena})
        {
            if (side.length() == 0 || side == _runtime->CurrentArena || (_runtime->SecondActive && side == _runtime->SecondArena))
            {
                continue;
            }

            _runtime->ShowArena(side);
            return;
        }
    }

    void ObjectBehaviors::Play(MdkObject& obj, const std::string& name)
    {
        if (name.length() != 0 && name != NoSound)
        {
            _runtime->PlaySound(obj, name, 0, Formats::Scripts::Operand{});
        }
    }
}
