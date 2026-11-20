#include <utility>
#include <vector>

#include <doctest.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/GamepadMap.h"
#include "Mdk.Game.Tests/Asserts.h"

using namespace Mdk;
using namespace Mdk::Engine::Platform;

// The gamepad's layout (GamepadMap): its buttons in play and in the menus, the left stick walking,
// the right one looking.

namespace
{
    constexpr float Tolerance = 1e-4f;
    constexpr Key WalkKeys[] = {Key::Forward, Key::Back, Key::StrafeLeft, Key::StrafeRight};
    constexpr int MenuKeyCount = 10;

    std::vector<Key> Walking(Vector2 stick)
    {
        std::vector<Key> keys;
        for (auto k : WalkKeys)
        {
            if (GamepadMap::Walks(stick, k))
            {
                keys.push_back(k);
            }
        }

        return keys;
    }

    std::vector<MenuKey> Steering(Vector2 stick)
    {
        std::vector<MenuKey> keys;
        for (int i = 0; i < MenuKeyCount; i++)
        {
            if (GamepadMap::Steers(stick, static_cast<MenuKey>(i)))
            {
                keys.push_back(static_cast<MenuKey>(i));
            }
        }

        return keys;
    }
}

TEST_CASE("GamepadMapTests.ButtonsPlay")
{
    const std::pair<PadButton, Key> data[] = {
        {PadButton::RightTrigger, Key::Fire},
        {PadButton::LeftTrigger, Key::Sniper},
        {PadButton::South, Key::Jump},
        {PadButton::East, Key::Turbo},
        {PadButton::West, Key::UseItem},
        {PadButton::RightShoulder, Key::ItemNext},
        {PadButton::LeftShoulder, Key::ItemPrevious},
        {PadButton::DpadUp, Key::ZoomIn},
        {PadButton::DpadDown, Key::ZoomOut},
        {PadButton::Start, Key::Escape},
    };
    for (auto [button, key] : data)
    {
        CHECK(GamepadMap::Game(button) == key);
    }
}

TEST_CASE("GamepadMapTests.ButtonsWorkTheMenus")
{
    const std::pair<PadButton, MenuKey> data[] = {
        {PadButton::South, MenuKey::Accept},
        {PadButton::East, MenuKey::Back},
        {PadButton::Start, MenuKey::Back},
        {PadButton::DpadUp, MenuKey::Up},
        {PadButton::DpadDown, MenuKey::Down},
        {PadButton::DpadLeft, MenuKey::Left},
        {PadButton::DpadRight, MenuKey::Right},
    };
    for (auto [button, key] : data)
    {
        CHECK(GamepadMap::Menu(button) == key);
    }
}

TEST_CASE("GamepadMapTests.TheLeftStickWalks")
{
    // Up the stick (SDL's y down) walks forward; a slight push holds nothing.
    CHECK(Walking(Vector2(0.0f, -0.9f)) == std::vector<Key>{Key::Forward});
    CHECK(Walking(Vector2(0.8f, 0.8f)) == std::vector<Key>{Key::Back, Key::StrafeRight});
    CHECK(Walking(Vector2(0.2f, -0.2f)).empty());
}

TEST_CASE("GamepadMapTests.TheLeftStickSteersTheMenus")
{
    CHECK(Steering(Vector2(0.0f, -0.9f)) == std::vector<MenuKey>{MenuKey::Up});
    CHECK(Steering(Vector2(-0.9f, 0.1f)) == std::vector<MenuKey>{MenuKey::Left});
    CHECK(Steering(Vector2(0.2f, 0.2f)).empty());
}

TEST_CASE("GamepadMapTests.TheRightStickLooks")
{
    // Full right turns at the full rate; inside the dead zone nothing; half way is slower than half
    // (a curve, for aiming).
    Vector2 full = GamepadMap::Look(Vector2(1.0f, 0.0f), 1.0f);
    CHECK_NEAR(GamepadMap::LookRate.X, full.X, Tolerance);
    CHECK_NEAR(0.0f, full.Y, Tolerance);
    CHECK(GamepadMap::Look(Vector2(0.1f, -0.1f), 1.0f) == Vector2::Zero());
    Vector2 half = GamepadMap::Look(Vector2(0.5f, 0.0f), 1.0f);
    CHECK(half.X >= 0.01f);
    CHECK(half.X <= GamepadMap::LookRate.X / 2.0f);

    // Up the stick looks up (the look's Y is down, as the mouse's).
    CHECK(GamepadMap::Look(Vector2(0.0f, -1.0f), 1.0f).Y < 0.0f);
}

TEST_CASE("GamepadMapTests.ButtonNamesRoundTrip")
{
    for (int i = 0; i < PadButtonCount; i++)
    {
        auto button = static_cast<PadButton>(i);
        CHECK(GamepadMap::Parse(GamepadMap::Name(button)) == button);
    }

    CHECK(!GamepadMap::Parse("W").has_value());
}
