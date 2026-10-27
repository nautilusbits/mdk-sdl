using System.Text;
using Mdk.Engine.Platform;
using Mdk.Game.Flow;

namespace Mdk.Game.Menu;

/// <summary>The save prompt after a level (0x42b520(1), each frame 0x42b75c): <c>SV_ASK</c> "Save
/// Game?" with <c>ABORT2</c> "Yes" and <c>ABORT3</c> "No", then <c>SV_TITLE</c> "Name for Saved
/// Game" with the name typed below it (up to 8 letters, digits, <c>_</c> and <c>$</c>), preset to
/// the next level's number. Enter saves, Esc gives up. <c>FONTBIG</c> on black.
/// <code>
///   Save Game?        y 139
///       Yes           y 175   the selected line grows from 65 % to full size in 5 ticks
///       No            y 211
/// </code></summary>
public sealed class SavePrompt
{
    private enum Stage { Ask, Name }

    private const float TicksPerSecond = 30f;
    private const float RowY = 175f;
    private const float RowHeight = 36f;
    /// <summary>The rows under the pointer start 26 above the first baseline.</summary>
    private const float RowTop = RowY - RowHeight + 10f;
    private static readonly string[] Answers = ["ABORT2", "ABORT3"];
    private const int Yes = 0;
    private const float Small = 0.65f;
    private const float GrowTicks = 5f;
    // The name: the title's baseline, then one character per 28 pixels from x 202, baseline 200.
    private const float TitleY = 31f;
    private const float NameX = 202f;
    private const float NameY = 200f;
    private const float NameStep = 28f;
    public const int NameLength = 8;
    private const string NameExtra = "_$";
    /// <summary>The cursor blinks every 8 frames (0x5742e8 &amp; 8).</summary>
    private const int BlinkTicks = 8;

    private readonly Ui _ui;
    private readonly Fonts _fonts;
    private readonly SaveGames _saves;
    private readonly SaveGame _save;
    private Stage _stage = Stage.Ask;
    private int _selected = Yes;
    private float _grow;
    private string _name;
    private int _cursor;
    private float _ticks;

    public bool Closed { get; private set; }
    /// <summary>Closed after writing the save, under <see cref="Name"/>.</summary>
    public bool Saved { get; private set; }
    public string Name => _name;
    /// <summary>A touch screen's keyboard shows while the name is typed.</summary>
    public Typing Typing => _stage == Stage.Name && !Closed ? Typing.On : Typing.Off;

    /// <summary>Asks whether to save <paramref name="save"/>, offering <paramref name="name"/>.</summary>
    public SavePrompt(Ui ui, Fonts fonts, SaveGames saves, SaveGame save, string name)
    {
        _ui = ui;
        _fonts = fonts;
        _saves = saves;
        _save = save;
        _name = name.Length > NameLength ? name[..NameLength] : name;
        // A full save (F2) asks for the name at once.
        _stage = save.Kind == SaveKind.Snapshot ? Stage.Name : Stage.Ask;
        _cursor = _name.Length;
    }

    public void Update(Input input, float delta)
    {
        _ticks += delta * TicksPerSecond;
        _grow = MathF.Min(_grow + delta * TicksPerSecond, GrowTicks);
        if (_stage == Stage.Ask)
        {
            Ask(input);
            return;
        }

        TypeName(input);
    }

    /// <summary>Yes or No: up and down or the mouse choose, Enter, Space or a click confirms, Esc says no.</summary>
    private void Ask(Input input)
    {
        if (input.PointerMoved || input.WasClicked(Pointer.Left))
        {
            var row = (int)MathF.Floor((_ui.View.Pointer(input).Y - RowTop) / RowHeight);
            if (row >= 0 && row < Answers.Length)
            {
                Select(row);
            }
        }

        if (input.WasPressed(MenuKey.Up) || input.WasPressed(MenuKey.Down))
        {
            Select(1 - _selected);
        }

        if (input.WasPressed(MenuKey.Accept) || input.WasClicked(Pointer.Left))
        {
            Answer();
        }
        else if (input.WasPressed(MenuKey.Back))
        {
            Closed = true;
        }
    }

    private void Select(int row)
    {
        if (row == _selected)
        {
            return;
        }

        _selected = row;
        _grow = 0f;
    }

    private void Answer()
    {
        if (_selected != Yes)
        {
            Closed = true;
            return;
        }

        _stage = Stage.Name;
    }

    /// <summary>Typed characters replace the one at the cursor; Backspace and Delete remove one,
    /// Left, Right, Home and End move the cursor.</summary>
    private void TypeName(Input input)
    {
        if (input.WasPressed(MenuKey.Back))
        {
            Closed = true;
            return;
        }

        // Enter saves; Space (also an Accept key) doesn't.
        if (input.WasPressed(MenuKey.Accept) && !input.Typed.Contains(' '))
        {
            Save();
            return;
        }

        if (input.WasPressed(MenuKey.Left))
        {
            _cursor = Math.Max(_cursor - 1, 0);
        }

        if (input.WasPressed(MenuKey.Right))
        {
            _cursor = Math.Min(_cursor + 1, _name.Length);
        }

        if (input.WasPressed(MenuKey.Home))
        {
            _cursor = 0;
        }

        if (input.WasPressed(MenuKey.End))
        {
            _cursor = _name.Length;
        }

        if (input.WasPressed(MenuKey.Delete) && _cursor < _name.Length)
        {
            _name = _name.Remove(_cursor, 1);
        }

        foreach (var c in input.Typed.ToUpperInvariant())
        {
            Type(c);
        }
    }

    private void Type(char c)
    {
        // Backspace, in order with the text (a phone keyboard's corrections).
        if (c == Input.Erase)
        {
            if (_cursor > 0)
            {
                _name = _name.Remove(_cursor - 1, 1);
                _cursor--;
            }

            return;
        }

        if (_cursor >= NameLength || !(char.IsAsciiLetterOrDigit(c) || NameExtra.Contains(c)))
        {
            return;
        }

        _name = _name[.._cursor] + c + (_cursor + 1 < _name.Length ? _name[(_cursor + 1)..] : "");
        _cursor++;
    }

    private void Save()
    {
        if (_name.Length == 0 || !_saves.Write(_name, _save))
        {
            return;
        }

        Console.WriteLine($"Saved game {_name}: level {_save.Level}");
        Saved = true;
        Closed = true;
    }

    public void Draw()
    {
        var view = _ui.View;
        var font = _fonts.Big;
        view.FillCanvas(Ui.Black);
        if (_stage == Stage.Ask)
        {
            DrawLine("SV_ASK", -1, Small);
            for (var row = 0; row < Answers.Length; row++)
            {
                var grow = row == _selected ? Small + (1f - Small) * _grow / GrowTicks : Small;
                DrawLine(Answers[row], row, grow);
            }

            return;
        }

        // The title, the name one character per step, and the blinking cursor.
        var title = _ui.Fti.GetTextBytes("SV_TITLE");
        view.Text(font, title, (ScreenView.Size.X - font.Width(title)) * 0.5f, TitleY);
        for (var i = 0; i < _name.Length; i++)
        {
            var glyph = Encoding.ASCII.GetBytes(_name[i].ToString());
            view.Text(font, glyph, NameX + NameStep * i - font.Width(glyph) * 0.5f, NameY);
        }

        if (((int)_ticks & BlinkTicks) != 0)
        {
            var cursor = "_"u8;
            view.Text(font, cursor, NameX + NameStep * _cursor - font.Width(cursor) * 0.5f, NameY);
        }
    }

    /// <summary>A line centred at row <paramref name="row"/>, scaled about its centre.</summary>
    private void DrawLine(string name, int row, float scale)
    {
        var text = _ui.Fti.GetTextBytes(name);
        var font = _fonts.Big;
        _ui.View.Text(font, text, (ScreenView.Size.X - font.Width(text) * scale) * 0.5f, RowY + RowHeight * row, scale);
    }
}
