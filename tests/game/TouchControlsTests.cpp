#include <tuple>

#include <doctest.h>

#include <SDL3/SDL.h>

#include "core/Drawing.h"
#include "core/Numerics.h"
#include "engine/platform/Input.h"
#include "engine/platform/ScreenDensity.h"
#include "engine/platform/TouchControls.h"
#include "engine/platform/Window.h"
#include "game/Asserts.h"

using namespace Mdk;
using namespace Mdk::Engine::Platform;

// Reaches Window's private IsTouch (the C# test used reflection).
struct Mdk::Engine::Platform::WindowProbe
{
    static bool IsTouch(const SDL_Event& e) { return Window::IsTouch(e); }
};

// The on-screen controls of a touch screen (Android): the stick, the buttons, looking.

namespace
{
    // A phone held sideways (window pixels).
    constexpr Vector2 Screen{2400.0f, 1080.0f};
    constexpr std::uint64_t First = 1;
    constexpr std::uint64_t Second = 2;

    struct Setup
    {
        TouchControls Touch;
        Engine::Platform::Input Input;
    };

    Setup New()
    {
        Setup setup;
        setup.Input.BeginFrame();
        return setup;
    }

    const TouchButton& ButtonOf(TouchControls& touch, Vector2 screen, Key key)
    {
        for (const auto& b : touch.Layout(screen))
        {
            if (b.Key == key)
            {
                return b;
            }
        }

        throw std::runtime_error("Sequence contains no matching element");
    }

    Vector2 CentreOf(TouchControls& touch, Key key)
    {
        RectangleF area = ButtonOf(touch, Screen, key).Area;
        return {area.X + area.Width / 2.0f, area.Y + area.Height / 2.0f};
    }

    // A 16:10 tablet (2880 x 1800, 320 dpi: 12.6 px/mm).
    constexpr Vector2 Tablet{2880.0f, 1800.0f};
    constexpr float TabletDensity = 320.0f / 25.4f;
    // A phone's 420 dpi.
    constexpr float PhoneDensity = 420.0f / 25.4f;
    constexpr float ButtonMm = 11.0f;
}

TEST_CASE("TouchControlsTests.AButtonHoldsItsKeyWhileTouched")
{
    for (auto key : {Key::Fire, Key::Jump, Key::Sniper})
    {
        auto [touch, input] = New();

        touch.Down(First, CentreOf(touch, key), Screen, input);
        CHECK(input.IsDown(key));
        CHECK(input.WasPressed(key));

        touch.Up(First, input);
        CHECK_FALSE(input.IsDown(key));
    }
}

TEST_CASE("TouchControlsTests.PauseOpensThePauseMenu")
{
    auto [touch, input] = New();
    touch.Down(First, CentreOf(touch, Key::Escape), Screen, input);
    CHECK(input.WasPressed(MenuKey::Back));
}

TEST_CASE("TouchControlsTests.TheStickWalksWhereItIsPushed")
{
    auto [touch, input] = New();
    Vector2 origin(400.0f, 700.0f);
    float radius = touch.StickRadius(Screen);

    touch.Down(First, origin, Screen, input);
    CHECK_FALSE(input.IsDown(Key::Forward));

    touch.Move(First, origin - Vector2(0.0f, radius), Screen, input);
    CHECK(input.IsDown(Key::Forward));
    CHECK_FALSE(input.IsDown(Key::StrafeLeft));

    touch.Move(First, origin + Vector2(-radius, radius), Screen, input);
    CHECK_FALSE(input.IsDown(Key::Forward));
    CHECK(input.IsDown(Key::Back));
    CHECK(input.IsDown(Key::StrafeLeft));

    touch.Up(First, input);
    CHECK_FALSE(input.IsDown(Key::Back));
    CHECK_FALSE(input.IsDown(Key::StrafeLeft));
}

TEST_CASE("TouchControlsTests.TestHoldsDontReleaseTouches")
{
    auto [touch, input] = New();
    Vector2 origin(400.0f, 700.0f);

    // The viewer releases its test keys every frame.
    touch.Down(First, origin, Screen, input);
    touch.Move(First, origin - Vector2(0.0f, touch.StickRadius(Screen)), Screen, input);
    touch.Down(Second, CentreOf(touch, Key::Fire), Screen, input);
    input.Hold(Key::Forward, Input::State::Up);
    input.Hold(Key::Fire, Input::State::Up);

    CHECK(input.IsDown(Key::Forward));
    CHECK(input.IsDown(Key::Fire));
}

TEST_CASE("TouchControlsTests.DraggingTheRightSideLooks")
{
    auto [touch, input] = New();
    Vector2 at(1500.0f, 300.0f);

    touch.Down(First, at, Screen, input);
    touch.Move(First, at + Vector2(20.0f, -10.0f), Screen, input);
    CHECK(input.MouseX() > 0.0f);
    CHECK(input.MouseY() < 0.0f);
}

TEST_CASE("TouchControlsTests.DraggingFireAimsWhileFiring")
{
    auto [touch, input] = New();
    Vector2 at = CentreOf(touch, Key::Fire);

    touch.Down(First, at, Screen, input);
    touch.Move(First, at + Vector2(-300.0f, -120.0f), Screen, input);
    CHECK(input.IsDown(Key::Fire));
    CHECK(input.MouseX() < 0.0f);
    CHECK(input.MouseY() < 0.0f);
}

TEST_CASE("TouchControlsTests.FingersAreIndependent")
{
    auto [touch, input] = New();
    Vector2 origin(400.0f, 700.0f);

    touch.Down(First, origin, Screen, input);
    touch.Move(First, origin - Vector2(0.0f, touch.StickRadius(Screen)), Screen, input);
    touch.Down(Second, CentreOf(touch, Key::Fire), Screen, input);
    touch.Up(Second, input);

    CHECK(input.IsDown(Key::Forward));
    CHECK_FALSE(input.IsDown(Key::Fire));
}

TEST_CASE("TouchControlsTests.ReleaseLetsEverythingGo")
{
    auto [touch, input] = New();
    touch.Down(First, CentreOf(touch, Key::Fire), Screen, input);
    touch.Down(Second, Vector2(400.0f, 700.0f), Screen, input);
    touch.Move(Second, Vector2(400.0f, 400.0f), Screen, input);

    touch.Release(input);
    CHECK_FALSE(input.IsDown(Key::Fire));
    CHECK_FALSE(input.IsDown(Key::Forward));
    CHECK(!touch.Stick());
}

TEST_CASE("TouchControlsTests.ShownByTheSettingAndTheLastInput")
{
    const std::tuple<TouchButtons, InputSource, bool> data[] = {
        {TouchButtons::Auto, InputSource::Touch, true},
        {TouchButtons::Auto, InputSource::Device, false},
        {TouchButtons::Off, InputSource::Touch, false},
        {TouchButtons::On, InputSource::Device, true},
    };
    for (auto [mode, last, shown] : data)
    {
        // Auto: a gamepad, a keyboard or a mouse hides them; a touch shows them again.
        CHECK(TouchControls::Shown(mode, last) == shown);
    }
}

TEST_CASE("TouchControlsTests.ATabletsButtonsAreAFingertipWide")
{
    // Not 15 % of 1800 pixels (270): 11 mm.
    TouchControls touch;
    touch.Measure(TabletDensity, RectangleF::Empty());

    RectangleF fire = ButtonOf(touch, Tablet, Key::Fire).Area;
    CHECK_NEAR(ButtonMm * TabletDensity, fire.Width, 0.01f);
    CHECK(fire.Height == fire.Width);
    CHECK(touch.StickRadius(Tablet) < 0.12f * Tablet.Y);
}

TEST_CASE("TouchControlsTests.APhonesButtonsKeepTheirPartOfTheHeight")
{
    // 11 mm would be 182 pixels: a phone keeps 15 % of its height (162).
    TouchControls touch;
    touch.Measure(PhoneDensity, RectangleF::Empty());

    RectangleF fire = ButtonOf(touch, Screen, Key::Fire).Area;
    CHECK_NEAR(0.15f * Screen.Y, fire.Width, 0.01f);
}

TEST_CASE("TouchControlsTests.AnUnknownDisplayUsesTheHeight")
{
    TouchControls touch;
    touch.Measure(0.0f, RectangleF::Empty());
    CHECK_NEAR(0.15f * Tablet.Y, ButtonOf(touch, Tablet, Key::Fire).Area.Width, 0.01f);
    CHECK_NEAR(0.12f * Tablet.Y, touch.StickRadius(Tablet), 0.01f);
}

TEST_CASE("TouchControlsTests.ButtonsKeepInsideTheSafeArea")
{
    // A cut-out on the right and a rounded top: the buttons move in.
    TouchControls touch;
    RectangleF safe(0.0f, 60.0f, Tablet.X - 100.0f, Tablet.Y - 60.0f);
    touch.Measure(TabletDensity, safe);

    for (const auto& b : touch.Layout(Tablet))
    {
        CHECK((b.Area.Right() <= safe.Right() && b.Area.Top() >= safe.Top()));
    }
}

TEST_CASE("TouchControlsTests.AnotherDisplayLaysThemOutAgain")
{
    TouchControls touch;
    touch.Measure(0.0f, RectangleF::Empty());
    float before = ButtonOf(touch, Tablet, Key::Fire).Area.Width;

    touch.Measure(TabletDensity, RectangleF::Empty());
    CHECK(ButtonOf(touch, Tablet, Key::Fire).Area.Width != before);
}

TEST_CASE("TouchControlsTests.AHeldButtonIsPressed")
{
    auto [touch, input] = New();
    touch.Down(First, CentreOf(touch, Key::Jump), Screen, input);
    CHECK(touch.Held(Key::Jump));
    CHECK_FALSE(touch.Held(Key::Fire));

    touch.Up(First, input);
    CHECK_FALSE(touch.Held(Key::Jump));
}

TEST_CASE("TouchControlsTests.UnitsPerMmFollowTheDisplayScale")
{
    const std::tuple<float, float, float, float> data[] = {
        {2.625f, 1.0f, ScreenDensity::AndroidDpi, 16.54f},
        {1.0f, 1.0f, ScreenDensity::DesktopDpi, 3.78f},
        {2.0f, 2.0f, ScreenDensity::DesktopDpi, 3.78f},
        {0.0f, 1.0f, ScreenDensity::AndroidDpi, 0.0f},
    };
    for (auto [scale, pixelDensity, baseDpi, expected] : data)
    {
        // A Retina window's units are points: two pixels each.
        CHECK_NEAR(expected, ScreenDensity::UnitsPerMm(scale, pixelDensity, baseDpi), 0.01f);
    }
}

TEST_CASE("TouchControlsTests.AFingersClickReleaseReachesPlay")
{
    // A menu tap (SDL's left click of a finger) starts play: its release must still let go of the
    // click, or the left button (fire) stays held (issue #6).
    SDL_Event up{};
    up.button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.which = SDL_TOUCH_MOUSEID;
    up.button.button = SDL_BUTTON_LEFT;

    CHECK_FALSE(WindowProbe::IsTouch(up));
}
