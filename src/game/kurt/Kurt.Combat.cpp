#include "game/kurt/Kurt.h"

// Kurt's health, hits, knock-downs, death, chain gun and items (kurt.gd: hurt, knock_down,
// _update_death, _update_firing, _update_muzzle, _update_items; godot-mdk docs/gameplay.md "Firing",
// "Damage and death").
//   hit ──► health, red flash, knock damage (drains 2/s) ──5──► KNOCKED (K_BANG) ──► GET_UP (K_BFLIP)
//                                                               invulnerable 3 s
//   health 0 ──on the floor──► DEAD (K_BANG) ──skull fades in──► Died
//   fire ──► SHOT │ RUN_FIRE, chain gun loop, muzzle flash; the hits are the scripts runtime's
//   use  ──► THROW (K_SPWEP) ──frame 8──► ItemUsed │ in the air: ItemUsed at once

namespace Mdk::Game::Kurt
{
    using Engine::Platform::Input;
    using Engine::Platform::Key;

    const std::unordered_map<Kurt::State, Kurt::Offset>& Kurt::MuzzleOffsets()
    {
        static const std::unordered_map<State, Offset> offsets = {
            { State::Turn, { 0, 0 } },
            { State::Side, { 0, 0 } },
            { State::Fall, { 40, 6 } },
            { State::Jump, { 0, 0 } },
            { State::RunJump, { 0, -10 } },
            { State::Chute, { 20, 0 } },
        };
        return offsets;
    }

    void Kurt::Hurt(int damage)
    {
        if (Health == 0)
        {
            return;
        }

        if (Invulnerable > 0.f || Current == State::Dead || Current == State::Knocked || Current == State::GetUp
            || Current == State::HardLand)
        {
            KnockDamage = 0.f;
            return;
        }

        Damage(damage);
    }

    void Kurt::Damage(int damage)
    {
        if (Mortality == Game::Kurt::Mortality::God)
        {
            return;
        }

        damage = Scaled(damage, Inventory->Difficulty);
        if (damage > 0)
        {
            HurtFlash = Math::Clamp(HurtFlash + damage * HurtFlashPerDamage, HurtFlashMin, HurtFlashMax);
        }

        Health = Math::Max(Health - damage, 0);
        KnockDamage += damage;
    }

    int Kurt::Scaled(int damage, Difficulty difficulty)
    {
        switch (difficulty)
        {
            case Difficulty::Easy:
                return Math::Max(damage * EasyNumerator / EasyDenominator, 1);
            case Difficulty::Hard:
                return damage * HardFactor;
            default:
                return damage;
        }
    }

    std::string Kurt::Collect(const std::string& pickup)
    {
        auto health = Health;
        auto sound = Inventory->Collect(pickup, health);
        Health = health;
        return sound;
    }

    void Kurt::KnockDown(Vector2 push)
    {
        KnockDamage = 0.f;
        Invulnerable = KnockdownInvulnerability;
        _push += push;
        StopFiring();
        ChuteOpen = false;
        SetState(State::Knocked);
    }

    void Kurt::StopFiring()
    {
        LeaveSniper();
        Firing = false;
        _mixer.StopVoice(_gunVoice);
        _gunVoice = 0;
        Muzzle = std::nullopt;
    }

    void Kurt::Revive()
    {
        Health = MaxHealth;
        HurtFlash = 0.f;
        KnockDamage = 0.f;
        _push = Vector2::Zero();
        SetState(State::Still);
    }

    void Kurt::FadeFlashes(float delta)
    {
        HurtFlash = MathF::Max(HurtFlash - FlashFade * delta, 0.f);
        WhiteFlash = MathF::Max(WhiteFlash - FlashFade * delta, 0.f);
    }

    void Kurt::UpdateKnockDamage(float delta)
    {
        if (Health > 0 && KnockDamage >= KnockdownDamage && !Knocked())
        {
            auto knocked = OnFloor;
            Vector3 point;
            if (!knocked && _space.Floor(Feet, Feet - Vector3(0.f, 0.f, KnockdownFloorDistance), point))
            {
                knocked = true;
                VerticalSpeed = MathF::Min(VerticalSpeed, KnockdownSlamSpeed);
            }

            if (knocked)
            {
                KnockDown(Vector2::Zero());
                return;
            }
        }

        KnockDamage = Math::Clamp(KnockDamage, 0.f, KnockdownDamage);
        KnockDamage = MathF::Max(KnockDamage - KnockdownDrain * delta, 0.f);
    }

    void Kurt::UpdateDeath(float delta)
    {
        if (HurtFlash > SkullEnd)
        {
            return;
        }

        ForwardSpeed = 0.f;
        StrafeSpeed = 0.f;
        VerticalSpeed = OnFloor ? 0.f : MathF::Max(VerticalSpeed - Gravity * delta, -MaxFallSpeed);
        UpdateUpdraft(delta);
        Fall(delta);
        StateTime += delta;
        AnimationFrame = MathF::Min(AnimationFrame + Ticks * delta, static_cast<float>(_frameCount(State::Dead) - 1));
        HurtFlash += SkullFade * delta;
        if (HurtFlash > SkullEnd && Died)
        {
            Died();
        }
    }

    bool Kurt::Pressed(Input& input, Key key)
    {
        if (!input.IsDown(key))
        {
            _held.erase(key);
            return false;
        }

        return _held.insert(key).second;
    }

    void Kurt::UpdateItems(Input& input)
    {
        if (Pressed(input, Key::ItemNext))
        {
            Inventory->SelectNext(1);
        }

        if (Pressed(input, Key::ItemPrevious))
        {
            Inventory->SelectNext(-1);
        }

        for (auto i = 0; i < static_cast<int>(SlotKeys.size()); i++)
        {
            if (Pressed(input, SlotKeys[i]))
            {
                Inventory->Select(i);
            }
        }

        using Item = Game::Kurt::Inventory::Item;
        auto item = Inventory->SelectedItem();
        if (!Pressed(input, Key::UseItem) || Current == State::Throw || Current == State::Dead || Knocked()
            || item == Item::None || item == Item::SuperChainGun)
        {
            return;
        }

        // The bomb is out (or another item): "use" sets the bomb off.
        if (CanUseItem && !CanUseItem(item))
        {
            if (BombTriggered)
            {
                BombTriggered();
            }

            return;
        }

        if (OnFloor)
        {
            SetState(State::Throw);
        }
        else if ((Current == State::Jump || Current == State::RunJump || Current == State::Fall || Current == State::Chute) && ItemUsed)
        {
            ItemUsed();
        }
    }

    void Kurt::UpdateFiring(Input& input)
    {
        auto fire = input.IsDown(Key::Fire) && Health > 0 && Current != State::Throw && !Knocked();
        auto superGun = Inventory->SuperChainGun > 0;
        if (fire == Firing && (!Firing || superGun == _gunSuper))
        {
            return;
        }

        StopFiring();
        Firing = fire;
        _gunSuper = superGun;
        if (Firing)
        {
            _gunVoice = _mixer.PlayLooped(superGun ? SuperGunSound : GunSound);
        }
    }

    void Kurt::UpdateMuzzle(float delta)
    {
        _muzzleTicks += Ticks * delta;
        auto tick = ToInt(_muzzleTicks);
        auto offset = Firing ? MuzzleOffset() : std::nullopt;
        if (!offset || (tick & 1) == 0)
        {
            Muzzle = std::nullopt;
            return;
        }

        if (tick == _muzzleTick)
        {
            return;
        }

        _muzzleTick = tick;
        _muzzleFrame = (_muzzleFrame + _random.Next(MuzzleFrameMask) + 1) & MuzzleFrameMask;
        // C# evaluates the arguments left to right: X's jitter first.
        auto x = offset->X + _random.Next(MuzzleJitter);
        auto y = offset->Y + _random.Next(MuzzleJitter);
        Muzzle = MuzzleFlash{ _muzzleFrame, x, y };
    }

    std::optional<Kurt::Offset> Kurt::MuzzleOffset() const
    {
        if (Pose && Pose->Name == SurfPose)
        {
            return SurfMuzzle;
        }

        auto found = MuzzleOffsets().find(Current);
        if (found == MuzzleOffsets().end())
        {
            return std::nullopt;
        }

        return found->second;
    }

    bool Kurt::UpdateCombatState(float delta)
    {
        if (Health == 0 && OnFloor)
        {
            // Dead: the death animation once on the floor (state 1002).
            StopFiring();
            HurtFlash = 0.f;
            SetState(State::Dead);
            return true;
        }

        if (Current == State::Knocked)
        {
            // K_BANG, then K_BFLIP, one frame per tick; the push stops when he flips back up.
            AnimationFrame += Ticks * delta;
            auto bang = _frameCount(State::Knocked);
            if (AnimationFrame < bang)
            {
                return true;
            }

            auto flip = AnimationFrame - bang;
            _push = Vector2::Zero();
            SetState(State::GetUp);
            AnimationFrame = flip;
            return true;
        }

        if (Current == State::GetUp)
        {
            AnimationFrame += Ticks * delta;
            if (AnimationFrame < _frameCount(State::GetUp))
            {
                return true;
            }

            SetState(State::Still);
            return false;
        }

        if (Current != State::Throw || !OnFloor || AnimationFrame >= _frameCount(State::Throw) - 1)
        {
            return false;
        }

        // Kurt stands still while throwing; the item leaves his hand on frame 8.
        auto previous = ToInt(AnimationFrame);
        AnimationFrame += Ticks * delta;
        if (previous < ThrowFrame && ToInt(AnimationFrame) >= ThrowFrame && ItemUsed)
        {
            ItemUsed();
        }

        return true;
    }
}
