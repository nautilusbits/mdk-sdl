#include "game/scripts/Items.h"

#include <algorithm>

#include "core/DotNet.h"
#include "formats/Bni.h"
#include "formats/ModelAnimation.h"
#include "game/audio/SoundMixer.h"
#include "game/kurt/Inventory.h"
#include "game/kurt/Kurt.h"
#include "game/objects/MdkObject.h"
#include "game/scripts/GameStats.h"
#include "game/scripts/ObjectBehaviors.h"
#include "game/scripts/ScriptMath.h"
#include "game/scripts/ScriptRuntime.h"
#include "game/scripts/Twister.h"

namespace Mdk::Game::Scripts
{
    using namespace ScriptMath;
    using Objects::MdkObject;
    using KurtState = Game::Kurt::Kurt::State;

    void Items::Forget(MdkObject& obj)
    {
        if (&obj == Bomb.get())
        {
            Bomb = nullptr;
        }

        if (&obj == Decoy.get())
        {
            Decoy = nullptr;
        }
    }

    std::vector<std::shared_ptr<Formats::ModelAnimation>> Items::Animations()
    {
        std::vector<std::shared_ptr<Formats::ModelAnimation>> animations;
        for (auto name : {DecoyThrown, DecoyWalk, BombAnimation, MortarAnimation, Nuke, HamsterRun, HamsterIdle})
        {
            auto animation = GetAnimation(std::string(name));
            if (animation != nullptr)
            {
                animations.push_back(animation);
            }
        }

        return animations;
    }

    std::shared_ptr<Formats::ModelAnimation> Items::GetAnimation(const std::string& name)
    {
        std::shared_ptr<Formats::ModelAnimation> animation;
        if (!_animations.TryGetValue(name, animation))
        {
            animation = _sprites->Has(name) ? Formats::ModelAnimation::Parse(name, _sprites->Bytes(), _sprites->Entries().At(name).Offset) : nullptr;
            _animations[name] = animation;
        }

        return animation;
    }

    bool Items::CanUse(Item item)
    {
        if (Bomb != nullptr && !Bomb->Dead)
        {
            return false;
        }

        auto thrown = false;
        for (const auto& o : _runtime->Objects)
        {
            if (!o->Dead && o->ThrownKind > 0 && !IsDecoyKind(static_cast<Item>(o->ThrownKind)))
            {
                thrown = true;
                break;
            }
        }

        return !thrown || IsDecoyKind(item);
    }

    bool Items::IsDecoyKind(Item item)
    {
        return item == Item::Dummy || item == Item::Seal || item == Item::SuperBone;
    }

    void Items::TriggerBomb()
    {
        if (Bomb != nullptr && !Bomb->Dead && Bomb->AnimationEndFrame >= 0)
        {
            Bomb->AnimationEndFrame = MdkObject::NoHoldFrame;
        }
    }

    void Items::UseItem()
    {
        auto& kurt = *_runtime->Kurt;
        auto item = kurt.Inventory->SelectedItem();
        if (item == Item::None || _runtime->CurrentArena.empty())
        {
            return;
        }

        auto yaw = Wrap360(kurt.Yaw);
        auto controller = _runtime->GetArenaState(_runtime->CurrentArena)->Controller;
        auto thrown = _runtime->Spawn(*controller, std::string(Models[static_cast<int>(item)]), kurt.Feet + Vector3(0.0f, 0.0f, ThrowHeight), yaw, -1, 0,
            ScriptRuntime::Spawning::Plain);
        if (thrown == nullptr)
        {
            return;
        }

        thrown->Flags |= ThrownFlags;
        thrown->ThrownKind = static_cast<int>(item);
        thrown->ItemTicks = item == Item::Seal || item == Item::SuperBone ? LongFlightTicks : FlightTicks;
        thrown->Friction = 0.0f;
        thrown->Scale = ThrowScale;
        auto direction = FromAngle(yaw) * (item == Item::Grenade ? ThrowSpeed * GrenadeSpeedFactor : ThrowSpeed);
        thrown->Velocity = Vector3(direction, kurt.Current == KurtState::Chute ? 0.0f : ThrowUpSpeed);
        if (item == Item::Dummy)
        {
            thrown->RestartAnimation(GetAnimation(std::string(DecoyThrown)), MdkObject::Looping::Once);
            thrown->AnimationEndFrame = 0;
        }

        kurt.Inventory->Consume();
    }

    void Items::UpdateThrown(MdkObject& obj)
    {
        // A seal or bone Kurt took back shrinks away like a pickup.
        if ((obj.Flags & MdkObject::FlagCollected) != 0)
        {
            _runtime->Behaviors->UpdatePickup(obj);
            return;
        }

        if ((obj.Flags & FlagActive) != 0)
        {
            UpdateActive(obj);
            return;
        }

        obj.Scale = MathF::Min(obj.Scale + ScriptRuntime::Tick * GrowRate, 1.0f);
        if (obj.ThrownKind == static_cast<int>(Item::Seal))
        {
            obj.Roll = Wrap360(obj.Roll + ScriptRuntime::Tick * SealRollRate);
        }

        if (obj.ThrownKind == KindBomb)
        {
            obj.Pitch = MathF::Max(obj.Pitch - BombPitchRate * ScriptRuntime::Tick, BombPitchMin);
        }

        if (HitsObject(obj))
        {
            obj.ContactFlags |= ContactHitObject;
        }

        obj.ItemTicks--;
        constexpr int Stopped = MdkObject::ContactCollided | MdkObject::ContactFloor | ContactHitObject;
        if ((obj.ContactFlags & Stopped) == 0 && obj.ItemTicks > 0)
        {
            return;
        }

        // The mortar only works on a floor.
        if (obj.ThrownKind == static_cast<int>(Item::Mortar) && (obj.ContactFlags & MdkObject::ContactFloor) == 0)
        {
            return;
        }

        Activate(obj);
    }

    bool Items::HitsObject(MdkObject& obj)
    {
        constexpr int Excluded = 0x810;
        constexpr int BombExcluded = 0x830;
        constexpr int Catches = 0x1000000;
        auto excluded = obj.ThrownKind == KindBomb ? BombExcluded : Excluded;
        auto needed = obj.ThrownKind == static_cast<int>(Item::Grenade) || obj.ThrownKind == KindBomb ? 0 : Catches;
        for (const auto& otherRef : _runtime->Objects)
        {
            auto& other = *otherRef;
            if (&other == &obj || other.Dead || other.Health == 0 || other.Arena != obj.Arena || (other.Flags & excluded) != 0
                || (needed != 0 && (other.Flags & needed) == 0))
            {
                continue;
            }

            auto bounds = _runtime->GetWorldBounds(other);
            if (Contains(bounds, obj.Position) || SegmentEntry(bounds, obj.PreviousPosition, obj.Position))
            {
                return true;
            }
        }

        return false;
    }

    void Items::Activate(MdkObject& obj)
    {
        constexpr int Moving = MdkObject::FlagGravity | MdkObject::FlagCollides;
        obj.Velocity = Vector3::Zero();
        obj.Flags = (obj.Flags & ~Moving) | FlagActive;
        obj.ContactFlags &= ~(MdkObject::ContactCollided | MdkObject::ContactFloor | ContactHitObject);
        switch (obj.ThrownKind)
        {
            case static_cast<int>(Item::Grenade):
            case KindBomb:
                Explode(obj, GrenadeDamage, GrenadeKurtDamage, GrenadeRadius, HitGrenade, &obj, GrenadeExplosion);
                break;
            case static_cast<int>(Item::Dummy):
            {
                // The decoy walks forward for 15 seconds, and aliens aim at it.
                obj.ItemTicks = DecoyTicks;
                obj.Velocity = Vector3(FromAngle(obj.Yaw) * DecoySpeed, 0.0f);
                obj.Flags |= Moving;
                obj.RestartAnimation(GetAnimation(std::string(DecoyWalk)), MdkObject::Looping::Loop);
                _runtime->SetLoopSound(obj, "DUMMY");
                Decoy = obj.shared_from_this();
                break;
            }
            case static_cast<int>(Item::InterestingBomb):
                // It spins on its first frame for 20 seconds (or until Kurt sets it off), then plays
                // its animation and blows up.
                obj.RestartAnimation(GetAnimation(std::string(BombAnimation)), MdkObject::Looping::Once);
                obj.AnimationEndFrame = 0;
                obj.ItemTicks = BombTicks;
                Bomb = obj.shared_from_this();
                break;
            case static_cast<int>(Item::Tornado):
                _twisters.push_back(std::make_shared<Twister>(*_runtime, obj.Arena, obj.Position, 0.0f));
                obj.ItemTicks = TornadoTicks;
                obj.Parameter = TornadoFirstTwister;
                _runtime->Mixer->Play("TORNADO", Audio::SoundMixer::Start::Once);
                break;
            case static_cast<int>(Item::Mortar):
                obj.RestartAnimation(GetAnimation(std::string(MortarAnimation)), MdkObject::Looping::Once);
                obj.ItemTicks = MortarActiveTicks;
                obj.Parameter = 0.0f;
                UpdateMortar(obj);
                break;
            case static_cast<int>(Item::Key):
                StartNuke(obj);
                break;
        }
    }

    void Items::Explode(MdkObject& obj, int damage, int kurtDamage, float radius, int hitType, MdkObject* source, float scale)
    {
        auto center = obj.Position;
        Blast(center, damage, radius, TargetWorld, hitType, source);
        Blast(center, kurtDamage, radius, TargetKurt, hitType, source);
        _runtime->SpawnExplosion(obj.Arena, center, scale);
        _runtime->PlaySoundAt(std::string(ScriptRuntime::ExplodeSound), center);
        _runtime->Remove(obj);
    }

    void Items::StartNuke(MdkObject& obj)
    {
        auto model = _runtime->FindModel(obj.Arena, std::string(Nuke));
        if (model != nullptr)
        {
            obj.TypeName = Nuke;
            obj.Model = model;
        }

        obj.ItemTicks = NukeTicks;
        obj.RestartAnimation(GetAnimation(std::string(Nuke)), MdkObject::Looping::Once);
        obj.AnimationTime = 1.0f;
        _runtime->Kurt->WhiteFlash = MathF::Max(_runtime->Kurt->WhiteFlash, static_cast<float>(UINT8_MAX));

        // A loop without position, until the nuke goes.
        obj.LoopSound = _runtime->Mixer->Play("NUKE");
    }

    void Items::UpdateActive(MdkObject& obj)
    {
        switch (obj.ThrownKind)
        {
            case static_cast<int>(Item::Tornado):
                UpdateTornado(obj);
                break;
            case static_cast<int>(Item::Mortar):
                UpdateMortar(obj);
                break;
            case static_cast<int>(Item::Key):
                UpdateNuke(obj);
                break;
            case static_cast<int>(Item::Seal):
            case static_cast<int>(Item::SuperBone):
                UpdateSeal(obj);
                break;
            case static_cast<int>(Item::InterestingBomb):
                UpdateBomb(obj);
                break;
            default:
                // The decoy walks until its time is up.
                obj.ItemTicks--;
                if (obj.ItemTicks > 0)
                {
                    return;
                }

                Forget(obj);
                _runtime->Remove(obj);
                break;
        }
    }

    void Items::UpdateTornado(MdkObject& obj)
    {
        obj.Yaw = Wrap360(obj.Yaw + ScriptRuntime::Tick * TornadoSpin);
        obj.ItemTicks--;
        if (obj.ItemTicks < obj.Parameter)
        {
            obj.Parameter -= TornadoTwisterEvery;
            _twisters.push_back(std::make_shared<Twister>(*_runtime, obj.Arena, obj.Position, obj.Yaw));
        }

        if (obj.ItemTicks <= 0)
        {
            _runtime->Kill(obj);
        }
    }

    void Items::UpdateBomb(MdkObject& obj)
    {
        Bomb = obj.shared_from_this();
        if (obj.AnimationEndFrame == 0)
        {
            obj.Yaw = Wrap360(obj.Yaw + ScriptRuntime::Tick * BombSpin);
            obj.ItemTicks--;
            if (obj.ItemTicks < 1)
            {
                obj.AnimationEndFrame = MdkObject::NoHoldFrame;
            }

            return;
        }

        if (!obj.IsAnimationDone())
        {
            return;
        }

        Explode(obj, BombDamage, BombKurtDamage, BombRadius, HitBomb, nullptr, BigExplosion);
        Bomb = nullptr;
    }

    void Items::UpdateTwisters()
    {
        auto copy = _twisters;
        for (const auto& twister : copy)
        {
            if (twister->Arena != _runtime->CurrentArena || twister->Tick(_twisters))
            {
                continue;
            }

            auto it = std::find(_twisters.begin(), _twisters.end(), twister);
            if (it != _twisters.end())
            {
                _twisters.erase(it);
            }
        }
    }

    void Items::UpdateMortar(MdkObject& obj)
    {
        if (obj.Animation == nullptr || obj.IsAnimationDone())
        {
            _runtime->Kill(obj);
            return;
        }

        auto thump = ToInt(obj.Parameter);
        if (thump >= static_cast<int>(MortarThumps.size()) || obj.AnimationFrame < MortarThumps[thump])
        {
            return;
        }

        thump++;
        obj.Parameter = static_cast<float>(thump);
        if (thump >= MortarKnockThump && _runtime->Kurt->OnFloor)
        {
            _runtime->Kurt->KnockDamage = Game::Kurt::Kurt::KnockdownDamage;
        }

        _runtime->RaiseShake(MortarShake);
        auto copy = _runtime->Objects;
        for (const auto& otherRef : copy)
        {
            auto& other = *otherRef;
            if (&other == &obj || other.Dead || other.Arena != obj.Arena || (other.Flags & MortarIgnored) != 0
                || std::find(MortarSpared.begin(), MortarSpared.end(), Str::ToUpper(other.TypeName)) != MortarSpared.end())
            {
                continue;
            }

            if ((other.ContactFlags & MdkObject::ContactFloor) != 0)
            {
                other.Velocity.Z += MortarLift;
            }

            auto direction = Heading(_runtime->KurtPosition, Center(_runtime->GetWorldBounds(other)));
            if (other.Health < ScriptRuntime::Indestructible)
            {
                other.Health -= _runtime->KurtDamage(other, MortarDamage);
            }

            other.HitEvent = -1;
            other.HitType = HitMortar;
            other.HitDirection = direction;
            if (other.Health <= 0)
            {
                _runtime->Kill(other, direction + HalfTurn);
            }
        }
    }

    void Items::UpdateNuke(MdkObject& obj)
    {
        if (obj.Animation != nullptr && !obj.IsAnimationDone())
        {
            _runtime->RaiseShake(NukeShake);
            if (obj.AnimationFrame > NukeWhiteFrame)
            {
                auto flash = MathF::Round((obj.AnimationFrame - NukeWhiteFrame) * static_cast<float>(UINT8_MAX)
                    / Math::Max(obj.Animation->FrameCount - NukeWhiteFrame, 1));
                _runtime->Kurt->WhiteFlash = MathF::Max(_runtime->Kurt->WhiteFlash, flash);
            }

            return;
        }

        auto center = obj.Position;
        for (const auto& other : _runtime->Objects)
        {
            if (!other->Dead && (other->Flags & MdkObject::FlagDoor) != 0 && Vector3::DistanceSquared(other->Position, center) < NukeDoorRangeSquared)
            {
                other->DoorState = (other->DoorState & DoorStateKeep) | NukeDoorOpen;
            }
        }

        Blast(center, NukeDamage, NukeRadius, TargetWorld, HitNuke, nullptr);
        Blast(center, NukeKurtDamage, NukeRadius, TargetKurt, HitNuke, nullptr);
        _runtime->PlaySoundAt(std::string(ScriptRuntime::ExplodeSound), center);
        _runtime->SparkFire(obj.Arena, center);
        _runtime->SpawnExplosion(obj.Arena, center, BigExplosion);
        _runtime->Remove(obj);
    }

    void Items::UpdateSeal(MdkObject& obj)
    {
        if ((obj.ScriptFlags & SealHeld) == 0)
        {
            if (obj.ItemTicks < SealPickupTicks)
            {
                obj.Flags |= MdkObject::FlagPickup;
            }

            if (obj.ThrownKind == static_cast<int>(Item::Seal))
            {
                obj.ItemTicks--;
                if (obj.ItemTicks < 1)
                {
                    obj.Scale *= SealShrink;
                    if (obj.Scale < SealGone)
                    {
                        _runtime->Kill(obj);
                    }

                    return;
                }
            }
            else if (obj.ItemTicks >= SealPickupTicks)
            {
                obj.ItemTicks--;
            }
        }
        else
        {
            obj.Scale = 1.0f;
            obj.Flags &= ~MdkObject::FlagPickup;
        }

        if (obj.Animation != nullptr || (obj.ScriptFlags & SealLanded) != 0)
        {
            return;
        }

        obj.Roll = 0.0f;
        obj.ScriptFlags |= SealLanded;
        obj.Flags |= MdkObject::FlagGravity | MdkObject::FlagCollides;
        obj.Flags &= ~MdkObject::FlagLoop;
        obj.RestartAnimation(_runtime->FindArenaAnimation(obj.Arena, std::string(SealLanding)), MdkObject::Looping::Once);
    }

    void Items::Blast(Vector3 center, int damage, float radius, int targets, int hitType, MdkObject* source, Kills kills)
    {
        if ((targets & TargetObjects) != 0)
        {
            BlastObjects(center, damage, radius, hitType, source, kills);
        }

        if ((targets & TargetKurt) != 0)
        {
            BlastKurt(center, damage, radius);
        }

        if ((targets & TargetGroups) != 0)
        {
            BlastGroups(center, damage, radius, hitType, kills);
        }
    }

    void Items::BlastObjects(Vector3 center, int damage, float radius, int hitType, MdkObject* source, Kills kills)
    {
        constexpr int BlastEvent = -2;
        auto copy = _runtime->Objects;
        for (const auto& objRef : copy)
        {
            auto& obj = *objRef;
            if (obj.Dead || obj.Arena != _runtime->CurrentArena || obj.Health == 0
                || (obj.Flags & (MdkObject::FlagNotSolid | MdkObject::FlagNotTarget)) != 0)
            {
                continue;
            }

            auto [best, distance, point, hitEvent] = BlastWeakParts(obj, center, damage, radius, kills);
            if (&obj == source)
            {
                // The grenade itself takes it all.
                best = damage;
                distance = 0.0f;
                point = Center(_runtime->GetWorldBounds(obj));
            }
            else
            {
                auto box = _runtime->GetWorldBounds(obj);
                auto share = BlastDamage(box, center, damage, radius);
                if (share.Damage > best)
                {
                    best = share.Damage;
                    distance = share.Distance;
                    point = Center(box);
                }
            }

            if (best <= 0 || distance > obj.BlastRange)
            {
                continue;
            }

            auto direction = Heading(center, point);
            if (obj.Health < ScriptRuntime::Indestructible)
            {
                obj.Health -= kills == Kills::Counted ? _runtime->KurtDamage(obj, best) : best;
            }

            obj.HitEvent = hitEvent == 0 ? BlastEvent : hitEvent;
            obj.HitType = hitType;
            obj.HitDirection = direction;
            if (obj.Health >= 1)
            {
                continue;
            }

            if (kills == Kills::Counted)
            {
                _runtime->Stats->CountEnemy(obj.TypeName, GameStats::Kill::Killed);
            }

            _runtime->Kill(obj, direction + HalfTurn);
        }
    }

    Items::WeakPartHit Items::BlastWeakParts(MdkObject& obj, Vector3 center, int damage, float radius, Kills kills)
    {
        WeakPartHit result{0, 0.0f, obj.Position, 0};
        if ((obj.Flags & MdkObject::FlagWeakParts) == 0 || obj.Model == nullptr)
        {
            return result;
        }

        auto partBounds = obj.PartBounds();
        for (int i = 0; i < static_cast<int>(obj.Model->PartList.size()); i++)
        {
            if ((obj.HiddenParts & (1 << (i & 31))) != 0 || !ScriptRuntime::IsWeakPart(obj, i) || i >= static_cast<int>(obj.PartHealth.size())
                || !partBounds[i])
            {
                continue;
            }

            auto box = _runtime->GetWorldBounds(obj, *partBounds[i]);
            auto [amount, distance] = BlastDamage(box, center, damage, radius);
            if (amount <= 0)
            {
                continue;
            }

            if (amount > result.Best)
            {
                result = {amount, distance, Center(box), result.Event};
            }

            obj.PartHealth[i] -= BlastHit(obj, amount, obj.PartHealth[i], kills);
            if (obj.PartHealth[i] < 1)
            {
                obj.PartHealth[i] = 0;
                result.Event = i + 1;
            }
        }

        return result;
    }

    void Items::BlastKurt(Vector3 center, int damage, float radius)
    {
        auto kurtPoint = _runtime->KurtPosition + Vector3(0.0f, 0.0f, BlastLift);
        auto distance = Vector3::Distance(center, kurtPoint);
        if (_runtime->Raycast(center + Vector3(0.0f, 0.0f, BlastLift), kurtPoint))
        {
            distance = radius + 1.0f;
        }

        distance *= BlastKurtDistance;
        if (distance >= radius)
        {
            return;
        }

        _runtime->Kurt->Hurt(Math::Min(ScriptMath::Round(damage * (1.0f - distance / radius)), BlastKurtMax));
        _runtime->Kurt->KnockDamage *= 2.0f;
    }

    void Items::BlastGroups(Vector3 center, int damage, float radius, int hitType, Kills kills)
    {
        auto kind = kills == Kills::Counted ? ScriptRuntime::HitBlast : ScriptRuntime::HitOtherBlast;
        auto arena = _runtime->CurrentArena;
        auto state = _runtime->GetArenaState(arena);
        auto origin = center + Vector3(0.0f, 0.0f, BlastLift);
        for (int i = 0; i < ScriptRuntime::GroupCount; i++)
        {
            if (state->GroupHitFlags[i] == 0 && state->GroupHitScripts[i] == 0)
            {
                continue;
            }

            for (auto triangle : _runtime->GroupCenters(arena, i + 1))
            {
                if (Vector3::DistanceSquared(origin, triangle) > radius * radius)
                {
                    continue;
                }

                // Another group in the way stops it; the group's own triangle in the way is the hit.
                auto point = triangle;
                auto hit = _runtime->Raycast(origin, triangle);
                if (hit)
                {
                    if (hit->Group != i + 1)
                    {
                        continue;
                    }

                    point = hit->Point;
                }

                auto distance = Vector3::Distance(origin, point);
                _runtime->HitGroup(arena, i + 1, ScriptMath::Round(damage * (radius - distance) / radius), kind, hitType);
                break;
            }
        }
    }

    int Items::BlastHit(MdkObject& obj, int damage, int health, Kills kills)
    {
        return kills == Kills::Counted ? _runtime->KurtDamage(obj, damage, health) : damage;
    }

    Items::BlastShare Items::BlastDamage(const Formats::Box& box, Vector3 center, int damage, float radius)
    {
        auto size = Size(box).Length();
        auto target = Center(box);

        // Lift the blast off the floor it may lie on, so the wall test doesn't hit that floor.
        center += Vector3(0.0f, 0.0f, BlastLift);
        auto squared = Vector3::DistanceSquared(center, target) - size * size * 0.25f;
        if (squared > radius * radius || _runtime->Raycast(center, target))
        {
            return {0, 0.0f};
        }

        auto distance = MathF::Sqrt(MathF::Max(squared, 0.0f));
        return {ScriptMath::Round(damage * (1.0f - distance / radius)), distance};
    }
}
