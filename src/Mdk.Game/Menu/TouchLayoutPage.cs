using System.Numerics;
using Mdk.Engine.Platform;
using Mdk.Game.Hud;

namespace Mdk.Game.Menu;

/// <summary>The touch layout editor's page (<see cref="TouchEditor"/>): the controls over the dimmed
/// screen, driven by the pointer (a finger in the menus) and by keys: Up/Down pick a control,
/// Left/Right shrink or grow it, Delete resets, Enter is Done; Back (the menus') cancels.</summary>
internal sealed class TouchLayoutPage
{
    private static readonly Vector4 Dim = new(0f, 0f, 0f, 0.75f);

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

        return input.WasPressed(MenuKey.Accept) ? TouchEditor.Outcome.Done : TouchEditor.Outcome.Editing;
    }

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

        // The tools brighter than the controls (theirs are + and -).
        foreach (var (tool, area) in _editor.Tools)
        {
            _view.Button(area, TouchEditor.LabelOf(tool), TouchView.Shade.Held);
        }
    }

    private TouchView.Shade ShadeOf(TouchPart part) =>
        _editor.Overlapping(part) ? TouchView.Shade.Overlap
        : part == _editor.Selected ? TouchView.Shade.Selected
        : TouchView.Shade.Idle;
}
