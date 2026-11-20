#pragma once

#include <functional>
#include <memory>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Engine/Platform/TouchLayout.h"
#include "Mdk.Game/Hud/TouchView.h"
#include "Mdk.Game/Menu/TouchEditor.h"

namespace Mdk::Engine::Platform
{
    class Input;
}

namespace Mdk::Game::Menu
{
    class Ui;

    // The touch layout editor's page (TouchEditor): the controls over the dimmed screen, driven by
    // the pointer (a finger in the menus) and by keys: Up/Down pick a control, Left/Right shrink or
    // grow it, Delete resets, Enter is Done; Back cancels (asking first when something changed). A
    // help line, a warning while buttons overlap, and the look area's mark.
    class TouchLayoutPage
    {
    public:
        TouchLayoutPage(Ui& ui, Hud::TouchView& view, std::function<void()> back);

        void Update();

        // The zone under the buttons, the buttons, the selected one's handle, the tools.
        void Draw();

    private:
        static constexpr Vector4 Dim{0.f, 0.f, 0.f, 0.75f};
        static constexpr const char* Help = "DRAG TO MOVE - HANDLE OR +/- TO RESIZE";
        static constexpr const char* OverlapWarning = "RED BUTTONS OVERLAP: ONE TOUCH PRESSES BOTH";
        static constexpr const char* Question = "DISCARD CHANGES?";
        static constexpr const char* LookLabel = "LOOK";
        // The look area's mark: a part of the space right of the stick's zone.
        static constexpr float LookPart = 0.5f;

        Ui& _ui;
        Hud::TouchView& _view;
        std::shared_ptr<TouchEditor> _editor;
        std::function<void()> _back;
        // The question was printed (once per asking, for tests).
        bool _asked = false;

        TouchEditor::Outcome Keys(Engine::Platform::Input& input);

        // Done: the edited controls' places replace the saved ones.
        void Save();

        void Measure();

        // "LOOK" in the space right of the stick's zone, where a drag turns the view.
        void DrawLook(float side);

        Hud::TouchView::Shade ShadeOf(Engine::Platform::TouchPart part) const;
    };
}
