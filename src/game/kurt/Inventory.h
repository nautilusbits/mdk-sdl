#pragma once

#include <array>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/DotNet.h"

namespace Mdk::Game::Kurt
{
    // The game's difficulty (0x57423e).
    enum class Difficulty { Easy, Normal, Hard };

    // Kurt's inventory (0x57432c, 5 slots) and sniper ammo (0x5743f3), filled by the pickups
    // he runs through (damp_collect_pickups). A port of godot-mdk's kurt/inventory.gd (see its
    // docs/gameplay.md "Pickups").
    //   pickup ──► instant? ──► ammo (+8, 3, 3, 8, 1) │ health │ easter egg
    //          └─► item ────► grenades, super chain gun: stack in their slot
    //                     └─► others: a new slot (at most 5), selected
    class Inventory
    {
    public:
        // Item types, by pickup model (table 0x4921ac).
        enum class Item { None, Dummy, InterestingBomb, Tornado, Mortar, Grenade, SuperChainGun, Key, Seal, SuperBone };

        struct Slot
        {
            Inventory::Item Item = Item::None;
            int Count = 0;

            bool operator==(const Slot&) const = default;
        };

        static constexpr int MaxSlots = 5;
        static constexpr int AmmoTypes = 5;
        static constexpr int MaxHealth = 100;

        Game::Kurt::Difficulty Difficulty = Game::Kurt::Difficulty::Normal;
        const std::vector<Slot>& Slots() const { return _slots; }
        int Selected = 0;
        // Sniper ammo per round type.
        std::span<const int> Ammo() const { return _ammo; }
        // The selected round type: 1-5, 0 normal bullets.
        int SelectedAmmo = 0;
        // Ticks of super chain gun left (0x5743ef).
        int SuperChainGun = 0;
        // The selected item, or Item::None.
        Item SelectedItem() const { return _slots.empty() ? Item::None : _slots[Selected].Item; }

        // A pickup was taken: its name, the text shown for 2 seconds (0x46d098).
        std::function<void(const std::string&)> PickedUp;

        // Whether a model name is a pickup (SW_HBOMB, SW_H50...).
        static bool IsPickup(const std::string& model);

        // The pickups of a full save, as they were.
        void Restore(std::span<const Slot> slots, int selected, std::span<const int> ammo, int selectedAmmo, int superChainGun);

        // Takes a pickup by model name; health pickups change health. Returns the sound to play,
        // or "" when Kurt can't take it (full inventory, not a pickup).
        std::string Collect(const std::string& pickup, int& health);

        // Selects slot index (the item keys 1-5), if there is one.
        void Select(int index);

        // Selects the next (step 1) or previous (-1) slot, wrapping around.
        void SelectNext(int step);

        // Selects a sniper round type (0 normal bullets, 1-5).
        void SelectAmmo(int type) { SelectedAmmo = Math::Clamp(type, 0, AmmoTypes); }

        // A sniper round of type was fired; normal bullets never run out.
        void UseAmmo(int type);

        // The next (step 1) or previous (-1) round type with rounds, wrapping around; normal
        // bullets always have some (0x46c900).
        int NextAmmo(int step) const;

        // One of the selected item is used up; an empty slot goes.
        void Consume();

        // The super chain gun's time runs (while firing); at 0 its slot goes.
        void TickSuperChainGun(int ticks);

    private:
        static const std::unordered_map<std::string, Item>& ItemPickups();
        // Pickups used at once (table 0x4920e0): sniper ammo (0-4), health (5-9), easter eggs.
        static constexpr std::array<const char*, 12> InstantPickups =
            { "SW_HOME", "SW_SGREN", "SW_HGREN", "SW_LGREN", "SW_BONES", "SW_H25", "SW_H50", "SW_H100", "SW_H150", "SW_H01", "SW_EWJ", "BONEFLC" };
        static constexpr int LastAmmoPickup = 4;
        static constexpr int BonesPickup = 4;
        static constexpr int Health25 = 5;
        static constexpr int Health50 = 6;
        static constexpr int Health100 = 7;
        static constexpr int Health150 = 8;
        static constexpr int Health01 = 9;
        static constexpr int Groovy = 10;
        static constexpr int BoneFlc = 11;
        // Sniper ammo per pickup 0-4 (halved on hard when above 1).
        static constexpr std::array<int, 5> AmmoPerPickup = { 8, 3, 3, 8, 1 };
        // Per difficulty: grenades per pickup, super chain gun ticks per pickup.
        static constexpr std::array<int, 3> Grenades = { 5, 3, 1 };
        static constexpr std::array<int, 3> SuperChainGunTicks = { 400, 200, 100 };
        static constexpr int SmallHealth = 10;
        static constexpr int BigHealth = 50;
        static constexpr int SuperHealth = 150;
        static constexpr const char* CollectSound = "COLLECT";
        static constexpr const char* BombSound = "WMIB";
        static constexpr const char* BonesSound = "BONES";
        static constexpr const char* HealthSound = "APPLE";

        std::vector<Slot> _slots;
        std::array<int, AmmoTypes> _ammo{};

        std::string CollectItem(Item item);

        Slot Add(Slot slot, Item item);

        std::string UseInstant(int index, int& health);

        // Adds health up to 100; above 100 (SW_H150) it stays.
        static int Heal(int health, int amount) { return health < MaxHealth ? Math::Min(health + amount, MaxHealth) : health; }

        void RemoveSlot(int index);
    };
}
