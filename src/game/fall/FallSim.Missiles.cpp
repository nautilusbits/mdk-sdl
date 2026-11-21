#include "game/fall/FallSim.h"

#include <algorithm>

// The missiles (0x412568, 0x411ee8) launched when a radar sees Kurt, their hits and
// explosions.
namespace Mdk::Game::Fall
{
    namespace
    {
        // C#'s List.Remove by reference: the first entry holding item.
        template <typename T>
        void RemoveItem(std::vector<std::shared_ptr<T>>& list, const T* item)
        {
            auto it = std::find_if(list.begin(), list.end(), [item](const auto& entry) { return entry.get() == item; });
            if (it != list.end())
            {
                list.erase(it);
            }
        }
    }

    void FallSim::UpdateMissiles(float dt, int ticks)
    {
        if (_missileQueue > 0)
        {
            _missileWait -= ticks;
            if (_missileWait <= 0)
            {
                _missileQueue--;
                _missileWait = MissileGap + _random->Next(MissileGapRandom);
                Launch();
            }
        }

        auto missiles = _missiles;
        for (const auto& missile : missiles)
        {
            auto previous = missile->Position;
            auto happened = missile->Update(dt, ticks, KurtPosition);
            if (happened == FallMissile::Event::Passed && Sound)
            {
                Sound("M_PASS");
            }

            if (happened == FallMissile::Event::Gone)
            {
                RemoveItem(_missiles, missile.get());
                continue;
            }

            if (Time <= SteerTime && !Dying && CrossesBox(KurtPosition, previous, missile->Position))
            {
                Hit(*missile);
            }
        }
    }

    void FallSim::Launch()
    {
        if (Sound)
        {
            Sound("M_LNCH");
        }

        _rate = FlashRate;
        _target = MathF::Max(_target - LaunchDim, LaunchFloor);
        _missiles.push_back(std::make_shared<FallMissile>(Spread, *_random));
    }

    void FallSim::Hit(FallMissile& missile)
    {
        // C#'s Sound?.Invoke(...) skips the random draw without a listener.
        if (Sound)
        {
            Sound(ExplodeSounds[_random->Next(static_cast<int>(ExplodeSounds.size()))]);
            Sound(HitSounds[_random->Next(static_cast<int>(HitSounds.size()))]);
        }

        int damage;
        switch (_setup.Difficulty)
        {
            case Kurt::Difficulty::Normal:
                damage = HitDamage + _random->Next(HitDamageRandom);
                break;
            case Kurt::Difficulty::Hard:
                damage = HardFactor * (HitDamage + _random->Next(HitDamageRandom));
                break;
            default:
                damage = HitDamage;
                break;
        }

        Health = Math::Max(Health - damage, 0);
        _brightness = HitBrightness;
        KurtPose = KurtAnimation::Hit;
        KurtFrame = 0.f;
        // The explosion goes off on Kurt (0x411ee8 copies his position), not where the missile met
        // his box, up to 4 units to his side.
        _explosions.push_back(std::make_shared<Explosion>(KurtPosition, _random->NextSingle() * MathF::Tau));
        RemoveItem(_missiles, &missile);
        if (Health > 0)
        {
            return;
        }

        Dying = true;
        _brightness = HitBrightness;
        KurtPose = KurtAnimation::Fall;
    }

    void FallSim::UpdateExplosions(int ticks)
    {
        auto explosions = _explosions;
        for (const auto& explosion : explosions)
        {
            explosion->Ticks += ticks;
            if (explosion->Ticks >= ExplosionTicks)
            {
                RemoveItem(_explosions, explosion.get());
                continue;
            }

            explosion->Position.Z = KurtPosition.Z;
        }
    }
}
