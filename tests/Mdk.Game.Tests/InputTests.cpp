#include <string>

#include <doctest.h>

#include <SDL3/SDL_scancode.h>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/GamepadMap.h"
#include "Mdk.Engine/Platform/Input.h"
#include "Mdk.Game.Tests/Asserts.h"

using namespace Mdk;
using namespace Mdk::Engine::Platform;

// Default bindings and rebinding (project.godot's input map, settings.gd bind).

namespace
{
    constexpr int WheelAway = 1;
    constexpr int WheelToward = -1;
    // An SDL mouse button with no name of its own.
    constexpr int ExtraButton = 8;

    Input NewInput()
    {
        Input input;
        input.BeginFrame();
        return input;
    }
}

TEST_CASE("InputTests.WheelSelectsItemsUntilTheStepUsesIt")
{
    auto input = NewInput();
    input.AddWheel(WheelToward);
    CHECK(input.IsDown(Key::ItemNext));
    CHECK(input.LastControl() == "Wheel down");

    input.ClearMouse();
    CHECK_FALSE(input.IsDown(Key::ItemNext));

    input.AddWheel(WheelAway);
    CHECK(input.IsDown(Key::ItemPrevious));
}

TEST_CASE("InputTests.EUsesTheItemAndFliesUp")
{
    auto input = NewInput();
    input.SetKey(SDL_SCANCODE_E, Input::State::Down, Repeat::No);
    CHECK(input.IsDown(Key::UseItem));
    CHECK(input.IsDown(Key::Up));

    // Rebinding "use" leaves E flying up.
    input.SetKey(SDL_SCANCODE_E, Input::State::Up, Repeat::No);
    CHECK(input.Bind(Key::UseItem, "F"));
    input.SetKey(SDL_SCANCODE_E, Input::State::Down, Repeat::No);
    CHECK_FALSE(input.IsDown(Key::UseItem));
    CHECK(input.IsDown(Key::Up));
}

TEST_CASE("InputTests.AnyMouseButtonBinds")
{
    auto input = NewInput();
    input.SetButton(static_cast<MouseButton>(ExtraButton), Input::State::Down);
    CHECK(input.LastControl() == "Mouse " + std::to_string(ExtraButton));

    CHECK(input.Bind(Key::Jump, input.LastControl()));
    input.SetButton(static_cast<MouseButton>(ExtraButton), Input::State::Up);
    input.SetButton(static_cast<MouseButton>(ExtraButton), Input::State::Down);
    CHECK(input.IsDown(Key::Jump));
    CHECK(input.Describe(Key::Jump) == "Mouse " + std::to_string(ExtraButton));
}

TEST_CASE("InputTests.SavedButtonNamesStillBind")
{
    const std::pair<const char*, MouseButton> data[] = {
        {"Mouse 4", MouseButton::Back},
        {"Mouse 5", MouseButton::Forward},
        {"Middle mouse", MouseButton::Middle},
    };
    for (auto [name, button] : data)
    {
        auto input = NewInput();
        CHECK(input.Bind(Key::Jump, name));
        input.SetButton(button, Input::State::Down);
        CHECK(input.IsDown(Key::Jump));
        CHECK(input.Describe(Key::Jump) == name);
    }
}

TEST_CASE("InputTests.WheelBinds")
{
    auto input = NewInput();
    CHECK(input.Bind(Key::Jump, "Wheel up"));
    input.AddWheel(WheelAway);
    CHECK(input.IsDown(Key::Jump));
    CHECK_FALSE(input.IsDown(Key::ItemPrevious));
}

TEST_CASE("InputTests.PadButtonsPlayAndWorkTheMenus")
{
    auto input = NewInput();
    input.SetPadButton(PadButton::South, Input::State::Down);
    CHECK(input.IsDown(Key::Jump));
    CHECK(input.WasPressed(MenuKey::Accept));
    CHECK(input.LastPadControl() == "A");
    CHECK(input.LastControl() == "");

    input.SetPadButton(PadButton::South, Input::State::Up);
    CHECK_FALSE(input.IsDown(Key::Jump));
}

TEST_CASE("InputTests.PadButtonBinds")
{
    auto input = NewInput();
    CHECK(input.BindPad(Key::Jump, "RB"));
    CHECK(input.DescribePad(Key::Jump) == "RB");

    input.SetPadButton(PadButton::RightShoulder, Input::State::Down);
    CHECK(input.IsDown(Key::Jump));
    CHECK_FALSE(input.IsDown(Key::ItemNext));

    // A is free now; back to the defaults, it jumps again.
    input.SetPadButton(PadButton::RightShoulder, Input::State::Up);
    input.SetPadButton(PadButton::South, Input::State::Down);
    CHECK_FALSE(input.IsDown(Key::Jump));
    input.ResetPadBindings();
    CHECK(input.DescribePad(Key::Jump) == "A");
}

TEST_CASE("InputTests.StartAndUnknownNamesDontBind")
{
    for (const char* name : {"Start", "Keypad"})
    {
        // Start pauses and goes back in the menus, whatever the bindings.
        auto input = NewInput();
        CHECK_FALSE(input.BindPad(Key::Jump, name));
        CHECK(input.DescribePad(Key::Jump) == "A");
    }
}

TEST_CASE("InputTests.PadLookScalesAndInverts")
{
    auto input = NewInput();
    input.AddPadLook(Vector2(0.15f, 0.15f));
    CHECK_PRECISION(1.0f, input.MouseX(), 3);
    CHECK_PRECISION(1.0f, input.MouseY(), 3);

    input.ClearMouse();
    input.PadSensitivity = 2.0f;
    input.InvertPad = true;
    input.AddPadLook(Vector2(0.15f, 0.15f));
    CHECK_PRECISION(2.0f, input.MouseX(), 3);
    CHECK_PRECISION(-2.0f, input.MouseY(), 3);
}

namespace
{
    void Press(Input& input, SDL_Scancode scancode) { input.SetKey(scancode, Input::State::Down, Repeat::No); }
}

namespace
{
    void Release(Input& input, SDL_Scancode scancode) { input.SetKey(scancode, Input::State::Up, Repeat::No); }
}

TEST_CASE("InputTests.ZoomKeysKeepTheirDefaultsEverywhere")
{
    auto input = NewInput();
    Press(input, SDL_SCANCODE_PAGEUP);
    CHECK(input.IsDown(Key::ZoomIn));
    Release(input, SDL_SCANCODE_PAGEUP);

    input.Scoped = true;
    Press(input, SDL_SCANCODE_PAGEDOWN);
    CHECK(input.IsDown(Key::ZoomOut));
    Press(input, SDL_SCANCODE_W);
    CHECK(input.IsDown(Key::Forward));
    CHECK(input.Describe(Key::ZoomIn) != "-");
}

TEST_CASE("InputTests.AKeyWalksOnFootAndZoomsWhileSniping")
{
    auto input = NewInput();
    CHECK(input.Bind(Key::ZoomIn, "W"));
    CHECK(input.Bind(Key::ZoomOut, "S"));
    CHECK(input.Describe(Key::ZoomIn) == "W");
    CHECK(input.Describe(Key::Forward) == "W");

    Press(input, SDL_SCANCODE_W);
    CHECK(input.IsDown(Key::Forward));
    CHECK_FALSE(input.IsDown(Key::ZoomIn));
    Release(input, SDL_SCANCODE_W);

    input.Scoped = true;
    Press(input, SDL_SCANCODE_W);
    CHECK(input.IsDown(Key::ZoomIn));
    CHECK_FALSE(input.IsDown(Key::Forward));
    Release(input, SDL_SCANCODE_W);

    Press(input, SDL_SCANCODE_S);
    CHECK(input.IsDown(Key::ZoomOut));
    CHECK_FALSE(input.IsDown(Key::Back));
    Release(input, SDL_SCANCODE_S);

    // The old zoom keys went to W and S, the other bindings are as they were.
    Press(input, SDL_SCANCODE_PAGEUP);
    CHECK_FALSE(input.IsDown(Key::ZoomIn));
    Press(input, SDL_SCANCODE_A);
    CHECK(input.IsDown(Key::StrafeLeft));
}

TEST_CASE("InputTests.AKeyHeldAcrossSnipingIsReleasedAsPressed")
{
    auto input = NewInput();
    CHECK(input.Bind(Key::ZoomIn, "W"));
    Press(input, SDL_SCANCODE_W);
    CHECK(input.IsDown(Key::Forward));

    input.Scoped = true;
    Release(input, SDL_SCANCODE_W);
    CHECK_FALSE(input.IsDown(Key::Forward));
    CHECK_FALSE(input.IsDown(Key::ZoomIn));
}

TEST_CASE("InputTests.BindingAZoomKeyToAnotherActionTakesItsZoomAway")
{
    auto input = NewInput();
    Press(input, SDL_SCANCODE_PAGEUP);
    Release(input, SDL_SCANCODE_PAGEUP);
    CHECK(input.Bind(Key::Jump, input.LastControl()));
    input.Scoped = true;
    Press(input, SDL_SCANCODE_PAGEUP);
    CHECK(input.IsDown(Key::Jump));
    CHECK_FALSE(input.IsDown(Key::ZoomIn));
}

TEST_CASE("InputTests.WheelZoomsWhileSnipingByDefault")
{
    auto input = NewInput();
    input.Scoped = true;
    input.AddWheel(WheelAway);
    CHECK(input.ZoomWheel() == 1.0f);
    CHECK_FALSE(input.IsDown(Key::ItemPrevious));

    input.ClearMouse();
    input.AddWheel(WheelToward);
    CHECK(input.ZoomWheel() == -1.0f);
    CHECK_FALSE(input.IsDown(Key::ItemNext));

    input.ClearMouse();
    CHECK(input.ZoomWheel() == 0.0f);
    input.Scoped = false;
    input.AddWheel(WheelAway);
    CHECK(input.ZoomWheel() == 0.0f);
    CHECK(input.IsDown(Key::ItemPrevious));
}

TEST_CASE("InputTests.ReboundWheelDoesItsActionWhileSnipingToo")
{
    auto input = NewInput();
    CHECK(input.Bind(Key::Jump, "Wheel up"));
    input.Scoped = true;
    input.AddWheel(WheelAway);
    CHECK(input.ZoomWheel() == 0.0f);
    CHECK(input.IsDown(Key::Jump));

    // The other direction was not rebound: it still zooms.
    input.ClearMouse();
    input.AddWheel(WheelToward);
    CHECK(input.ZoomWheel() == -1.0f);
}

TEST_CASE("InputTests.WheelBoundToAZoomActionZoomsThatWay")
{
    auto input = NewInput();
    CHECK(input.Bind(Key::ZoomOut, "Wheel up"));
    input.Scoped = true;
    input.AddWheel(WheelAway);
    CHECK(input.ZoomWheel() == -1.0f);
    CHECK_FALSE(input.IsDown(Key::ZoomOut));

    // Off the scope the wheel keeps selecting items.
    input.ClearMouse();
    input.Scoped = false;
    input.AddWheel(WheelAway);
    CHECK(input.IsDown(Key::ItemPrevious));
}

TEST_CASE("InputTests.DefaultKeysRestoreTheWheelsZoom")
{
    auto input = NewInput();
    CHECK(input.Bind(Key::Jump, "Wheel up"));
    input.ResetBindings();
    input.Scoped = true;
    input.AddWheel(WheelAway);
    CHECK(input.ZoomWheel() == 1.0f);
}

TEST_CASE("InputTests.CapsLockKeepsTurboOn")
{
    // The original's Caps Lock: turbo follows the lock (its light), not the key's presses
    // (macOS sends a press when it locks, a release when it unlocks).
    auto input = NewInput();
    input.SetKey(SDL_SCANCODE_CAPSLOCK, Input::State::Down, Repeat::No);
    CHECK_FALSE(input.WasPressed(Key::AlwaysRun));
    CHECK(input.Describe(Key::AlwaysRun) == "CapsLock");

    input.LockCaps(Input::Lock::On);
    CHECK(input.IsDown(Key::Turbo));

    input.LockCaps(Input::Lock::Off);
    CHECK_FALSE(input.IsDown(Key::Turbo));
}

TEST_CASE("InputTests.AnotherAlwaysRunKeyToggles")
{
    // Bound elsewhere, "always run" toggles on presses and the lock is ignored.
    auto input = NewInput();
    CHECK(input.Bind(Key::AlwaysRun, "F"));
    input.LockCaps(Input::Lock::On);
    CHECK_FALSE(input.IsDown(Key::Turbo));

    input.SetKey(SDL_SCANCODE_F, Input::State::Down, Repeat::No);
    CHECK(input.WasPressed(Key::AlwaysRun));

    input.ToggleRun();
    CHECK(input.IsDown(Key::Turbo));
}

