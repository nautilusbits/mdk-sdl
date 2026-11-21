#include <doctest.h>

#include "Asserts.h"
#include "core/Drawing.h"
#include "core/Numerics.h"
#include "engine/platform/Input.h"
#include "engine/platform/TouchControls.h"
#include "engine/platform/TouchLayout.h"
#include "game/flow/Settings.h"
#include "game/menu/TouchEditor.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Mdk;
using namespace Mdk::Engine::Platform;
using Mdk::Game::Flow::Settings;
using Mdk::Game::Menu::TouchEditor;

// The player's places of the on-screen controls: saved, fitted to other displays, edited.
namespace
{
    // A phone held sideways (420 dpi) with a cut-out on the left.
    const Vector2 Phone(2400.f, 1080.f);
    const RectangleF PhoneSafe(90.f, 0.f, 2310.f, 1080.f);
    constexpr float PhoneDensity = 420.f / 25.4f;
    // A foldable's inner display (near square) and a 16:10 tablet.
    const Vector2 Inner(2208.f, 1768.f);
    constexpr float InnerDensity = 373.f / 25.4f;
    const Vector2 Tablet(2880.f, 1800.f);
    constexpr float TabletDensity = 320.f / 25.4f;
    const TouchPlaces None;
    constexpr float Tolerance = 0.01f;

    Vector2 CentreOf(RectangleF area) { return Vector2(area.X + area.Width / 2.f, area.Y + area.Height / 2.f); }

    TouchEditor Editor(const TouchPlaces& saved = None)
    {
        TouchEditor editor(saved);
        editor.Measure(Phone, PhoneSafe, PhoneDensity);
        return editor;
    }

    bool InRange(float value, float low, float high) { return value >= low && value <= high; }

    // The play side: the placed controls and the input they press (Playing's tuple).
    struct Play
    {
        std::shared_ptr<TouchControls> Touch;
        std::shared_ptr<Engine::Platform::Input> Pressed;
    };

    Play Playing(const TouchPlaces& places)
    {
        auto input = std::make_shared<Input>();
        input->BeginFrame();
        auto touch = std::make_shared<TouchControls>();
        touch->Measure(PhoneDensity, PhoneSafe);
        touch->Arrange(places, 1.f);
        return {touch, input};
    }

    RectangleF ButtonArea(TouchControls& touch, Key key)
    {
        for (const auto& button : touch.Layout(Phone))
        {
            if (button.Key == key)
            {
                return button.Area;
            }
        }

        FAIL("no button for the key");
        return {};
    }

    Vector2 ToolAt(const TouchEditor& editor, TouchEditor::Tool tool)
    {
        for (const auto& area : editor.Tools())
        {
            if (area.Tool == tool)
            {
                return CentreOf(area.Area);
            }
        }

        FAIL("no such tool");
        return {};
    }

    // No tool covers a button.
    void CheckToolsClearOfButtons(const TouchEditor& editor)
    {
        for (const auto& tool : editor.Tools())
        {
            for (TouchPart part : TouchLayout::Parts())
            {
                if (!TouchLayout::IsButton(part))
                {
                    continue;
                }

                CHECK_FALSE(tool.Area.IntersectsWith(editor.Areas().at(part)));
            }
        }
    }
}

TEST_CASE("TouchLayoutTests.APlaceRoundTripsThroughText")
{
    TouchPlace place{Across::Right, 0.0113f, Along::Bottom, 0.2f, 9.8f, 9.8f};
    CHECK("Right,0.0113,Bottom,0.2,9.8,9.8" == place.ToString());
    TouchPlace parsed;
    CHECK(TouchPlace::TryParse(place.ToString(), parsed));
    CHECK(place == parsed);
}

TEST_CASE("TouchLayoutTests.BadPlacesAreRefused")
{
    const std::array<const char*, 6> texts{
        "",
        "Right,0.1,Bottom,0.2,9",
        "Up,0.1,Bottom,0.2,9,9",
        "Right,1.5,Bottom,0.2,9,9",
        "Right,0.1,Bottom,0.2,0,9",
        "Right,0.1,Bottom,0.2,NaN,9",
    };
    for (const char* text : texts)
    {
        CAPTURE(text);
        TouchPlace parsed;
        CHECK_FALSE(TouchPlace::TryParse(text, parsed));
    }
}

TEST_CASE("TouchLayoutTests.SettingsKeepThePlacesAndTheOpacity")
{
    Settings settings;
    settings.TouchOpacity = 50;
    settings.TouchPlaces[TouchPart::Fire] = TouchPlace{Across::Right, 0.05f, Along::Bottom, 0.3f, 12.f, 12.f};
    settings.TouchPlaces[TouchPart::Stick] = TouchPlace{Across::Left, 0.f, Along::Bottom, 0.f, 40.f, 30.f};

    std::string text = settings.Format();
    CHECK(text.find("touch.Fire=Right,0.05,Bottom,0.3,12,12\n") != std::string::npos);
    Settings parsed = Settings::Parse(text);
    CHECK(50 == parsed.TouchOpacity);
    CHECK(settings.TouchPlaces == parsed.TouchPlaces);
}

TEST_CASE("TouchLayoutTests.NothingSavedKeepsTheDefaults")
{
    Settings settings = Settings::Parse("touch.Fire=broken\ntouch.Nose=Right,0,Bottom,0,9,9\ntouch_opacity=3\n");
    CHECK(settings.TouchPlaces.empty());
    CHECK(Settings::MinOpacity == settings.TouchOpacity);
    CHECK(Settings().Format().find("touch.") == std::string::npos);

    for (TouchPart part : TouchLayout::Parts())
    {
        CHECK(TouchLayout::DefaultArea(part, Phone, PhoneSafe, PhoneDensity) == TouchLayout::AreaOf(part, None, Phone, PhoneSafe, PhoneDensity));
    }
}

TEST_CASE("TouchLayoutTests.AnAreaRoundTripsThroughItsPlace")
{
    RectangleF area(1900.f, 700.f, 180.f, 180.f);
    TouchPlace place = TouchLayout::PlaceOf(area, Phone, PhoneSafe, PhoneDensity);
    CHECK(Across::Right == place.X);
    CHECK(Along::Bottom == place.Y);

    RectangleF back = TouchLayout::Place(place, Phone, PhoneSafe, PhoneDensity);
    CHECK_NEAR(area.X, back.X, Tolerance);
    CHECK_NEAR(area.Y, back.Y, Tolerance);
    CHECK_NEAR(area.Width, back.Width, Tolerance);
}

TEST_CASE("TouchLayoutTests.APlaceKeepsItsEdgeAndSizeOnAnotherDisplay")
{
    // A button 2 % of the width from the right edge, 11 mm: on the tablet as far (in parts)
    // from its right edge, still 11 mm.
    TouchPlace place{Across::Right, 0.02f, Along::Bottom, 0.1f, 11.f, 11.f};
    RectangleF area = TouchLayout::AreaOf(TouchPart::Fire, TouchPlaces{{TouchPart::Fire, place}}, Tablet, RectangleF::Empty(), TabletDensity);

    CHECK_NEAR(Tablet.X - 0.02f * Tablet.X, area.Right(), Tolerance);
    CHECK_NEAR(Tablet.Y - 0.1f * Tablet.Y, area.Bottom(), Tolerance);
    CHECK_NEAR(11.f * TabletDensity, area.Width, Tolerance);
}

TEST_CASE("TouchLayoutTests.EveryPartFitsAFoldablesOtherDisplay")
{
    for (TouchPart part : {TouchPart::Fire, TouchPart::Pause, TouchPart::Stick})
    {
        CAPTURE(static_cast<int>(part));

        // Placed on the outer display (a wide phone), opened on the inner one (near square).
        RectangleF area = TouchLayout::DefaultArea(part, Phone, PhoneSafe, PhoneDensity);
        TouchPlaces places{{part, TouchLayout::PlaceOf(area, Phone, PhoneSafe, PhoneDensity)}};
        RectangleF inner = TouchLayout::AreaOf(part, places, Inner, RectangleF::Empty(), InnerDensity);

        CHECK(InRange(inner.Left(), 0.f, Inner.X));
        CHECK(InRange(inner.Right(), 0.f, Inner.X));
        CHECK(InRange(inner.Top(), 0.f, Inner.Y));
        CHECK(InRange(inner.Bottom(), 0.f, Inner.Y));
    }
}

TEST_CASE("TouchLayoutTests.FittingKeepsButtonsInsideAndBetweenTheirSizes")
{
    RectangleF tiny = TouchLayout::Fit(TouchPart::Fire, RectangleF(-50.f, 1000.f, 20.f, 30.f), Phone, PhoneSafe, PhoneDensity);
    CHECK_NEAR(TouchLayout::MinMm * PhoneDensity, tiny.Width, Tolerance);
    CHECK(tiny.Width == tiny.Height);
    CHECK(PhoneSafe.Left() == tiny.Left());
    CHECK_NEAR(PhoneSafe.Bottom(), tiny.Bottom(), Tolerance);

    RectangleF huge = TouchLayout::Fit(TouchPart::Fire, RectangleF(3000.f, -10.f, 900.f, 900.f), Phone, PhoneSafe, PhoneDensity);
    CHECK_NEAR(TouchLayout::MaxMm * PhoneDensity, huge.Width, Tolerance);
    CHECK_NEAR(PhoneSafe.Right(), huge.Right(), Tolerance);
    CHECK(0.f == huge.Top());
}

TEST_CASE("TouchLayoutTests.TheStickZoneHoldsAFullPush")
{
    RectangleF zone = TouchLayout::Fit(TouchPart::Stick, RectangleF(100.f, 100.f, 10.f, 5000.f), Phone, PhoneSafe, PhoneDensity);
    float reach = TouchControls().StickRadius(Phone);
    CHECK(zone.Width >= 2.f * reach - Tolerance);
    CHECK_NEAR(PhoneSafe.Height, zone.Height, Tolerance);
}

TEST_CASE("TouchLayoutTests.OverlappingButtonsAreFound")
{
    std::map<TouchPart, RectangleF> areas{
        {TouchPart::Fire, RectangleF(100.f, 100.f, 100.f, 100.f)},
        {TouchPart::Jump, RectangleF(150.f, 150.f, 100.f, 100.f)},
        {TouchPart::Use, RectangleF(200.f, 100.f, 100.f, 100.f)},
        {TouchPart::Stick, RectangleF(0.f, 0.f, 1000.f, 1000.f)},
    };

    CHECK(TouchLayout::Overlaps(TouchPart::Fire, areas));
    CHECK(TouchLayout::Overlaps(TouchPart::Jump, areas));
    // Touching edges and the zone under the buttons are fine.
    areas.erase(TouchPart::Jump);
    CHECK_FALSE(TouchLayout::Overlaps(TouchPart::Fire, areas));
    CHECK_FALSE(TouchLayout::Overlaps(TouchPart::Stick, areas));
}

TEST_CASE("TouchLayoutTests.TheDefaultsDontOverlap")
{
    std::map<TouchPart, RectangleF> areas;
    for (TouchPart part : TouchLayout::Parts())
    {
        areas[part] = TouchLayout::DefaultArea(part, Phone, PhoneSafe, PhoneDensity);
    }

    for (TouchPart part : TouchLayout::Parts())
    {
        CHECK_FALSE(TouchLayout::Overlaps(part, areas));
    }
}

// --- Play: the placed controls, and fingers keeping their roles -------------------------------

TEST_CASE("TouchLayoutTests.APlacedButtonPressesWhereItWasPut")
{
    // Fire on the left, inside the stick's zone: the button wins.
    RectangleF area(300.f, 700.f, 180.f, 180.f);
    auto [touch, input] = Playing({{TouchPart::Fire, TouchLayout::PlaceOf(area, Phone, PhoneSafe, PhoneDensity)}});

    touch->Down(1, CentreOf(area), Phone, *input);
    CHECK(input->IsDown(Key::Fire));
    CHECK_FALSE(touch->Stick().has_value());
}

TEST_CASE("TouchLayoutTests.APlacedZoneLeavesTheRestToLooking")
{
    // The zone shrunk to the bottom left corner: the left side's top looks.
    RectangleF zone(PhoneSafe.Left(), 600.f, 600.f, 480.f);
    auto [touch, input] = Playing({{TouchPart::Stick, TouchLayout::PlaceOf(zone, Phone, PhoneSafe, PhoneDensity)}});

    touch->Down(1, Vector2(400.f, 200.f), Phone, *input);
    CHECK_FALSE(touch->Stick().has_value());
    touch->Move(1, Vector2(420.f, 200.f), Phone, *input);
    CHECK(input->MouseX() > 0.f);

    touch->Down(2, Vector2(400.f, 900.f), Phone, *input);
    CHECK(touch->Stick().has_value());
}

TEST_CASE("TouchLayoutTests.ALookSlidingOverAButtonDoesntPressIt")
{
    auto [touch, input] = Playing({});
    Vector2 fire = CentreOf(ButtonArea(*touch, Key::Fire));
    // From the look area left of the buttons, across Jump to Fire.
    Vector2 start = fire - Vector2(600.f, 0.f);

    touch->Down(1, start, Phone, *input);
    touch->Move(1, fire, Phone, *input);
    CHECK_FALSE(input->IsDown(Key::Fire));
    CHECK_FALSE(input->IsDown(Key::Jump));
    CHECK(input->MouseX() > 0.f);
}

TEST_CASE("TouchLayoutTests.AButtonsFingerDoesntLook")
{
    auto [touch, input] = Playing({});
    Vector2 jump = CentreOf(ButtonArea(*touch, Key::Jump));

    touch->Down(1, jump, Phone, *input);
    touch->Move(1, jump - Vector2(300.f, 300.f), Phone, *input);
    CHECK(input->IsDown(Key::Jump));
    CHECK(0.f == input->MouseX());
    CHECK(0.f == input->MouseY());
    CHECK_FALSE(touch->Stick().has_value());
}

TEST_CASE("TouchLayoutTests.ThePointerIsHeldUntilLetGo")
{
    // The editor drags while the menus' finger (the left button) stays down.
    Input input;
    input.BeginFrame();
    input.SetButton(MouseButton::Left, Input::State::Down);
    input.BeginFrame();
    CHECK(input.IsDown(Pointer::Left));
    CHECK_FALSE(input.WasClicked(Pointer::Left));

    input.SetButton(MouseButton::Left, Input::State::Up);
    CHECK_FALSE(input.IsDown(Pointer::Left));
}

TEST_CASE("TouchLayoutTests.PauseCoveredByAButtonStillPauses")
{
    RectangleF pause = TouchLayout::DefaultArea(TouchPart::Pause, Phone, PhoneSafe, PhoneDensity);
    auto [touch, input] = Playing({{TouchPart::Fire, TouchLayout::PlaceOf(pause, Phone, PhoneSafe, PhoneDensity)}});

    touch->Down(1, CentreOf(pause), Phone, *input);
    CHECK(input->WasPressed(MenuKey::Back));
    CHECK_FALSE(input->IsDown(Key::Fire));
}

TEST_CASE("TouchLayoutTests.TheZoneLeavesRoomToLook")
{
    RectangleF zone = TouchLayout::Fit(TouchPart::Stick, PhoneSafe, Phone, PhoneSafe, PhoneDensity);
    CHECK(zone.Width < PhoneSafe.Width);
}

TEST_CASE("TouchLayoutTests.AnEmptyWindowKeepsThePlaces")
{
    // Mid-rotation (0 x 0): an edit can't make a place of nothing.
    TouchEditor editor = Editor();
    editor.Select(1);
    editor.Measure(Vector2::Zero(), RectangleF::Empty(), 0.f);
    editor.Grow(1);
    CHECK(editor.Places().empty());
}

TEST_CASE("TouchLayoutTests.TheToolsMoveAwayFromADraggedButton")
{
    // Pause goes into the top row, where the tools are: they move to the bottom while it's
    // dragged there and stay clear of it after.
    TouchEditor editor = Editor();
    Vector2 done = ToolAt(editor, TouchEditor::Tool::Done);
    RectangleF pause = editor.Areas().at(TouchPart::Pause);
    editor.Press(CentreOf(pause));
    editor.Drag(done);
    CHECK(editor.Tools()[0].Area.Y > Phone.Y / 2.f);
    editor.Release();

    CHECK_NEAR(done.Y, CentreOf(editor.Areas().at(TouchPart::Pause)).Y, 1.f);
    CheckToolsClearOfButtons(editor);
}

TEST_CASE("TouchLayoutTests.TheToolsFindRoomBesideTheButtons")
{
    // A phone with a cut-out and rounded corners (an emulator's safe area): Pause in the top
    // row, the buttons' column on the right reaches the bottom row; the tools go beside it.
    RectangleF safe(206.f, 63.f, 2116.f, 933.f);
    TouchEditor editor(None);
    editor.Measure(Phone, safe, PhoneDensity);
    RectangleF pause = editor.Areas().at(TouchPart::Pause);
    editor.Press(CentreOf(pause));
    editor.Drag(Vector2(1200.f, CentreOf(pause).Y));
    editor.Release();

    CheckToolsClearOfButtons(editor);
    for (const auto& tool : editor.Tools())
    {
        CHECK(safe.Contains(tool.Area));
    }
}

TEST_CASE("TouchLayoutTests.CancellingChangesAsksFirst")
{
    TouchEditor editor = Editor();
    editor.Select(1);
    editor.Grow(1);
    CHECK(editor.Changed());

    // Cancel asks; Keep editing goes back, Discard ends.
    CHECK(TouchEditor::Outcome::Editing == editor.Press(ToolAt(editor, TouchEditor::Tool::Cancel)));
    CHECK(editor.Confirming());
    CHECK(TouchEditor::Outcome::Editing == editor.Press(ToolAt(editor, TouchEditor::Tool::Keep)));
    CHECK_FALSE(editor.Confirming());
    CHECK(TouchEditor::Outcome::Editing == editor.Cancel());
    CHECK(TouchEditor::Outcome::Cancelled == editor.Press(ToolAt(editor, TouchEditor::Tool::Discard)));
}

TEST_CASE("TouchLayoutTests.AnUnchangedLayoutCancelsAtOnce")
{
    TouchEditor editor = Editor();
    CHECK_FALSE(editor.Changed());
    CHECK(TouchEditor::Outcome::Cancelled == editor.Cancel());
}

TEST_CASE("TouchLayoutTests.WithholdingLetsThePointerGo")
{
    Input input;
    input.SetButton(MouseButton::Left, Input::State::Down);
    input.Withhold();
    CHECK_FALSE(input.IsDown(Pointer::Left));
}

// --- The editor ---------------------------------------------------------------------------

TEST_CASE("TouchLayoutTests.TheEditorStartsFromTheSavedPlaces")
{
    RectangleF area(1000.f, 400.f, 200.f, 200.f);
    TouchPlaces saved{{TouchPart::Jump, TouchLayout::PlaceOf(area, Phone, PhoneSafe, PhoneDensity)}};
    TouchEditor editor = Editor(saved);

    CHECK_NEAR(area.X, editor.Areas().at(TouchPart::Jump).X, Tolerance);
    CHECK(TouchLayout::DefaultArea(TouchPart::Fire, Phone, PhoneSafe, PhoneDensity) == editor.Areas().at(TouchPart::Fire));
    CHECK(saved == editor.Places());
}

TEST_CASE("TouchLayoutTests.DraggingAButtonMovesIt")
{
    TouchEditor editor = Editor();
    RectangleF before = editor.Areas().at(TouchPart::Fire);
    Vector2 grab = CentreOf(before) + Vector2(10.f, 10.f);

    CHECK(TouchEditor::Outcome::Editing == editor.Press(grab));
    CHECK(editor.Selected() == TouchPart::Fire);
    editor.Drag(grab - Vector2(500.f, 300.f));
    editor.Release();

    RectangleF after = editor.Areas().at(TouchPart::Fire);
    CHECK_NEAR(before.X - 500.f, after.X, Tolerance);
    CHECK_NEAR(before.Y - 300.f, after.Y, Tolerance);
    CHECK(before.Size() == after.Size());

    // Only the moved button is saved; the others keep their defaults.
    TouchPlaces places = editor.Places();
    REQUIRE(1 == places.size());
    CHECK(TouchPart::Fire == places.begin()->first);
    CHECK_NEAR(after.X, TouchLayout::Place(places[TouchPart::Fire], Phone, PhoneSafe, PhoneDensity).X, Tolerance);
}

TEST_CASE("TouchLayoutTests.DraggingStopsAtTheSafeArea")
{
    TouchEditor editor = Editor();
    Vector2 grab = CentreOf(editor.Areas().at(TouchPart::Fire));
    editor.Press(grab);
    editor.Drag(Vector2(-1000.f, -1000.f));
    CHECK(PhoneSafe.Left() == editor.Areas().at(TouchPart::Fire).X);
    CHECK(PhoneSafe.Top() == editor.Areas().at(TouchPart::Fire).Y);
}

TEST_CASE("TouchLayoutTests.TheHandleResizesTowardsTheMiddle")
{
    // Fire is at the bottom right: its handle is its top left corner, its bottom right stays.
    TouchEditor editor = Editor();
    RectangleF before = editor.Areas().at(TouchPart::Fire);
    editor.Press(CentreOf(before));
    editor.Release();
    REQUIRE(editor.Handle().has_value());
    RectangleF handle = *editor.Handle();
    CHECK(handle.Contains(before.Left(), before.Top()));

    editor.Press(Vector2(before.Left(), before.Top()));
    editor.Drag(Vector2(before.Left() - 60.f, before.Top() - 40.f));
    editor.Release();

    RectangleF after = editor.Areas().at(TouchPart::Fire);
    CHECK_NEAR(before.Width + 60.f, after.Width, Tolerance);
    CHECK(after.Width == after.Height);
    CHECK_NEAR(before.Right(), after.Right(), Tolerance);
    CHECK_NEAR(before.Bottom(), after.Bottom(), Tolerance);
}

TEST_CASE("TouchLayoutTests.TheZonesHandleIsWithinReach")
{
    // The zone fills the height: its corner is on the screen's edge, the handle keeps inside.
    TouchEditor editor = Editor();
    RectangleF before = editor.Areas().at(TouchPart::Stick);
    editor.Press(CentreOf(before));
    editor.Release();
    REQUIRE(editor.Handle().has_value());
    RectangleF handle = *editor.Handle();
    CHECK(PhoneSafe.Contains(handle));

    editor.Press(CentreOf(handle));
    editor.Drag(CentreOf(handle) - Vector2(100.f, 100.f));
    editor.Release();

    RectangleF after = editor.Areas().at(TouchPart::Stick);
    CHECK_NEAR(before.Width - 100.f, after.Width, Tolerance);
    CHECK_NEAR(before.Height - 100.f, after.Height, Tolerance);
    CHECK_NEAR(before.Top(), after.Top(), Tolerance);
}

TEST_CASE("TouchLayoutTests.PlusGrowsALargeZoneInLargerSteps")
{
    TouchEditor editor = Editor();
    editor.Press(CentreOf(editor.Areas().at(TouchPart::Stick)));
    editor.Release();
    float before = editor.Areas().at(TouchPart::Stick).Width;

    editor.Grow(-1);
    CHECK(before - editor.Areas().at(TouchPart::Stick).Width > 5.f * PhoneDensity);
}

TEST_CASE("TouchLayoutTests.PlusAndMinusResizeTheSelectedOne")
{
    TouchEditor editor = Editor();
    RectangleF before = editor.Areas().at(TouchPart::Jump);
    editor.Press(CentreOf(before));
    editor.Release();

    editor.Press(ToolAt(editor, TouchEditor::Tool::Grow));
    editor.Release();
    CHECK_NEAR(before.Width + PhoneDensity, editor.Areas().at(TouchPart::Jump).Width, Tolerance);
    CHECK(editor.Selected() == TouchPart::Jump);

    for (int i = 0; i < 20; i++)
    {
        editor.Grow(-1);
    }

    CHECK_NEAR(TouchLayout::MinMm * PhoneDensity, editor.Areas().at(TouchPart::Jump).Width, Tolerance);
}

TEST_CASE("TouchLayoutTests.KeysPickTheControls")
{
    TouchEditor editor = Editor();
    editor.Select(1);
    CHECK(editor.Selected() == TouchLayout::Parts().front());
    editor.Select(-1);
    CHECK(editor.Selected() == TouchLayout::Parts().back());
}

TEST_CASE("TouchLayoutTests.ButtonsDroppedOnEachOtherAreFlagged")
{
    TouchEditor editor = Editor();
    RectangleF jump = editor.Areas().at(TouchPart::Jump);
    RectangleF fire = editor.Areas().at(TouchPart::Fire);
    editor.Press(CentreOf(fire));
    editor.Drag(CentreOf(fire) - Vector2((fire.X - jump.X) / 2.f, 0.f));
    editor.Release();

    CHECK(editor.Overlapping(TouchPart::Fire));
    CHECK(editor.Overlapping(TouchPart::Jump));
    CHECK_FALSE(editor.Overlapping(TouchPart::Use));
}

TEST_CASE("TouchLayoutTests.ResetRestoresTheDefaults")
{
    TouchPlaces saved{{TouchPart::Jump, TouchPlace{Across::Left, 0.3f, Along::Top, 0.3f, 15.f, 15.f}}};
    TouchEditor editor = Editor(saved);

    editor.Press(ToolAt(editor, TouchEditor::Tool::Reset));
    editor.Release();
    CHECK(editor.Places().empty());
    for (TouchPart part : TouchLayout::Parts())
    {
        CHECK(TouchLayout::DefaultArea(part, Phone, PhoneSafe, PhoneDensity) == editor.Areas().at(part));
    }
}

TEST_CASE("TouchLayoutTests.DoneAndCancelEndTheEditor")
{
    const std::array<std::pair<TouchEditor::Tool, TouchEditor::Outcome>, 2> cases{{
        {TouchEditor::Tool::Done, TouchEditor::Outcome::Done},
        {TouchEditor::Tool::Cancel, TouchEditor::Outcome::Cancelled},
    }};
    for (auto [tool, outcome] : cases)
    {
        CAPTURE(static_cast<int>(tool));
        TouchEditor editor = Editor();
        CHECK(outcome == editor.Press(ToolAt(editor, tool)));
    }
}

TEST_CASE("TouchLayoutTests.ATapOnNothingLetsGo")
{
    TouchEditor editor = Editor();
    editor.Press(CentreOf(editor.Areas().at(TouchPart::Fire)));
    editor.Release();

    editor.Press(Vector2(1200.f, 500.f));
    CHECK_FALSE(editor.Selected().has_value());
    CHECK_FALSE(editor.Handle().has_value());
}

TEST_CASE("TouchLayoutTests.TheToolsKeepInsideTheSafeArea")
{
    TouchEditor editor = Editor();
    for (const auto& tool : editor.Tools())
    {
        CHECK(PhoneSafe.Contains(tool.Area));
        CHECK_FALSE(tool.Area.IntersectsWith(editor.Areas().at(TouchPart::Pause)));
    }
}

TEST_CASE("TouchLayoutTests.AnotherDisplayKeepsTheEdits")
{
    // The phone unfolds while editing: the moved button keeps its place on the inner display.
    TouchEditor editor = Editor();
    RectangleF fire = editor.Areas().at(TouchPart::Fire);
    editor.Press(CentreOf(fire));
    editor.Drag(CentreOf(fire) - Vector2(400.f, 0.f));
    editor.Release();
    TouchPlace place = editor.Places().at(TouchPart::Fire);

    editor.Measure(Inner, RectangleF::Empty(), InnerDensity);
    CHECK_NEAR(place.GapX, editor.Places().at(TouchPart::Fire).GapX, Tolerance);
    CHECK(TouchLayout::DefaultArea(TouchPart::Jump, Inner, RectangleF::Empty(), InnerDensity) == editor.Areas().at(TouchPart::Jump));
}
