using System.Drawing;
using System.Globalization;
using System.Numerics;
using System.Text;
using Mdk.Engine.Audio;
using Mdk.Engine.Platform;
using Mdk.Engine.Render;
using Mdk.Formats;
using Mdk.Game.DevTools;
using Mdk.Game.Flow;
using Mdk.Game.HdTextures;
using Mdk.Game.Hud;
using Mdk.Game.Kurt;
using Mdk.Game.Mods;

namespace Mdk.Game.Menu;

/// <summary>A list of menu items drawn like the original's menus (menu_items.gd, menu_entry.gd,
/// 0x42c374): <c>FONTBIG</c> in <c>SYS_PAL</c>, unselected items at 65 %, the selected one growing
/// to full size in 5 ticks. Rows of 36 from y 5 on the 600 x 360 view (baselines at 31 + 36 x row,
/// 0x4265c0); longer pages get lower rows, and below 24 the small font. Also the options and
/// controls pages shared by the main menu and the pause menu.
/// <code>
///   main menu: a column at the view's left edge, items centred on its width
///   other pages: centred on x 300
/// </code></summary>
public sealed class MenuItems
{
    /// <summary>Where the column of items stands on the view.</summary>
    public enum Align { Left, Centre }

    private enum Kind { Item, Title }

    /// <summary>What a binding waits for: a key or mouse button, or a gamepad button.</summary>
    private enum Device { Keyboard, Gamepad }

    private sealed class Entry(string text, Kind kind, Action? press, Action<int>? change)
    {
        public string Text = text;
        public readonly Kind Kind = kind;
        public readonly Action? Press = press;
        /// <summary>An option: changed by a step (+1 or -1).</summary>
        public readonly Action<int>? Change = change;
        public bool Disabled;
        /// <summary>An option's label, made again when an option changes.</summary>
        public Func<string>? Label;
        public float Grow = Small;
        public RectangleF Area;

        public bool Selectable => Kind == Kind.Item && !Disabled;
    }

    private const float Top = 5f;
    private const float Row = 36f;
    /// <summary>The baseline is 26 below the row's top.</summary>
    private const float Baseline = 26f / 36f;
    private const float SmallFontRow = 24f;
    /// <summary>A touch screen's rows grow towards this height (millimetres), at most by the factor.</summary>
    private const float TouchRowMm = 10f;
    private const float MaxEnlargement = 1.4f;
    /// <summary>A touch screen's long page scrolls; it shows at least this many rows.</summary>
    private const int MinScrollRows = 8;
    /// <summary>The scroll bar on the view's right edge (view units) and its colours.</summary>
    private const float ScrollBarWidth = 4f;
    private static readonly Vector4 ScrollTrack = new(1f, 1f, 1f, 0.15f);
    private static readonly Vector4 ScrollThumb = new(1f, 1f, 1f, 0.6f);
    private const float Small = 0.65f;
    /// <summary>0.35 x 0.2 per tick.</summary>
    private const float GrowRate = 0.07f * 30f;
    private const float DisabledAlpha = 0.5f;
    private const int VolumeIncrement = 10;
    private static readonly float[] Sensitivities = [0.25f, 0.5f, 0.75f, 1f, 1.25f, 1.5f, 2f, 2.5f, 3f];
    private const int DefaultSensitivity = 3;
    /// <summary>Longer mod names are cut (the row's width).</summary>
    private const int MaxModName = 24;
    private static readonly string[] DifficultyNames = ["Easy", "Normal", "Hard"];
    private static readonly string[] AntiAliasingNames = ["Off", "2x", "4x"];
    /// <summary>The stereo sliders: a held Left/Right waits, then steps again.</summary>
    private const float RepeatWait = 0.4f;
    private const float RepeatRate = 0.06f;
    private const float SeparationStep = 0.05f;
    private const float ConvergenceRate = 1.15f;
    private static readonly int GraphicsCount = Enum.GetValues<Graphics>().Length;
    /// <summary>The options' sounds (0x42bb6c): the <c>OPTSONG</c> loop on the music bus and
    /// <c>OPTBUTT</c> on each change.</summary>
    private const string OptionSounds = "MISC/MDKSOUND.SNI";

    private readonly Ui _ui;
    private readonly Fonts _fonts;
    private readonly List<Entry> _entries = [];
    private readonly Sound? _click;
    private readonly float _clickGain;
    private readonly Sound? _song;
    private readonly Sound? _change;
    private int _songVoice;
    private int _selected = -1;
    /// <summary>The controls page waits for a key, or a gamepad button, for this action (from the next
    /// frame on).</summary>
    private Key? _waiting;
    private Device _waitingDevice;
    private Entry? _waitingEntry;
    private bool _armed;
    /// <summary>The time to the next step of an option whose Left/Right is held.</summary>
    private float _repeat;
    /// <summary>The HD textures being made, and the page's lines that follow it.</summary>
    private HdJob? _job;
    private Entry? _jobLine;
    private Entry? _jobBack;
    /// <summary>The touch layout editor while it's the page, and its drawing (made once).</summary>
    private TouchLayoutPage? _touchPage;
    private TouchView? _touchView;
    private static readonly int[] TouchOpacities = [Settings.MinOpacity, 50, 75, Settings.MaxOpacity];

    /// <summary>The rows' height as the original's (it picks the font), and the texts' scale on
    /// it (1, larger on a touch screen's grown rows).</summary>
    private float _originalRow;
    private float _textScale = 1f;
    /// <summary>A scrolling page (a touch screen's long one): its row, the first row shown, how many
    /// show, and a drag in progress (where it started, and whether it moved: a tap presses on release).</summary>
    private float _row;
    private int _scrollRow;
    private int _shownRows;
    private bool _reveal;
    private Vector2? _dragStart;
    private int _dragScroll;
    private bool _dragged;

    private bool Scrolls => _shownRows > 0;

    public Align Alignment = Align.Centre;
    /// <summary>The row the first item stands in.</summary>
    public int FirstRow;
    public bool Visible = true;

    public MenuItems(Ui ui, Fonts fonts, float clickGain = 1f)
    {
        _ui = ui;
        _fonts = fonts;
        _clickGain = clickGain;
        _click = Ui.SoundOf(ui.Fti.GetBytes("SND_PUSH"));
        MenuCursor.Apply(ui);
        var path = ui.Data.PathOf(OptionSounds);
        if (!File.Exists(path))
        {
            return;
        }

        var sounds = Sni.Load(path);
        _song = SniSound(sounds, "OPTSONG", Looping.Forever);
        _change = SniSound(sounds, "OPTBUTT", Looping.Once);
    }

    private static Sound? SniSound(Sni sni, string name, Looping looping)
    {
        var entry = sni.Entries.FirstOrDefault(e => e.Key == name).Value;
        return entry == null ? null : Ui.SoundOf(sni.GetBytes(entry), looping);
    }

    /// <summary>The texts of the page, for tests.</summary>
    public string Describe() => string.Join(" | ", _entries.Select(e => e.Text));

    public void Clear()
    {
        _ui.Audio.Stop(_songVoice);
        _songVoice = 0;
        ClearPage();
    }

    /// <summary>A new page; the options' song plays on across their pages.</summary>
    private void ClearPage()
    {
        // Leaving the progress page stops the HD textures.
        _job?.Cancel();
        _job = null;
        _touchPage = null;
        _back = null;
        Alignment = Align.Centre;
        FirstRow = 0;
        _entries.Clear();
        _selected = -1;
        _waiting = null;
        _repeat = 0f;
        _scrollRow = 0;
        _reveal = true;
        _dragStart = null;
    }

    public void AddItem(string text, Action press) => Add(new Entry(text, Kind.Item, press, null));

    /// <summary>An item that can't be chosen (shown greyed).</summary>
    public void AddDisabled(string text) => Add(new Entry(text, Kind.Item, null, null) { Disabled = true });

    /// <summary>A line at full size that can't be selected ("Really Quit?", list titles).</summary>
    public void AddTitle(string text) => Add(new Entry(text, Kind.Title, null, null) { Grow = 1f });

    /// <summary>An item that changes a setting by a step: a click or Right goes on, a right click or
    /// Left back. <paramref name="text"/> gives its label.</summary>
    public void AddOption(Func<string> text, Action<int> change)
    {
        Entry? entry = null;
        entry = new Entry(text(), Kind.Item, () => Step(1), Step);
        Add(entry);

        entry.Label = text;

        // Options may depend on each other (the resolution on the display mode): all are relabelled.
        void Step(int step)
        {
            change(step);
            _ui.ApplySettings();
            foreach (var option in _entries)
            {
                option.Text = option.Label?.Invoke() ?? option.Text;
            }

            _ui.Play(_change);
        }
    }

    /// <summary>Esc's page: the one above (the options' pages), or null when the screen handles Esc.</summary>
    private Action? _back;

    public bool HandlesBack => _back != null;

    private void Add(Entry entry)
    {
        _entries.Add(entry);
        if (_selected < 0 && entry.Selectable)
        {
            _selected = _entries.Count - 1;
        }
    }

    /// <summary>Selects the item at <paramref name="index"/> (the first selectable one after it).</summary>
    public void Select(int index)
    {
        for (var i = index; i < _entries.Count; i++)
        {
            if (_entries[i].Selectable)
            {
                _selected = i;
                _reveal = true;
                return;
            }
        }
    }

    /// <summary>A frame: keys, the mouse and the items' growth.</summary>
    public void Update(float delta)
    {
        ShowJob();
        Layout();
        if (Visible)
        {
            HandleInput(delta);
        }

        for (var i = 0; i < _entries.Count; i++)
        {
            var entry = _entries[i];
            var target = i == _selected || entry.Kind == Kind.Title ? 1f : Small;
            entry.Grow = MoveToward(entry.Grow, target, GrowRate * delta);
        }
    }

    private static float MoveToward(float value, float target, float step) =>
        value < target ? MathF.Min(value + step, target) : MathF.Max(value - step, target);

    private void HandleInput(float delta)
    {
        var input = _ui.Input;
        if (_waiting != null)
        {
            WaitForControl(input);
            return;
        }

        if (_back != null && input.WasPressed(MenuKey.Back))
        {
            Click();
            _back();
            return;
        }

        if (_touchPage != null)
        {
            _touchPage.Update();
            return;
        }

        if (input.WasPressed(MenuKey.Up))
        {
            Move(-1);
        }

        if (input.WasPressed(MenuKey.Down))
        {
            Move(1);
        }

        if (Scrolls && Drag(input))
        {
            return;
        }

        if (!Scrolls && (input.PointerMoved || input.WasClicked(Pointer.Left) || input.WasClicked(Pointer.Right)))
        {
            Hover(_ui.View.Pointer(input));
        }

        if (_selected < 0 || _selected >= _entries.Count)
        {
            return;
        }

        var selected = _entries[_selected];
        var pointed = !Scrolls && selected.Area.Contains(_ui.View.Pointer(input).X, _ui.View.Pointer(input).Y);
        if (input.WasPressed(MenuKey.Accept) || (input.WasClicked(Pointer.Left) && pointed))
        {
            Click();
            selected.Press?.Invoke();
            return;
        }

        if (selected.Change == null)
        {
            return;
        }

        if (input.WasPressed(MenuKey.Right))
        {
            selected.Change(1);
            _repeat = RepeatWait;
        }
        else if (input.WasPressed(MenuKey.Left) || (input.WasClicked(Pointer.Right) && pointed))
        {
            selected.Change(-1);
            _repeat = RepeatWait;
        }
        else
        {
            // Held, an option steps on (the stereo sliders: separations and distances).
            var held = input.IsDown(MenuKey.Right) ? 1 : input.IsDown(MenuKey.Left) ? -1 : 0;
            if (held == 0)
            {
                _repeat = 0f;
            }
            else
            {
                _repeat -= delta;
                if (_repeat <= 0f)
                {
                    selected.Change(held);
                    _repeat = RepeatRate;
                }
            }
        }
    }

    /// <summary>The next selectable item up or down.</summary>
    private void Move(int step)
    {
        for (var i = _selected + step; i >= 0 && i < _entries.Count; i += step)
        {
            if (!_entries[i].Selectable)
            {
                continue;
            }

            _selected = i;
            _reveal = true;
            Click();
            return;
        }
    }

    /// <summary>A scrolling page's pointer: a drag scrolls by whole rows, a tap (released where it
    /// didn't drag) selects and presses its item. Returns whether a tap pressed one.</summary>
    private bool Drag(Input input)
    {
        var point = _ui.View.Pointer(input);
        if (input.WasClicked(Pointer.Left))
        {
            _dragStart = point;
            _dragScroll = _scrollRow;
            _dragged = false;
            return false;
        }

        if (_dragStart is not { } start)
        {
            return false;
        }

        if (input.IsDown(Pointer.Left))
        {
            // Moved off by half a row any way, it's a drag (no press on release).
            var rows = (int)MathF.Round((start.Y - point.Y) / _row);
            _dragged |= Vector2.Distance(start, point) > _row / 2f;
            _scrollRow = ClampScroll(_dragScroll + rows);
            return false;
        }

        _dragStart = null;
        if (_dragged)
        {
            return false;
        }

        var index = _entries.FindIndex(e => e.Selectable && e.Area.Contains(point.X, point.Y));
        if (index < 0)
        {
            return false;
        }

        _selected = index;
        Click();
        _entries[index].Press?.Invoke();
        return true;
    }

    private int ClampScroll(int row) => Math.Clamp(row, 0, Math.Max(_entries.Count - _shownRows, 0));

    private void Hover(Vector2 point)
    {
        for (var i = 0; i < _entries.Count; i++)
        {
            if (!_entries[i].Selectable || !_entries[i].Area.Contains(point.X, point.Y) || i == _selected)
            {
                continue;
            }

            _selected = i;
            Click();
            return;
        }
    }

    private void Click() => _ui.Play(_click, Bus.Effects, _clickGain);

    /// <summary>Rows on the view (0x4265c0).</summary>
    private void Layout()
    {
        if (_entries.Count == 0)
        {
            return;
        }

        var enlargement = Enlargement();
        var scrollRow = _ui.Window.HasTouchScreen ? ScrollRow(_entries.Count, FirstRow, enlargement) : 0f;
        _originalRow = scrollRow > 0f ? Row : RowFor(_entries.Count);
        var row = _row = scrollRow > 0f ? scrollRow : RowFor(_entries.Count, FirstRow, enlargement);
        _shownRows = scrollRow > 0f ? ShownRows(row, FirstRow) : 0;
        Reveal();
        _textScale = row / _originalRow;
        var font = UsesBigFont(_originalRow) ? _fonts.Big : _fonts.Small;
        var width = _entries.Max(e => font.Width(Encoding.ASCII.GetBytes(e.Text))) * _textScale;

        // A scrolling page's big font may be wider than the view: its texts shrink to fit beside the bar.
        var widest = ScreenView.Size.X - ScrollBarWidth * 4f;
        if (Scrolls && width > widest)
        {
            _textScale *= widest / width;
            width = widest;
        }

        var x = Alignment == Align.Left ? 0f : (ScreenView.Size.X - width) / 2f;
        for (var i = 0; i < _entries.Count; i++)
        {
            // A scrolling page's rows off the list have no area (not drawn, not hit).
            var shown = i - _scrollRow;
            _entries[i].Area = Scrolls && (shown < 0 || shown >= _shownRows) ? RectangleF.Empty
                : new RectangleF(x, RowTop(row, (Scrolls ? shown : i) + FirstRow), width, row);
        }
    }

    /// <summary>The selection changed by keys or a new page: a scrolling list shows it.</summary>
    private void Reveal()
    {
        if (!Scrolls)
        {
            _scrollRow = 0;
            return;
        }

        _scrollRow = ClampScroll(_scrollRow);
        if (!_reveal || _selected < 0)
        {
            return;
        }

        _reveal = false;
        _scrollRow = ClampScroll(Math.Clamp(_scrollRow, _selected - _shownRows + 1, _selected));
    }

    /// <summary>A touch screen's row for a page whose fitted rows take the small font (the keys' pages):
    /// grown by <paramref name="enlargement"/> as the short pages', but so that at least
    /// <see cref="MinScrollRows"/> show; the rest scroll. 0: the page fits, it doesn't scroll.
    /// <code>
    ///   phone (× 1.4), 18 items: min(50.4, 350 / 8) = 43.75, 8 rows shown
    ///   tablet (× 1):  18 items: 36, 9 rows shown
    /// </code></summary>
    public static float ScrollRow(int entries, int firstRow, float enlargement)
    {
        if (UsesBigFont(RowFor(entries + firstRow)))
        {
            return 0f;
        }

        return MathF.Max(Row, MathF.Min(Row * enlargement, (ScreenView.Size.Y - Top * 2f) / (MinScrollRows + firstRow)));
    }

    /// <summary>The whole rows a scrolling page shows below <paramref name="firstRow"/>.</summary>
    public static int ShownRows(float row, int firstRow) => Math.Max((int)((ScreenView.Size.Y - Top * 2f) / row) - firstRow, 1);

    /// <summary>The height of a row for a page of <paramref name="entries"/> items: 36, less when they don't fit.</summary>
    public static float RowFor(int entries) => MathF.Min(Row, (ScreenView.Size.Y - Top * 2f) / Math.Max(entries, 1));

    /// <summary>The height of a row grown by <paramref name="enlargement"/> as far as the page's
    /// rows (from <paramref name="firstRow"/>) fit, never below <see cref="RowFor(int)"/>.
    /// <code>
    ///   phone, 6 items: min(36 × 1.4, 350 / 6) = 50.4
    /// </code></summary>
    public static float RowFor(int entries, int firstRow, float enlargement) =>
        MathF.Max(RowFor(entries), MathF.Min(Row * enlargement, (ScreenView.Size.Y - Top * 2f) / Math.Max(entries + firstRow, 1)));

    /// <summary>How much a touch screen's rows grow: to <see cref="TouchRowMm"/> from a row of
    /// <paramref name="rowMm"/>, at most <see cref="MaxEnlargement"/> (1: no touch screen, or an
    /// unknown display).
    /// <code>
    ///   phone  1080 px, 16.5 px/mm: a row of 108 px is 6.5 mm ─► × 1.4
    ///   tablet 1800 px, 12.6 px/mm: 173 px, 13.7 mm ─► × 1
    /// </code></summary>
    public static float Enlargement(float rowMm) =>
        rowMm > 0f ? Math.Clamp(TouchRowMm / rowMm, 1f, MaxEnlargement) : 1f;

    /// <summary>The growth of this window's rows (<see cref="Enlargement(float)"/>).</summary>
    private float Enlargement()
    {
        var window = _ui.Window;
        var unitsPerMm = window.UnitsPerMm;
        if (!window.HasTouchScreen || unitsPerMm <= 0f)
        {
            return 1f;
        }

        // A view unit in window units: the view's scale on the canvas, the canvas on the window.
        var unit = _ui.View.Scale * window.Size.Height / Renderer.CanvasHeight;
        return Enlargement(Row * unit / unitsPerMm);
    }

    /// <summary>Rows below 24 use the small font.</summary>
    public static bool UsesBigFont(float row) => row >= SmallFontRow;

    /// <summary>A row's top on the view.</summary>
    public static float RowTop(float row, int index) => Top + row * index;

    public void Draw()
    {
        if (Visible && _touchPage != null)
        {
            _touchPage.Draw();
            return;
        }

        if (!Visible || _entries.Count == 0)
        {
            return;
        }

        // A press may have just opened another page: its items have no rows yet.
        Layout();
        var font = UsesBigFont(_originalRow) ? _fonts.Big : _fonts.Small;
        var baseline = MathF.Round(_row * Baseline);
        foreach (var entry in _entries)
        {
            if (entry.Area.Height <= 0f)
            {
                continue;
            }

            var text = Encoding.ASCII.GetBytes(entry.Text);
            var grow = entry.Grow * _textScale;
            var x = entry.Area.X + (entry.Area.Width - font.Width(text) * grow) / 2f;
            var alpha = entry.Disabled ? DisabledAlpha : 1f;
            _ui.View.Text(font, text, MathF.Round(x), entry.Area.Y + baseline, grow, alpha);
        }

        DrawScrollBar();
    }

    /// <summary>A scrolling page's bar on the right: the list's height, the shown rows' part lit.</summary>
    private void DrawScrollBar()
    {
        if (!Scrolls)
        {
            return;
        }

        var top = RowTop(_row, FirstRow);
        var height = _row * _shownRows;
        var x = ScreenView.Size.X - ScrollBarWidth * 2f;
        _ui.View.Fill(new RectangleF(x, top, ScrollBarWidth, height), ScrollTrack);
        var share = height / _entries.Count;
        _ui.View.Fill(new RectangleF(x, top + share * _scrollRow, ScrollBarWidth, share * _shownRows), ScrollThumb);
    }

    // --- Options --------------------------------------------------------------------------------

    /// <summary>The options (see <see cref="Settings"/>), a page per kind; each change is applied
    /// and saved at once. Esc goes up a page.
    /// <code>
    ///   Options ─┬─ Display   mode, resolution, render scale, VSync, frame limit, GPU backend
    ///            ├─ Graphics  look, anti-aliasing, gore
    ///            ├─ Audio     volumes, music filter
    ///            ├─ Keyboard & mouse  mouse, key bindings
    ///            ├─ Gamepad   look, button bindings
    ///            ├─ Touch     touch controls, opacity, layout editor (a touch screen only)
    ///            ├─ Game      difficulty
    ///            └─ Mods      mods on/off, Make HD textures (desktop), Import from folder (Android)
    /// </code></summary>
    public void ShowOptions(Action back)
    {
        ClearPage();
        _back = back;
        var pages = new List<(string Name, Action<Action> Show)>
        {
            ("Display", ShowDisplay), ("Graphics", ShowGraphics), ("3D Stereo", ShowStereo), ("Audio", ShowAudio),
            ("Keyboard & mouse", ShowControls), ("Gamepad", ShowGamepad), ("Game", ShowGame), ("Mods", ShowMods),
        };
        if (HasTouchPage)
        {
            pages.Insert(pages.FindIndex(p => p.Show == ShowGamepad) + 1, ("Touch", ShowTouch));
        }

        for (var i = 0; i < pages.Count; i++)
        {
            var (name, show) = pages[i];
            var row = i;
            AddItem(name, () => show(() =>
            {
                ShowOptions(back);
                Select(row);
            }));
        }

        AddItem("Back", back);
        if (_songVoice == 0)
        {
            _songVoice = _ui.Play(_song, Bus.Music);
        }

        Print();
    }

    /// <summary>A sub-page of the options: Esc and its last item go back.</summary>
    private void EndPage(Action back)
    {
        _back = back;
        AddItem("Back", back);
        Print();
    }

    /// <summary>The page's items, for tests.</summary>
    private void Print() => Console.WriteLine($"Menu: {Describe()}");

    /// <summary>The window and the frame (<see cref="DisplayMenu"/>: a phone shows fewer).</summary>
    private void ShowDisplay(Action back)
    {
        ClearPage();
        var s = _ui.Settings;
        foreach (var item in DisplayMenu.Items(HdMenu.Current))
        {
            switch (item)
            {
                case DisplayItem.Mode:
                    AddOption(() => $"Display mode: {FullscreenNames[(int)s.Fullscreen]}",
                        step => s.Fullscreen = (Fullscreen)Wrap((int)s.Fullscreen + step, FullscreenNames.Length));
                    break;
                case DisplayItem.Resolution:
                    AddOption(ResolutionText, ResolutionStep);
                    break;
                case DisplayItem.Scale:
                    AddOption(() => s.RenderScale == Renderer.AutoScale ? "Render scale: Auto" : $"Render scale: {s.RenderScale}%",
                        step => s.RenderScale = Renderer.Scales[Wrap(Array.IndexOf(Renderer.Scales, s.RenderScale) + step, Renderer.Scales.Length)]);
                    break;
                case DisplayItem.VSync:
                    AddOption(VSyncText, step => s.VSync = (VSync)Wrap((int)s.VSync + step, VSyncCount));
                    break;
                case DisplayItem.FrameLimit:
                    AddOption(() => $"Frame limit: {(s.FrameLimit == FrameLimiter.Off ? "Off" : s.FrameLimit)}",
                        step => s.FrameLimit = FrameLimiter.Limits[Wrap(Array.IndexOf(FrameLimiter.Limits, s.FrameLimit) + step, FrameLimiter.Limits.Length)]);
                    break;
                case DisplayItem.Backend:
                    AddOption(BackendText, BackendStep);
                    break;
                case DisplayItem.Gamma:
                    AddOption(() => string.Create(CultureInfo.InvariantCulture, $"Gamma: {s.Gamma:0.0}"),
                        step => s.Gamma = GammaStep(s.Gamma, step));
                    break;
            }
        }

        EndPage(back);
    }

    private static readonly string[] FullscreenNames = ["Windowed", "Fullscreen", "Exclusive"];
    private static readonly int VSyncCount = Enum.GetValues<VSync>().Length;

    /// <summary>The window's size, the exclusive mode's, or the desktop's (borderless fullscreen).</summary>
    private string ResolutionText()
    {
        var s = _ui.Settings;
        var desktop = _ui.Window.Desktop.Size;
        return s.Fullscreen switch
        {
            Fullscreen.Off => $"Resolution: {s.WindowSize}",
            Fullscreen.Exclusive => $"Resolution: {s.ExclusiveSize ?? desktop}",
            _ => $"Resolution: {desktop} (desktop)",
        };
    }

    private void ResolutionStep(int step)
    {
        var s = _ui.Settings;
        var window = _ui.Window;
        if (s.Fullscreen == Fullscreen.Off)
        {
            s.WindowSize = Resolutions.Step(window.WindowedSizes(), s.WindowSize, step);
            return;
        }

        if (s.Fullscreen == Fullscreen.Exclusive)
        {
            var sizes = window.ExclusiveModes().Select(m => m.Size).ToList();
            s.ExclusiveSize = Resolutions.Step(sizes, s.ExclusiveSize ?? window.Desktop.Size, step);
        }
    }

    /// <summary>VSync as chosen, and the mode used when the display falls back to another.</summary>
    private string VSyncText()
    {
        var chosen = _ui.Settings.VSync;
        var used = _ui.Renderer.VSync;
        return used == chosen ? $"VSync: {chosen}" : $"VSync: {chosen} ({used})";
    }

    /// <summary>The backend chosen (Auto names SDL's choice); a change waits for the next start.</summary>
    private string BackendText()
    {
        var renderer = _ui.Renderer;
        var chosen = _ui.Settings.Backend;
        var name = chosen == GpuBackend.Auto ? $"Auto ({GpuBackends.Name(renderer.Gpu.Backend)})" : GpuBackends.Name(chosen);
        return chosen == renderer.Requested ? $"GPU: {name}" : $"GPU: {name} (restart)";
    }

    private void BackendStep(int step)
    {
        var s = _ui.Settings;
        var backends = Renderer.Backends();
        s.Backend = backends[Wrap(backends.ToList().IndexOf(s.Backend) + step, backends.Count)];
    }

    /// <summary>The levels' look, its anti-aliasing, and gore.</summary>
    private void ShowGraphics(Action back)
    {
        ClearPage();
        var s = _ui.Settings;
        AddOption(() => $"Graphics: {s.Graphics}", step => s.Graphics = (Graphics)Wrap((int)s.Graphics + step, GraphicsCount));
        AddOption(() => $"Anti-aliasing: {AntiAliasingNames[(int)s.AntiAliasing]}",
            step => s.AntiAliasing = (AntiAliasing)Wrap((int)s.AntiAliasing + step, AntiAliasingNames.Length));
        AddOption(() => $"Gore: {OnOff(s.Gore)}", _ => s.Gore = !s.Gore);
        EndPage(back);
    }

    private void ShowAudio(Action back)
    {
        ClearPage();
        var s = _ui.Settings;
        AddOption(() => $"Master volume: {s.MasterVolume}", step => s.MasterVolume = VolumeStep(s.MasterVolume, step));
        AddOption(() => $"Music volume: {s.MusicVolume}", step => s.MusicVolume = VolumeStep(s.MusicVolume, step));
        AddOption(() => $"Effects volume: {s.EffectsVolume}", step => s.EffectsVolume = VolumeStep(s.EffectsVolume, step));
        AddOption(() => $"Music filter: {OnOff(s.MusicFilter)}", _ => s.MusicFilter = !s.MusicFilter);
        EndPage(back);
    }

    private void ShowGame(Action back)
    {
        ClearPage();
        var s = _ui.Settings;
        AddOption(() => $"Difficulty: {DifficultyNames[(int)s.Difficulty]}", step => s.Difficulty = (Difficulty)Wrap((int)s.Difficulty + step, DifficultyNames.Length));

        // The console's fps overlay, for phones without a keyboard (this session only).
        var dev = _ui.Dev;
        AddOption(() => $"FPS overlay: {OnOff(dev.Overlay == Switch.On)}", _ => dev.ToggleOverlay());
        AddItem("Console", () => dev.ConsoleAsked = true);
        EndPage(back);
    }

    /// <summary>The 3D stereo page: the layout of the two eyes, their separation and the distance
    /// where their images converge (the same point on the screen in both; sliders: hold Left or
    /// Right to step again). <see cref="Stereo"/>.</summary>
    private void ShowStereo(Action back)
    {
        ClearPage();
        var s = _ui.Settings;
        AddOption(() => $"Mode: {StereoModes.Names[(int)s.Stereo]}",
            step => s.Stereo = (Stereo)Wrap((int)s.Stereo + step, StereoModes.Names.Length));
        AddOption(() => string.Create(CultureInfo.InvariantCulture, $"Separation: {s.StereoSeparation:0.00}"),
            step => s.StereoSeparation = Math.Clamp(s.StereoSeparation + step * SeparationStep, 0f, StereoModes.MaxSeparation));
        AddOption(() => $"Convergence: {ConvergenceText(s.StereoConvergence)}",
            step => s.StereoConvergence = ConvergenceStep(s.StereoConvergence, step));
        AddOption(() => $"Head tracking: {OnOff(s.HeadTracking == HeadTracking.On)}",
            _ => s.HeadTracking = s.HeadTracking == HeadTracking.On ? HeadTracking.Off : HeadTracking.On);
        EndPage(back);
    }

    /// <summary>A convergence change: each step scales the distance (they span an order of magnitude).</summary>
    private static float ConvergenceStep(float value, int step) =>
        Math.Clamp(step > 0 ? value * ConvergenceRate : value / ConvergenceRate, StereoModes.MinConvergence, StereoModes.MaxConvergence);

    /// <summary>The convergence distance ("far" at the most: parallel rays).</summary>
    private static string ConvergenceText(float value) => value >= StereoModes.MaxConvergence
        ? "far" : value.ToString("0.#", CultureInfo.InvariantCulture);

    /// <summary>The mods found (<see cref="ModCatalog"/>), each switched on or off; the enhanced look
    /// takes them from the next level (2D images at once). Then the HD textures' maker (desktop:
    /// Android never makes them) and, on Android, the import of the MDK folder again.</summary>
    private void ShowMods(Action back)
    {
        ClearPage();
        _ui.ScanMods();
        var s = _ui.Settings;
        AddTitle("Mods: enhanced look");
        foreach (var mod in _ui.Mods.Mods)
        {
            var name = mod.Name.Length > MaxModName ? mod.Name[..MaxModName] : mod.Name;
            AddOption(() => $"{name}: {OnOff(s.StateOf(mod.Folder) == ModState.On)}",
                _ => s.Mods[mod.Folder] = s.StateOf(mod.Folder) == ModState.On ? ModState.Off : ModState.On);
        }

        if (_ui.Mods.Mods.Count == 0)
        {
            AddDisabled("No mods in mods/");
        }

        if (HdMenu.Items(HdMenu.Current).Contains(HdItem.Make))
        {
            AddItem("Make HD textures", () => ShowHdTextures(() => ShowMods(back)));
        }

        // Android: pick the MDK folder again (new or changed files, HD textures made on a PC).
        if (_ui.Import is { } import)
        {
            AddItem("Import from folder", () =>
            {
                import();
                ShowMods(back);
            });
        }

        EndPage(back);
    }

    /// <summary>Makes the HD textures (<see cref="HdGenerator"/>, minutes on a GPU) while showing
    /// its progress; leaving the page cancels it.</summary>
    private void ShowHdTextures(Action back)
    {
        ClearPage();
        _back = back;
        AddTitle("HD textures");
        AddTitle("Real-ESRGAN, on the GPU");
        _jobLine = new Entry("", Kind.Title, null, null) { Grow = 1f };
        Add(_jobLine);
        _jobBack = new Entry("Cancel", Kind.Item, back, null);
        Add(_jobBack);
        _job = HdJob.Start(_ui.Data, _ui.UserFolder, HdOptions.Default with { Source = _ui.Settings.Upscaler });
        ShowJob();
    }

    /// <summary>The progress line, and "Back" once the job is over.</summary>
    private void ShowJob()
    {
        if (_job == null)
        {
            return;
        }

        _jobLine!.Text = _job.Progress.Describe();
        _jobBack!.Text = _job.Progress.Finished ? "Back" : "Cancel";
    }

    /// <summary>The mouse, and the key bindings: choosing a binding waits for a key or a mouse
    /// button (Esc cancels).</summary>
    public void ShowControls(Action back)
    {
        ClearPage();
        var s = _ui.Settings;
        AddOption(() => string.Create(CultureInfo.InvariantCulture, $"Mouse sensitivity: {s.MouseSensitivity:0.00}"),
            step => s.MouseSensitivity = SensitivityStep(s.MouseSensitivity, step));
        AddOption(() => $"Invert mouse: {OnOff(s.InvertMouse)}", _ => s.InvertMouse = !s.InvertMouse);
        foreach (var (key, name) in Settings.Actions)
        {
            Entry? entry = null;
            entry = new Entry(BindingText(key, name), Kind.Item, () =>
            {
                _waiting = key;
                _waitingDevice = Device.Keyboard;
                _waitingEntry = entry;
                _armed = false;
                entry!.Text = $"{name}: press a key...";
            }, null);
            Add(entry);
        }

        AddItem("Default keys", () =>
        {
            _ui.Settings.Bindings.Clear();
            _ui.ApplySettings();
            ShowControls(back);
        });
        EndPage(back);
    }

    /// <summary>The gamepad's look, and its button bindings: choosing a binding waits for a button
    /// (a key cancels: B and Start are buttons too). The sticks and Start (pause) are fixed.</summary>
    public void ShowGamepad(Action back)
    {
        ClearPage();
        var s = _ui.Settings;
        AddOption(() => string.Create(CultureInfo.InvariantCulture, $"Look sensitivity: {s.PadSensitivity:0.00}"),
            step => s.PadSensitivity = SensitivityStep(s.PadSensitivity, step));
        AddOption(() => $"Invert look: {OnOff(s.InvertPad)}", _ => s.InvertPad = !s.InvertPad);
        foreach (var (key, name) in Settings.PadActions)
        {
            Entry? entry = null;
            entry = new Entry(PadBindingText(key, name), Kind.Item, () =>
            {
                _waiting = key;
                _waitingDevice = Device.Gamepad;
                _waitingEntry = entry;
                _armed = false;
                entry!.Text = $"{name}: press a button...";
            }, null);
            Add(entry);
        }

        AddItem("Default buttons", () =>
        {
            _ui.Settings.PadBindings.Clear();
            _ui.ApplySettings();
            ShowGamepad(back);
        });
        EndPage(back);
    }

    /// <summary>The Touch page: a touch screen's, or the desktop's with --touch-layout (tests).</summary>
    private bool HasTouchPage => _ui.Window.HasTouchScreen || _ui.TouchEditor;

    /// <summary>The on-screen controls: when they show (<see cref="TouchButtons"/>, a touch screen
    /// only), their opacity, and the layout editor.</summary>
    public void ShowTouch(Action back)
    {
        ClearPage();
        var s = _ui.Settings;
        if (_ui.Window.HasTouchScreen)
        {
            AddOption(() => $"Touch controls: {s.Touch}",
                step => s.Touch = (TouchButtons)Wrap((int)s.Touch + step, Enum.GetValues<TouchButtons>().Length));
        }

        AddOption(() => $"Touch opacity: {s.TouchOpacity}%", step => s.TouchOpacity = OpacityStep(s.TouchOpacity, step));
        // Back on its row.
        var row = _entries.Count;
        AddItem("Edit touch layout", () => ShowTouchLayout(() =>
        {
            ShowTouch(back);
            Select(row);
        }));
        EndPage(back);
    }

    /// <summary>The touch layout editor (<see cref="TouchEditor"/>); Back cancels it.</summary>
    public void ShowTouchLayout(Action back)
    {
        ClearPage();
        _back = back;
        _touchView ??= new TouchView(_ui.Renderer, _fonts.Small);
        _touchPage = new TouchLayoutPage(_ui, _touchView, back);
    }

    /// <summary>The next opacity of the list (wrapping; from full when it isn't in it).</summary>
    public static int OpacityStep(int value, int step)
    {
        var index = Array.IndexOf(TouchOpacities, value);
        return TouchOpacities[Wrap((index >= 0 ? index : TouchOpacities.Length - 1) + step, TouchOpacities.Length)];
    }

    /// <summary>The key or button pressed after the item was chosen becomes the binding.</summary>
    private void WaitForControl(Input input)
    {
        if (!_armed)
        {
            _armed = true;
            return;
        }

        if (_waiting is not { } key)
        {
            return;
        }

        if (_waitingDevice == Device.Gamepad)
        {
            WaitForButton(input, key);
            return;
        }

        if (input.LastControl.Length == 0)
        {
            return;
        }

        if (!input.WasPressed(MenuKey.Back))
        {
            _ui.Settings.Bindings[key] = input.LastControl;
            _ui.ApplySettings();
        }

        _waitingEntry!.Text = BindingText(key, Settings.Actions.First(a => a.Key == key).Name);
        _waiting = null;
    }

    /// <summary>A gamepad button binds (not Start: it stays pause); a key or mouse button cancels.</summary>
    private void WaitForButton(Input input, Key key)
    {
        var button = input.LastPadControl;
        if (button.Length == 0 && input.LastControl.Length == 0)
        {
            return;
        }

        if (button.Length != 0 && input.BindPad(key, button))
        {
            _ui.Settings.PadBindings[key] = button;
            _ui.ApplySettings();
        }

        _waitingEntry!.Text = PadBindingText(key, Settings.PadActions.First(a => a.Key == key).Name);
        _waiting = null;
    }

    private string BindingText(Key key, string name) => $"{name}: {_ui.Input.Describe(key)}";

    private string PadBindingText(Key key, string name) => $"{name}: {_ui.Input.DescribePad(key)}";

    /// <summary>Volumes go by 10 and wrap (0-100).</summary>
    public static int VolumeStep(int value, int step) => Wrap(value + step * VolumeIncrement, Settings.MaxVolume + VolumeIncrement);

    /// <summary>The next sensitivity of the list (wrapping; from 1 when it isn't in it).</summary>
    public static float SensitivityStep(float value, int step)
    {
        var index = Array.IndexOf(Sensitivities, value);
        return Sensitivities[Wrap((index >= 0 ? index : DefaultSensitivity) + step, Sensitivities.Length)];
    }

    /// <summary>The next gamma of the list (wrapping; from 1 when it isn't in it).</summary>
    public static float GammaStep(float value, int step)
    {
        var index = Array.IndexOf(Gammas, value);
        return Gammas[Wrap((index >= 0 ? index : Array.IndexOf(Gammas, Renderer.DefaultGamma)) + step, Gammas.Length)];
    }

    /// <summary>The gammas by 0.1, <see cref="Renderer.MinGamma"/> to <see cref="Renderer.MaxGamma"/>.</summary>
    private static readonly float[] Gammas = [0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.6f, 1.7f, 1.8f, 1.9f, 2f];

    private static int Wrap(int value, int count) => ((value % count) + count) % count;

    private static string OnOff(bool value) => value ? "On" : "Off";
}
