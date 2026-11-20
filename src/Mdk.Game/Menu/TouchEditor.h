#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "Mdk.Core/Drawing.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/TouchLayout.h"
#include "Mdk.Game/DevTools/ICommandTarget.h"

namespace Mdk::Game::Menu
{
    // Editing where the on-screen controls are (Options, Touch, "Edit touch layout"): a
    // control is dragged by its middle, resized by its handle (the corner facing the screen's middle;
    // the opposite corner stays) or by Smaller and Bigger; Reset brings the defaults back, Done keeps
    // the edits, Cancel drops them. Only the controls moved or resized are kept (Places): the
    // others follow the defaults on any display. The tools' row moves to the bottom while a control
    // is dragged into the top half (and back), and keeps clear of the buttons after, so a control
    // can go anywhere. Cancel with changes asks first (Discard, Keep editing). Window pixels.
    //      [RESET] [SMALLER] [BIGGER] [CANCEL] [DONE]
    //   ┌ MOVE ──────┐                          [ ||]
    //   │            │                    o──────┐
    //   │            │                    │ FIRE │   o: the selected one's handle
    //   └────────────┘                    └──────┘
    class TouchEditor
    {
    public:
        using TouchPart = Engine::Platform::TouchPart;
        using TouchPlace = Engine::Platform::TouchPlace;

        enum class Tool { Reset, Shrink, Grow, Cancel, Done, Discard, Keep };
        enum class Outcome { Editing, Done, Cancelled };

        // A tool's button and where it is.
        struct ToolArea
        {
            TouchEditor::Tool Tool{};
            RectangleF Area;
        };

        explicit TouchEditor(const std::map<TouchPart, TouchPlace>& saved);

        static std::string LabelOf(Tool tool);

        // The places differ from the saved ones (Cancel asks first).
        bool Changed() const;

        // "Discard changes?" is asked: only its buttons work (Tools). Read-only outside (C#: private set).
        bool Confirming() const { return _confirming; }

        // The help line's area: beside the tools' row, towards the middle.
        RectangleF HelpArea() const { return _helpArea; }

        const std::map<TouchPart, RectangleF>& Areas() const { return _areas; }

        const std::vector<ToolArea>& Tools() const { return _tools; }

        std::optional<TouchPart> Selected() const { return _selected; }

        // The selected control's handle, or null.
        std::optional<RectangleF> Handle() const;

        bool Overlapping(TouchPart part) const { return Engine::Platform::TouchLayout::Overlaps(part, _areas); }

        // The kept controls' places (see TouchLayout).
        std::map<TouchPart, TouchPlace> Places() const { return _places; }

        // The display edited on; another one (a rotation, a fold) lays the controls out again.
        void Measure(Vector2 screen, RectangleF safe, float unitsPerMm);

        // A finger lands (or a click): a tool, the selected one's handle, a control (the
        // buttons over the zone), or nothing (none selected). Asked, only the question's buttons.
        Outcome Press(Vector2 at);

        // The finger moves: the grabbed control follows.
        void Drag(Vector2 at);

        // The finger lifts: the tools' row keeps clear of the buttons where it can.
        void Release();

        // The selected control grows (+1) or shrinks (-1) about its middle.
        void Grow(int step);

        // The next (+1) or previous (-1) control (keys, a gamepad).
        void Select(int step);

        // The defaults again (kept by Done).
        void Reset();

        // Cancel (its tool, Back): ends at once without changes, else asks first; asked
        // already, Back keeps editing.
        Outcome Cancel();

        // Asked, Enter discards the changes; else it's Done.
        Outcome Accept() const { return _confirming ? Outcome::Cancelled : Outcome::Done; }

    private:
        enum class Grab { None, Move, Resize };

        // The safe area's edge the tools' row stands at (while a control is dragged).
        enum class Edge { Top, Bottom };

        // Where the tools' row stands across the safe area.
        enum class Across { Centre, Left, Right };

        struct ToolSlot
        {
            TouchEditor::Tool Tool;
            const char* Label;
            float Width;
        };

        // + and - grow by a millimetre, or this part of the larger side (a zone: in fewer taps).
        static constexpr float StepMm = 1.f;
        static constexpr float StepPart = 0.1f;
        // The handle's side, the tools' height and their gaps (millimetres).
        static constexpr float HandleMm = 7.f;
        static constexpr float ToolMm = 9.f;
        static constexpr float GapMm = 2.f;
        // The tools' widths in their height.
        static constexpr float WideTool = 2.f;
        // The tools take at most this part of the safe area's width.
        static constexpr float ToolsPart = 0.9f;
        static constexpr std::array<ToolSlot, 5> ToolSlots{{
            {Tool::Reset, "RESET", WideTool}, {Tool::Shrink, "SMALLER", WideTool}, {Tool::Grow, "BIGGER", WideTool},
            {Tool::Cancel, "CANCEL", WideTool}, {Tool::Done, "DONE", WideTool},
        }};
        // The question's buttons, in the safe area's middle.
        static constexpr std::array<ToolSlot, 2> PromptSlots{{
            {Tool::Discard, "DISCARD", WideTool * 1.5f}, {Tool::Keep, "KEEP EDITING", WideTool * 2.f},
        }};
        // The help line's height, a part of a tool's.
        static constexpr float HelpPart = 0.45f;

        // The controls moved or resized (by their places), and every control's area.
        std::map<TouchPart, TouchPlace> _places;
        std::map<TouchPart, TouchPlace> _saved;
        Edge _edge{};
        // The row's top and where it stands across after a drop moved it clear of the
        // buttons, or null (at its edge, in the middle).
        std::optional<float> _rowTop;
        Across _rowAcross = Across::Centre;
        std::map<TouchPart, RectangleF> _areas;
        std::vector<ToolArea> _tools;
        Vector2 _screen;
        RectangleF _safe;
        float _unitsPerMm = 0;
        bool _measured = false;
        Grab _grab{};
        // A move: the finger's offset from the area's corner; a resize: the corner that stays,
        // and the finger's offset to the moving corner (the handle keeps inside the screen).
        Vector2 _anchor;
        Vector2 _offset;
        bool _confirming = false;
        RectangleF _helpArea;
        std::optional<TouchPart> _selected;

        void MoveTools(Edge edge);

        // The row covers a button (it couldn't be picked again): the other edge, else the
        // nearest height clear of the buttons, centred or to a side; none clear, it stays.
        //        [||]                        [||]
        //               [- +]  ─►                   [- +]
        //     [TOOLS....JUMP]           [TOOLS]     [JUMP]   (the bottom, to the left)
        void AvoidButtons();

        bool CoversButton() const;

        void Ask(DevTools::Switch asking);

        Outcome Use(Tool tool);

        // A control's new area, fitted, and kept as a place. An empty window (mid-rotation)
        // changes nothing.
        void Change(TouchPart part, RectangleF area);

        // The area from the staying corner to the finger: a button square (its larger side).
        RectangleF Resized(TouchPart part, Vector2 at) const;

        // The handle's corner: true for the left (top) one, when the area is right of (below) the
        // safe area's middle.
        std::tuple<bool, bool> HandleCorner(RectangleF area) const;

        RectangleF HandleOf(RectangleF area) const;

        RectangleF ToolsArea() const;

        RectangleF Bounds() const;

        // The tools in a row at the safe area's top or bottom middle, smaller when they
        // don't fit, the help line beside them; asked, the question's buttons in the middle.
        void LayTools();
    };
}
