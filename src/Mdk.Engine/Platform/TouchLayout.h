#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Mdk.Core/Drawing.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/Input.h"

namespace Mdk::Engine::Platform
{
    // An on-screen control the player can place: a button, or the stick's zone.
    enum class TouchPart { Fire, Jump, Use, Sniper, Item, ZoomIn, ZoomOut, Pause, Stick };

    // The safe area's edge a placed control keeps its distance to.
    enum class Across { Left, Right };

    enum class Along { Top, Bottom };

    // A placed control: its gaps to the nearer edges of the safe area (parts of its width and
    // height) and its size in millimetres. Kept apart from the pixels, it fits another display (a
    // foldable's other screen, a tablet).
    //   touch.Fire=Right,0.0113,Bottom,0.2,9.8,9.8
    //   ┌ safe area ──────────────────┐
    //   │                  gap x ─►┌──┤  gap x × width from the right edge
    //   │                          └──┤
    //   │                  gap y × height from the bottom
    //   └─────────────────────────────┘
    struct TouchPlace
    {
        Across X = Across::Left;
        float GapX = 0;
        Along Y = Along::Top;
        float GapY = 0;
        float WidthMm = 0;
        float HeightMm = 0;

        std::string ToString() const;

        // A place of its text (ToString); false for a bad one.
        static bool TryParse(const std::string& text, TouchPlace& place);

        bool operator==(const TouchPlace&) const = default;

    private:
        static constexpr char Separator = ',';
        static constexpr int Fields = 6;
        // Sizes past this are a broken line, not a control.
        static constexpr float MaxSizeMm = 1000.0f;

        static std::string Number(float value);
        static bool Fraction(const std::string& text, float& value);
        static bool Size(const std::string& text, float& value);
    };

    // C#'s IReadOnlyDictionary<TouchPart, TouchPlace> (looked up, compared; never iterated for order).
    using TouchPlaces = std::map<TouchPart, TouchPlace>;

    // Where the on-screen controls are (window pixels): the defaults, or the player's places
    // (TouchPlace) fitted to the display. The defaults:
    //   ┌───────────────────────────────────────────────┐
    //   │ stick zone (left 40 %)                  [ ||] │  pause
    //   │                                  [ - ] [ + ] [ITEM]
    //   │                                        [SNIPE] [USE ]
    //   │                                        [JUMP ] [FIRE]
    //   │                                                (radar)
    //   └───────────────────────────────────────────────┘
    // Placed controls keep inside the safe area, 7 to 25 mm (the zone: the stick's reach to 60 % of
    // its width).
    struct TouchLayout
    {
        // A placed button's size: at least, at most, and at most this part of the safe area's height.
        static constexpr float MinMm = 7.0f;
        static constexpr float MaxMm = 25.0f;

        // The buttons in the order they're hit (drawn the other way: the first on top), then the
        // stick's zone.
        static const std::vector<TouchPart>& Parts();

        static bool IsButton(TouchPart part) { return part != TouchPart::Stick; }

        static Key KeyOf(TouchPart part);

        static std::string LabelOf(TouchPart part);

        // Window units per millimetre; on an unknown display, those making a default button 11 mm
        // (its part of the height).
        static float UnitsPerMm(float unitsPerMm, Vector2 screen);

        // A control's area, placed (and fitted) or the default.
        static RectangleF AreaOf(TouchPart part, const TouchPlaces& places, Vector2 screen, RectangleF safe, float unitsPerMm);

        // The default area: the buttons from the safe area's right edge, over the radar's room (its
        // height's part); the stick's zone the screen's left part.
        static RectangleF DefaultArea(TouchPart part, Vector2 screen, RectangleF safe, float unitsPerMm);

        // A place's area on this display (not fitted).
        static RectangleF Place(TouchPlace place, Vector2 screen, RectangleF safe, float unitsPerMm);

        // An area's place: its gaps to the safe area's nearer edges, its size in millimetres.
        static TouchPlace PlaceOf(RectangleF area, Vector2 screen, RectangleF safe, float unitsPerMm);

        // An area kept to its sizes (a button square, 7-25 mm and at most half the safe area's
        // height; the zone at least the stick's reach) and inside the safe area.
        static RectangleF Fit(TouchPart part, RectangleF area, Vector2 screen, RectangleF safe, float unitsPerMm);

        // Whether a button covers part of another (the zone lies under them: buttons win).
        static bool Overlaps(TouchPart part, const std::map<TouchPart, RectangleF>& areas);

    private:
        // Sizes in parts of the screen's height (the largest; all of it when the display's density
        // is unknown).
        static constexpr float ButtonSize = 0.15f;
        static constexpr float Margin = 0.025f;
        // Sizes in millimetres: a fingertip's button, the gap between buttons.
        static constexpr float ButtonMm = 11.0f;
        static constexpr float MarginMm = 3.0f;
        // Room under the buttons for the HUD's radar (bottom right).
        static constexpr float RadarSpace = 0.2f;
        // The stick's zone: the left part of the screen's width.
        static constexpr float StickSide = 0.4f;
        static constexpr float MaxPart = 0.5f;
        // The stick's zone leaves room to look: at most this part of the safe area's width.
        static constexpr float MaxZone = 0.6f;

        // The stick's zone has no key; the editor names it.
        static constexpr const char* StickLabel = "MOVE";

        struct Slot
        {
            TouchPart Part;
            Platform::Key Key;
            const char* Label;
            int Column;
            int Row;
        };

        static const Slot& SlotOf(TouchPart part);

        // The safe area, or all of the screen when it's empty (unknown).
        static RectangleF SafeOf(Vector2 screen, RectangleF safe);

        static std::pair<float, float> ButtonSide(RectangleF area, Vector2 screen, RectangleF bounds, float unitsPerMm);
        static std::pair<float, float> ZoneSize(RectangleF area, Vector2 screen, RectangleF bounds, float unitsPerMm);
    };
}
