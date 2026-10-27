using System.Drawing;
using System.Numerics;
using Mdk.Engine.Platform;
using Mdk.Game.Hud;

namespace Mdk.Game.Menu;

/// <summary>The touch layout editor's page (<see cref="TouchEditor"/>): the controls over the dimmed
/// screen, driven by the pointer (a finger in the menus) and by keys: Up/Down pick a control,
/// Left/Right shrink or grow it, Delete resets, Enter is Done; Back cancels (asking first when
/// something changed). A help line, a warning while buttons overlap, and the look area's mark.</summary>
internal sealed class TouchLayoutPage
{
    private static readonly Vector4 Dim = new(0f, 0f, 0f, 0.75f);
    private const string Help = "DRAG TO MOVE - HANDLE OR +/- TO RESIZE";
    private const string OverlapWarning = "RED BUTTONS OVERLAP: ONE TOUCH PRESSES BOTH";
    private const string Question = "DISCARD CHANGES?";
    private const string LookLabel = "LOOK";
    /// <summary>The look area's mark: a part of the space right of the stick's zone.</summary>
    private const float LookPart = 0.5f;

    private readonly Ui _ui;
    private readonly TouchView _view;
    private readonly TouchEditor _editor;
    private readonly Action _back;

    public TouchLayoutPage(Ui ui, TouchView view, Action back)
    {
        _ui = ui;
        _view = view;
        _back = back;
        _editor = new TouchEditor(ui.Settings.TouchPlaces);
        Measure();
        Console.WriteLine($"Touch layout: {string.Join(" | ", TouchLayout.Parts.Select(TouchLayout.LabelOf))}");
    }

    public void Update()
    {
        Measure();
        var input = _ui.Input;
        var outcome = Keys(input);
        var at = new Vector2(input.PointerX, input.PointerY);
        if (input.WasClicked(Pointer.Left))
        {
            outcome = _editor.Press(at);
        }
        else if (input.IsDown(Pointer.Left))
        {
            _editor.Drag(at);
        }
        else
        {
            _editor.Release();
        }

        if (input.WasPressed(MenuKey.Back))
        {
            outcome = _editor.Cancel();
        }

        if (_editor.Confirming && !_asked)
        {
            Console.WriteLine($"Touch layout: {Question}");
        }

        _asked = _editor.Confirming;
        if (outcome == TouchEditor.Outcome.Done)
        {
            Save();
        }

        if (outcome != TouchEditor.Outcome.Editing)
        {
            _back();
        }
    }

    private TouchEditor.Outcome Keys(Input input)
    {
        if (input.WasPressed(MenuKey.Up))
        {
            _editor.Select(-1);
        }

        if (input.WasPressed(MenuKey.Down))
        {
            _editor.Select(1);
        }

        if (input.WasPressed(MenuKey.Left))
        {
            _editor.Grow(-1);
        }

        if (input.WasPressed(MenuKey.Right))
        {
            _editor.Grow(1);
        }

        if (input.WasPressed(MenuKey.Delete))
        {
            _editor.Reset();
        }

        return input.WasPressed(MenuKey.Accept) ? _editor.Accept() : TouchEditor.Outcome.Editing;
    }

    /// <summary>The question was printed (once per asking, for tests).</summary>
    private bool _asked;

    /// <summary>Done: the edited controls' places replace the saved ones.</summary>
    private void Save()
    {
        var places = _ui.Settings.TouchPlaces;
        places.Clear();
        foreach (var (part, place) in _editor.Places())
        {
            places[part] = place;
        }

        _ui.ApplySettings();
        Console.WriteLine($"Touch layout saved: {string.Join(" | ", places.Keys)}");
    }

    private void Measure()
    {
        var window = _ui.Window;
        var (width, height) = window.Size;
        _editor.Measure(new Vector2(width, height), window.SafeArea, window.UnitsPerMm);
    }

    /// <summary>The zone under the buttons, the buttons, the selected one's handle, the tools.</summary>
    public void Draw()
    {
        // At full opacity: the selected and the overlapping ones must show whatever play's opacity.
        _ui.View.FillCanvas(Dim);
        // The zone is drawn as large as a default button's corners and label.
        var window = _ui.Window;
        var (width, height) = window.Size;
        var side = TouchLayout.DefaultArea(TouchPart.Fire, new Vector2(width, height), window.SafeArea, window.UnitsPerMm).Width;
        foreach (var part in TouchLayout.Parts.Reverse())
        {
            _view.Button(_editor.Areas[part], TouchLayout.LabelOf(part), ShadeOf(part), TouchLayout.IsButton(part) ? null : side);
        }

        if (_editor.Handle is { } handle)
        {
            _view.Disc(handle);
        }

        // The tools brighter than the controls (theirs are + and -); asked, over a darker screen.
        if (_editor.Confirming)
        {
            _ui.View.FillCanvas(Dim);
        }
        else
        {
            DrawLook(side);
        }

        foreach (var (tool, area) in _editor.Tools)
        {
            _view.Button(area, TouchEditor.LabelOf(tool), TouchView.Shade.Held);
        }

        var overlap = TouchLayout.Parts.Any(_editor.Overlapping);
        _view.Caption(_editor.HelpArea, _editor.Confirming ? Question : overlap ? OverlapWarning : Help);
    }

    /// <summary>"LOOK" in the space right of the stick's zone, where a drag turns the view.</summary>
    private void DrawLook(float side)
    {
        var window = _ui.Window;
        var safe = window.SafeArea;
        var zone = _editor.Areas[TouchPart.Stick];
        var width = (safe.Right - zone.Right) * LookPart;
        if (width <= 0f)
        {
            return;
        }

        var height = side * LookPart;
        var x = zone.Right + (safe.Right - zone.Right - width) / 2f;
        var y = safe.Y + (safe.Height - height) / 2f;
        _view.Caption(new RectangleF(x, y, width, height), LookLabel);
    }

    private TouchView.Shade ShadeOf(TouchPart part) =>
        _editor.Overlapping(part) ? TouchView.Shade.Overlap
        : part == _editor.Selected ? TouchView.Shade.Selected
        : TouchView.Shade.Idle;
}
