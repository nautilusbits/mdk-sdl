#include "Mdk.Game/Kurt/Inventory.h"

#include <algorithm>

namespace Mdk::Game::Kurt
{
    namespace
    {
        // The first slot holding item, or -1 (List.FindIndex).
        int FindSlot(const std::vector<Inventory::Slot>& slots, Inventory::Item item)
        {
            for (size_t i = 0; i < slots.size(); i++)
            {
                if (slots[i].Item == item)
                {
                    return static_cast<int>(i);
                }
            }

            return -1;
        }
    }

    const std::unordered_map<std::string, Inventory::Item>& Inventory::ItemPickups()
    {
        static const std::unordered_map<std::string, Item> pickups =
        {
            {"SW_DUMMY", Item::Dummy},
            {"SW_INTER", Item::InterestingBomb},
            {"SW_TWIST", Item::Tornado},
            {"SW_THUMP", Item::Mortar},
            {"SW_HBOMB", Item::Grenade},
            {"SW_GATT", Item::SuperChainGun},
            {"SW_KEY", Item::Key},
            {"SW_SEAL", Item::Seal},
            {"SW_SBONE", Item::SuperBone},
        };
        return pickups;
    }

    // Whether a model name is a pickup (SW_HBOMB, SW_H50...).
    bool Inventory::IsPickup(const std::string& model)
    {
        bool instant = std::find(InstantPickups.begin(), InstantPickups.end(), model) != InstantPickups.end();
        return instant || ItemPickups().contains(model);
    }

    // The pickups of a full save, as they were.
    void Inventory::Restore(std::span<const Slot> slots, int selected, std::span<const int> ammo, int selectedAmmo, int superChainGun)
    {
        _slots.assign(slots.begin(), slots.end());
        Selected = selected;
        for (size_t i = 0; i < _ammo.size() && i < ammo.size(); i++)
        {
            _ammo[i] = ammo[i];
        }

        SelectedAmmo = selectedAmmo;
        SuperChainGun = superChainGun;
    }

    // Takes a pickup by model name; health pickups change health. Returns
    // the sound to play, or "" when Kurt can't take it (full inventory, not a pickup).
    std::string Inventory::Collect(const std::string& pickup, int& health)
    {
        auto found = std::find(InstantPickups.begin(), InstantPickups.end(), pickup);
        int index = found == InstantPickups.end() ? -1 : static_cast<int>(found - InstantPickups.begin());
        if (index >= 0)
        {
            // SW_H01 and BONEFLC show no message.
            if (index != Health01 && index != BoneFlc && PickedUp)
            {
                PickedUp(pickup);
            }

            return UseInstant(index, health);
        }

        auto item = ItemPickups().find(pickup);
        if (item == ItemPickups().end())
        {
            return "";
        }

        std::string sound = CollectItem(item->second);
        if (!sound.empty() && PickedUp)
        {
            PickedUp(pickup);
        }

        return sound;
    }

    std::string Inventory::CollectItem(Item item)
    {
        // Grenades and the super chain gun stack in their slot.
        int stacked = item == Item::Grenade || item == Item::SuperChainGun ? FindSlot(_slots, item) : -1;
        if (stacked >= 0)
        {
            _slots[stacked] = Add(_slots[stacked], item);
            if (item != Item::SuperChainGun)
            {
                Selected = stacked;
            }

            return CollectSound;
        }

        if (static_cast<int>(_slots.size()) >= MaxSlots)
        {
            return "";
        }

        // The new item is selected, but the super chain gun only in an empty inventory.
        if (item != Item::SuperChainGun || _slots.empty())
        {
            Selected = static_cast<int>(_slots.size());
        }

        _slots.push_back(Add(Slot{item, 0}, item));
        return item == Item::InterestingBomb ? BombSound : CollectSound;
    }

    Inventory::Slot Inventory::Add(Slot slot, Item item)
    {
        switch (item)
        {
            case Item::Grenade:
                slot.Count = slot.Count + Grenades[static_cast<int>(Difficulty)];
                return slot;
            case Item::SuperChainGun:
                SuperChainGun += SuperChainGunTicks[static_cast<int>(Difficulty)];
                slot.Count = 1;
                return slot;
            default:
                slot.Count = 1;
                return slot;
        }
    }

    std::string Inventory::UseInstant(int index, int& health)
    {
        if (index <= LastAmmoPickup)
        {
            int amount = AmmoPerPickup[index];
            if (Difficulty == Game::Kurt::Difficulty::Hard && amount > 1)
            {
                amount /= 2;
            }

            _ammo[index] += amount;
            SelectedAmmo = index + 1;
            return index == BonesPickup ? BonesSound : CollectSound;
        }

        switch (index)
        {
            case Health25:
                health = Heal(health, SmallHealth);
                break;
            case Health50:
                health = Heal(health, BigHealth);
                break;
            case Health100:
                health = Math::Max(health, MaxHealth);
                break;
            case Health150:
                health = Math::Max(health, SuperHealth);
                break;
            case Health01:
                health = Heal(health, 1);
                break;
            case Groovy:
                return CollectSound;
            case BoneFlc:
                return BonesSound;
        }

        return HealthSound;
    }

    // Selects slot index (the item keys 1-5), if there is one.
    void Inventory::Select(int index)
    {
        if (index >= 0 && index < static_cast<int>(_slots.size()))
        {
            Selected = index;
        }
    }

    // Selects the next (step 1) or previous (-1) slot, wrapping around.
    void Inventory::SelectNext(int step)
    {
        if (_slots.empty())
        {
            return;
        }

        int count = static_cast<int>(_slots.size());
        Selected = ((Selected + step) % count + count) % count;
    }

    // A sniper round of type was fired; normal bullets never run out.
    void Inventory::UseAmmo(int type)
    {
        if (type > 0)
        {
            _ammo[type - 1] = Math::Max(_ammo[type - 1] - 1, 0);
        }
    }

    // The next (step 1) or previous (-1) round type with rounds,
    // wrapping around; normal bullets always have some (0x46c900).
    int Inventory::NextAmmo(int step) const
    {
        constexpr int Types = AmmoTypes + 1;
        int type = SelectedAmmo;
        for (int i = 0; i < Types; i++)
        {
            type = ((type + step) % Types + Types) % Types;
            if (type == 0 || _ammo[type - 1] > 0)
            {
                break;
            }
        }

        return type;
    }

    // One of the selected item is used up; an empty slot goes.
    void Inventory::Consume()
    {
        if (_slots.empty())
        {
            return;
        }

        Slot slot = _slots[Selected];
        if (slot.Count > 1)
        {
            slot.Count = slot.Count - 1;
            _slots[Selected] = slot;
            return;
        }

        RemoveSlot(Selected);
    }

    // The super chain gun's time runs (while firing); at 0 its slot goes.
    void Inventory::TickSuperChainGun(int ticks)
    {
        if (SuperChainGun <= 0)
        {
            return;
        }

        SuperChainGun -= ticks;
        if (SuperChainGun > 0)
        {
            return;
        }

        SuperChainGun = 0;
        int index = FindSlot(_slots, Item::SuperChainGun);
        if (index >= 0)
        {
            RemoveSlot(index);
        }
    }

    void Inventory::RemoveSlot(int index)
    {
        _slots.erase(_slots.begin() + index);
        Selected = Math::Clamp(Selected, 0, Math::Max(static_cast<int>(_slots.size()) - 1, 0));
    }
}
