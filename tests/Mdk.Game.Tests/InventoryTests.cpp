#include <doctest.h>

#include "Mdk.Game/Kurt/Inventory.h"

#include <array>
#include <string>
#include <tuple>

using namespace Mdk::Game::Kurt;
using Item = Inventory::Item;

// Kurt's pickups (inventory.gd, godot-mdk docs/gameplay.md "Pickups").
namespace
{
    const std::string Grenade = "SW_HBOMB";
    const std::string SuperChainGun = "SW_GATT";
    const std::array<std::string, 5> FiveItems = {"SW_DUMMY", "SW_INTER", "SW_TWIST", "SW_THUMP", "SW_KEY"};

    std::string Collect(Inventory& inventory, const std::string& pickup)
    {
        int health = Inventory::MaxHealth;
        return inventory.Collect(pickup, health);
    }
}

TEST_CASE("InventoryTests.GrenadesStackInOneSlot")
{
    const std::array<std::tuple<Difficulty, int>, 3> cases = {{
        {Difficulty::Easy, 10},
        {Difficulty::Normal, 6},
        {Difficulty::Hard, 2},
    }};
    for (const auto& [difficulty, count] : cases)
    {
        Inventory inventory;
        inventory.Difficulty = difficulty;
        Collect(inventory, Grenade);
        Collect(inventory, Grenade);

        REQUIRE(inventory.Slots().size() == 1);
        CHECK(inventory.Slots()[0] == Inventory::Slot{Item::Grenade, count});
    }
}

TEST_CASE("InventoryTests.SixthItemIsLeft")
{
    Inventory inventory;
    for (const auto& pickup : FiveItems)
    {
        CHECK(Collect(inventory, pickup) != "");
    }

    CHECK(Collect(inventory, "SW_SEAL") == "");
    CHECK(inventory.Slots().size() == Inventory::MaxSlots);
}

TEST_CASE("InventoryTests.NewItemIsSelectedButNotTheSuperChainGun")
{
    Inventory inventory;
    Collect(inventory, "SW_DUMMY");
    Collect(inventory, "SW_INTER");
    CHECK(inventory.SelectedItem() == Item::InterestingBomb);

    Collect(inventory, SuperChainGun);
    CHECK(inventory.SelectedItem() == Item::InterestingBomb);
    CHECK(inventory.SuperChainGun == 200);
}

TEST_CASE("InventoryTests.SelectionWrapsAndUsingTheLastOneRemovesTheSlot")
{
    Inventory inventory;
    Collect(inventory, "SW_DUMMY");
    Collect(inventory, "SW_TWIST");
    inventory.SelectNext(1);
    CHECK(inventory.SelectedItem() == Item::Dummy);
    inventory.SelectNext(-1);
    CHECK(inventory.SelectedItem() == Item::Tornado);

    inventory.Consume();
    REQUIRE(inventory.Slots().size() == 1);
    CHECK(inventory.Slots()[0].Item == Item::Dummy);
    CHECK(inventory.Selected == 0);
}

TEST_CASE("InventoryTests.SuperChainGunSlotGoesWhenItsTimeIsUp")
{
    Inventory inventory;
    inventory.Difficulty = Difficulty::Hard;
    Collect(inventory, SuperChainGun);
    inventory.TickSuperChainGun(99);
    CHECK(inventory.Slots().size() == 1);

    inventory.TickSuperChainGun(1);
    CHECK(inventory.Slots().empty());
    CHECK(inventory.SuperChainGun == 0);
}

TEST_CASE("InventoryTests.AmmoIsHalvedOnHard")
{
    const std::array<std::tuple<Difficulty, int>, 2> cases = {{
        {Difficulty::Normal, 8},
        {Difficulty::Hard, 4},
    }};
    for (const auto& [difficulty, amount] : cases)
    {
        Inventory inventory;
        inventory.Difficulty = difficulty;
        CHECK(Collect(inventory, "SW_HOME") == "COLLECT");
        CHECK(inventory.Ammo()[0] == amount);
        CHECK(inventory.SelectedAmmo == 1);
    }
}

TEST_CASE("InventoryTests.HealthPickupsHeal")
{
    const std::array<std::tuple<const char*, int, int>, 5> cases = {{
        {"SW_H25", 95, 100},
        {"SW_H25", 150, 150},
        {"SW_H50", 20, 70},
        {"SW_H100", 20, 100},
        {"SW_H150", 120, 150},
    }};
    for (const auto& [pickup, before, after] : cases)
    {
        int health = before;
        CHECK(Inventory().Collect(pickup, health) == "APPLE");
        CHECK(health == after);
    }
}
