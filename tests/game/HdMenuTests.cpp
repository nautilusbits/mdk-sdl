#include <doctest.h>

#include "game/hd_textures/HdMenu.h"

#include <vector>

using namespace Mdk::Game::HdTextures;

// The options page's HD texture items per platform: Android can't run the upscaler (it uses
// the mod made on a PC, switched on the Mods page).

TEST_CASE("HdMenuTests.DesktopMakes")
{
    CHECK(HdMenu::Items(Host::Desktop) == std::vector<HdItem>{HdItem::Make});
}

TEST_CASE("HdMenuTests.AndroidDoesnt")
{
    CHECK(HdMenu::Items(Host::Android).empty());
}

TEST_CASE("HdMenuTests.OnlyDesktopMakes")
{
    CHECK(HdMenu::CanMake(Host::Desktop));
    CHECK_FALSE(HdMenu::CanMake(Host::Android));
}
