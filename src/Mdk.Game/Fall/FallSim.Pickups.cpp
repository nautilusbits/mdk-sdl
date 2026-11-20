#include "Mdk.Game/Fall/FallSim.h"

#include <algorithm>

// The pickups (0x4128fc, 0x41275c) of FALLPU_n, dropped one by one from above Kurt
// (the list's last first): they fall at twice his speed, open a chute after 1-3 seconds and brake
// to 50 u/s, spinning. Kurt takes the ones that cross his box until 30 s.
namespace Mdk::Game::Fall
{
    namespace
    {
        // C#'s List.Remove by reference: the first entry holding item.
        void RemovePickup(std::vector<std::shared_ptr<FallSim::Pickup>>& list, const FallSim::Pickup* item)
        {
            auto it = std::find_if(list.begin(), list.end(), [item](const auto& entry) { return entry.get() == item; });
            if (it != list.end())
            {
                list.erase(it);
            }
        }
    }

    void FallSim::StartPickups()
    {
        if (!_pickupNames.empty())
        {
            _pickupWait = _random->Next(PickupFirstRandom) + PickupWait;
        }
    }

    void FallSim::UpdatePickups(float dt, int ticks)
    {
        if (!_pickupNames.empty() && Time <= SteerTime)
        {
            _pickupWait -= ticks;
            if (_pickupWait <= 0)
            {
                _pickupWait = PickupWait + _random->Next(PickupNextRandom);
                Spawn(_pickupNames.back());
                _pickupNames.pop_back();
            }
        }

        auto pickups = _pickups;
        for (const auto& pickup : pickups)
        {
            auto previous = pickup->Position;
            MovePickup(*pickup, dt, ticks);

            // Only under its chute (0x41275c): dropped above the camera, it would go at once.
            if (!pickup->Chute)
            {
                continue;
            }

            if (pickup->Position.Z > CameraPosition.Z)
            {
                RemovePickup(_pickups, pickup.get());
            }
            else if (!Dying && Time <= SteerTime && CrossesBox(KurtPosition, previous, pickup->Position))
            {
                Take(*pickup);
            }
        }
    }

    void FallSim::MovePickup(Pickup& pickup, float dt, int ticks)
    {
        pickup.Ticks += ticks;
        if (!pickup.Chute && pickup.Ticks >= pickup.FallTicks)
        {
            if (Sound)
            {
                Sound("P_CHUTE");
            }

            pickup.Chute = true;
        }

        if (pickup.Chute)
        {
            pickup.Velocity.Z = MathF::Min(pickup.Velocity.Z + KurtSpeed * dt, -PickupChuteSpeed);
            pickup.Yaw += PickupSpin * dt;
        }

        pickup.Position += pickup.Velocity * dt;
    }

    void FallSim::Take(Pickup& pickup)
    {
        // C#'s Sound?.Invoke(...) skips the random draw without a listener.
        if (Sound)
        {
            Sound("P_COLL");
            Sound(CollectSounds[_random->Next(static_cast<int>(CollectSounds.size()))]);
        }

        auto health = Health;
        Inventory->Collect(pickup.Name, health);
        Health = health;
        _collected.push_back(pickup.Name);
        RemovePickup(_pickups, &pickup);
    }

    void FallSim::Spawn(const std::string& name)
    {
        if (Sound)
        {
            Sound("P_FALL");
        }

        // x first: C++ leaves the order of constructor arguments open.
        auto x = RandomRange(PickupSpread * Limit().X);
        auto y = RandomRange(PickupSpread * Limit().Y);
        auto position = Vector3(x, y, KurtPosition.Z + PickupAbove);
        auto fallTicks = ChuteTicks + _random->Next(ChuteRandom);
        _pickups.push_back(std::make_shared<Pickup>(name, position, fallTicks));
    }
}
