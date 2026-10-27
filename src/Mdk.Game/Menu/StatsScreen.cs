using System.Drawing;
using System.Globalization;
using System.Numerics;
using System.Text;
using Mdk.Engine.Audio;
using Mdk.Engine.Platform;
using Mdk.Formats;
using Mdk.Game.Flow;

namespace Mdk.Game.Menu;

/// <summary>Which pages the statistics show: all of them after a level, or the briefing alone (a new
/// game, a save made before a level).</summary>
public enum StatsPages { All, Briefing }

/// <summary>The screens between levels (game state 6: 0x431b00, each frame 0x43200c; stats_screen.gd).
/// After a level: the intermission (<c>L1_INTRM</c>), the debriefing on the level's map, the
/// Score-O-matic, the save prompt, then the briefing of the next level on its map. Every page fades
/// in (from white for the intermission, else from black) over 0.5 s, waits for a key once it's fully
/// shown and fades out to black. Esc skips; holding Fire or Jump runs everything twice as fast
/// (typing 4x). All on the 600 x 360 screen.
/// <code>
///   intermission ─► debriefing ─► Score-O-matic ─► save prompt ─► briefing ─► fall ─► level
/// </code></summary>
public sealed class StatsScreen : IScreen
{
    public enum Phase { Score = 1, Intermission = 2, Briefing = 3, Debriefing = 4 }

    private const string Archive = "MISC/STATS.BNI";
    private const float FadeSpeed = 2f;
    private const float Fast = 2f;
    /// <summary>Typing (0x4335c0): characters per second, and when fast.</summary>
    private const float TypeRate = 15f;
    private const float TypeRateFast = 60f;
    private static readonly float[] DebriefY = [64f, 120f, 300f];
    private const float BriefY = 32f;
    /// <summary>Debriefing texts by the town flags' bits 31-29 (0x57440f).</summary>
    private static readonly string[] DebriefResults = ["S", "S", "F", "S", "SS", "SF", "FS", "FF"];
    private const int TownShift = 29;
    private const int TownMask = 7;
    /// <summary>Score-O-matic rows (tables 0x491b78 and 0x491bd8): label, start and end (x, y, bar width, scale).</summary>
    private static readonly (string Label, Vector4 From, Vector4 To)[] Rows =
    [
        ("ST_SHF", new(300, 85, 240, 256), new(180, 75, 120, 128)),
        ("ST_ACC", new(300, 155, 240, 256), new(420, 75, 120, 128)),
        ("ST_SNF", new(300, 155, 240, 256), new(180, 125, 120, 128)),
        ("ST_ACC", new(300, 225, 240, 256), new(420, 125, 120, 128)),
        ("ST_KILL", new(300, 225, 240, 256), new(300, 175, 120, 128)),
    ];
    private const int KillsRow = 4;
    private const float FullScale = 256f;
    private static readonly Vector2 HeadLabel = new(300f, 255f);
    private const float RowWait = 0.5f;
    private const int BarColour = 63;
    private const float BarHeight = 16f;
    private const float BarRise = 18f;
    private const float ValueGap = 8f;
    private const float ValueDrop = 14f;
    private const float TitleY = 28f;
    private const float NameY = 48f;
    private const float CountRate = 0.5f;
    private const float SlideSpeed = 2f;
    private const int RicochetChance = 8;
    private const int Ricochets = 3;
    private const int CursorBlink = 31;
    private const int CursorOn = 15;
    private const float TicksPerSecond = 30f;
    private const float Hidden = -1f;
    /// <summary>A row shows big (t = 0) until it slides.</summary>
    private const float Big = -0.5f;
    private const int PaletteSize = 768;
    private const int SystemColours = 192;
    private const int Players = 4;

    private readonly Ui _ui;
    private readonly GameState _state;
    private readonly SaveGames _saves;
    private readonly Bni _bni;
    private readonly Fonts _fonts;
    private readonly byte[] _systemRgb;
    private readonly Queue<Phase> _phases;
    private readonly Dictionary<string, Sound?> _sounds = [];
    private readonly int[] _voices = new int[Players];
    private readonly Random _random = new();
    private Phase _phase;
    private int _index;
    private PalettedImage? _image;
    private float _fadeIn;
    private float _fadeOut = -1f;
    private Vector4 _fadeColour;
    private bool _fast;
    private bool _skip;
    private bool _pressed;
    private float _ticks;
    private SavePrompt? _prompt;
    private int _loopVoice;
    private int _teletypeVoice;
    private Event _next = Event.None;

    // Typing: the texts of the page, their baselines, and the characters shown of the current one.
    private readonly List<byte[]> _texts = [];
    private readonly List<float> _textY = [];
    private int _current;
    private float _count;
    private int _shown;

    // Score-O-matic.
    private int _row;
    private float _rowTime;
    private int _rowStage;
    private readonly float[] _values = new float[Rows.Length];
    private readonly int[] _targets = new int[Rows.Length];
    private readonly int[] _fulls = new int[Rows.Length];
    private readonly float[] _slides = new float[Rows.Length];
    private int _heads;
    private float _headTime;
    private readonly HeadsView _headsView;

    public StatsScreen(Ui ui, GameState state, SaveGames saves, StatsPages pages, Phase? first)
    {
        _ui = ui;
        _state = state;
        _saves = saves;
        _bni = Bni.Load(ui.Data.PathOf(Archive));
        _systemRgb = new byte[PaletteSize];
        var system = ui.Fti.GetBytes("SYS_PAL");
        system.AsSpan(0, Math.Min(system.Length, PaletteSize)).CopyTo(_systemRgb);
        _fonts = new Fonts(ui.Renderer, ui.Fti, mods: ui.CanvasMods());
        _headsView = new HeadsView(ui.Renderer, _bni, PageRgb, ui.Data);
        foreach (var name in new[] { "CGUN", "SNIPER", "RICO1", "RICO2", "RICO3", "ALDIE", "XGHEAD1", "XGHEAD2", "TELETYPE" })
        {
            _sounds[name] = Ui.SoundOf(_bni, name, name == "CGUN" ? Looping.Forever : Looping.Once);
        }

        _index = Math.Max(GameState.IndexOf(state.Level), 0);
        _phases = pages == StatsPages.Briefing
            ? new Queue<Phase>([Phase.Briefing])
            : new Queue<Phase>([Phase.Intermission, Phase.Debriefing, Phase.Score, Phase.Briefing]);
        while (first is { } phase && _phases.Count > 1 && _phases.Peek() != phase)
        {
            _phases.Dequeue();
        }

        Start(_phases.Dequeue());
    }

    /// <summary>"statistics: Score", "statistics: Briefing", or the save prompt between them.</summary>
    public IReadOnlyList<string> Status => [_prompt != null ? "save prompt" : $"statistics: {_phase}"];

    public Typing Typing => _prompt?.Typing ?? Typing.Off;

    public Event Frame(float elapsed, string? screenshot)
    {
        var input = _ui.Input;
        _ui.View.Layout(ScreenView.Fit.Inside);
        if (_prompt != null)
        {
            _prompt.Update(input, elapsed);
            _prompt.Draw();
            _ui.Present(screenshot);
            if (_prompt.Closed)
            {
                _prompt = null;
                NextPhase();
            }

            return _next;
        }

        if (input.AnyPressed)
        {
            _skip |= input.WasPressed(MenuKey.Back);
            _pressed = true;
        }

        Update(elapsed, input);
        Draw();
        _ui.Present(screenshot);
        return _next;
    }

    private void Update(float delta, Input input)
    {
        _fast = _skip || input.IsDown(Key.Fire) || input.IsDown(Key.Jump);
        var speed = _fast ? Fast : 1f;
        _ticks += delta * TicksPerSecond;
        if (_fadeOut >= 0f)
        {
            _fadeOut += FadeSpeed * speed * delta;
            if (_fadeOut >= 1f)
            {
                EndPhase();
            }
        }
        else if (_fadeIn < 1f)
        {
            _fadeIn = MathF.Min(_fadeIn + FadeSpeed * speed * delta, 1f);
        }
        else if (UpdatePage(delta, speed) && (_pressed || _fast))
        {
            _fadeOut = 0f;
            _ui.Audio.Stop(_loopVoice);
        }

        _skip = false;
        _pressed = false;
    }

    private void Start(Phase phase)
    {
        Console.WriteLine($"Statistics: {phase}");
        _phase = phase;
        _fadeIn = 0f;
        _fadeOut = -1f;
        _fadeColour = phase == Phase.Intermission ? Ui.White : Ui.Black;
        _texts.Clear();
        _textY.Clear();
        _current = 0;
        _count = 1f;
        _shown = 0;
        _image = null;
        switch (phase)
        {
            case Phase.Intermission:
                _image = LoadImage("L1_INTRM");
                break;
            case Phase.Debriefing:
                _image = LoadImage($"L{_index + 1}_MAP");
                var result = DebriefResults[(_state.Stats.TownFlags >> TownShift) & TownMask];
                var name = $"DEB{_index + 1}{result}";
                if (!_ui.Fti.Has(name))
                {
                    name = $"DEB{_index + 1}S";
                }

                foreach (var entry in new[] { "DEBTOP", name, "DEBBOT" })
                {
                    _texts.Add(_ui.Fti.GetTextBytes(entry));
                }

                _textY.AddRange(DebriefY);
                break;
            case Phase.Score:
                StartScore();
                break;
            case Phase.Briefing:
                // On entry the inventory is emptied and the health raised to 100 (every level starts with both).
                _image = LoadImage($"L{_index + 1}_MAP");
                _texts.Add(_ui.Fti.GetTextBytes($"BRIEF{_index + 1}"));
                _textY.Add(BriefY);
                break;
        }
    }

    /// <summary>After the Score-O-matic the index moves on to the next level, which can be saved
    /// (named after its number); after the briefing the level starts.</summary>
    private void EndPhase()
    {
        if (_phase != Phase.Score)
        {
            NextPhase();
            return;
        }

        _index = Math.Min(_index + 1, GameState.Order.Length - 1);
        _state.Level = GameState.Order[_index];
        _prompt = new SavePrompt(_ui, _fonts, _saves, _state.Save(SaveKind.BeforeLevel), LevelFlow.SaveName(_state.Level));
        _phase = Phase.Briefing;
        _image = null;
    }

    /// <summary>The next page; after the last one (the briefing) the fall, then the level.</summary>
    private void NextPhase()
    {
        if (_phases.Count == 0)
        {
            _next = Event.Fall;
            return;
        }

        Start(_phases.Dequeue());
    }

    /// <summary>A page's palette: the system colours 0-63 and the image's 64-255.</summary>
    private byte[] PageRgb(byte[] rgb) => [.. _systemRgb[..SystemColours], .. rgb[SystemColours..PaletteSize]];

    /// <summary>An image of <c>STATS.BNI</c>: a 768-byte palette, <c>u16 width, height</c>, pixels.</summary>
    private PalettedImage? LoadImage(string name)
    {
        if (!_bni.Has(name))
        {
            return null;
        }

        var entry = _bni.Entries[name];
        var rgb = _bni.Bytes.AsSpan(entry.Offset, PaletteSize).ToArray();
        return PalettedImage.Of(_ui.Renderer, Texture.Parse(name, _bni.Bytes, entry.Offset + PaletteSize), PageRgb(rgb), _ui.CanvasMods());
    }

    /// <summary>Advances the page; true once everything is shown.</summary>
    private bool UpdatePage(float delta, float speed) => _phase switch
    {
        Phase.Debriefing or Phase.Briefing => UpdateTyping(delta),
        Phase.Score => UpdateScore(delta, speed),
        _ => true,
    };

    private void Play(string name)
    {
        if (_sounds.GetValueOrDefault(name) is not { } sound)
        {
            return;
        }

        var free = Array.FindIndex(_voices, v => !_ui.Audio.IsPlaying(v));
        var slot = free >= 0 ? free : 0;
        _ui.Audio.Stop(_voices[slot]);
        _voices[slot] = _ui.Play(sound);
    }

    // --- Typing ---------------------------------------------------------------------------------

    /// <summary>Types the page's texts one after the other; Esc finishes the current one (the briefing: all).</summary>
    private bool UpdateTyping(float delta)
    {
        if (_current >= _texts.Count)
        {
            return true;
        }

        var total = Typesetter.Layout(_texts[_current], 0f).Count;
        if (_skip)
        {
            if (_phase == Phase.Briefing)
            {
                _current = _texts.Count;
                return false;
            }

            _count = total;
        }
        else
        {
            _count += (_fast ? TypeRateFast : TypeRate) * delta;
        }

        var shown = Math.Min((int)MathF.Round(_count), total);
        if (shown != _shown)
        {
            _shown = shown;
            _ui.Audio.Stop(_teletypeVoice);
            _teletypeVoice = _ui.Play(_sounds.GetValueOrDefault("TELETYPE"));
        }

        if (_count < total)
        {
            return false;
        }

        _current++;
        _count = 0f;
        _shown = 0;
        return _current >= _texts.Count;
    }

    /// <summary>Draws a text with a budget of characters (-1: all), a cursor after the last one typed.</summary>
    private void DrawTyped(byte[] text, float y, int budget)
    {
        var (lines, count) = Typesetter.Layout(text, y);
        var remaining = budget < 0 ? count : budget;
        var font = _fonts.Big;
        for (var n = 0; n < lines.Count; n++)
        {
            var line = lines[n];
            if (line.Start > remaining)
            {
                break;
            }

            var part = line.Text[..Math.Min(line.Text.Length, remaining - line.Start)].ToList();

            // The cursor follows the last line being typed.
            var last = n + 1 == lines.Count || lines[n + 1].Start > remaining;
            if (last && remaining < count)
            {
                part.Add(((int)_ticks & CursorBlink) <= CursorOn ? (byte)'_' : (byte)' ');
            }

            // A centred line is placed by its whole width, so it doesn't move while typed.
            var left = line.Centred ? line.X - (font.Width(line.Text) >> 1) : line.X;
            _ui.View.Text(font, part.ToArray(), left, line.Y);
        }
    }

    // --- Score-O-matic --------------------------------------------------------------------------

    private void StartScore()
    {
        var s = _state.Stats;
        int[] targets = [s.Shots, Scripts.GameStats.Percent(s.ShotHits, s.Shots), s.SniperShots, Scripts.GameStats.Percent(s.SniperHits, s.SniperShots), s.Kills];
        int[] fulls = [s.Shots, 100, s.SniperShots, 100, s.Enemies];
        targets.CopyTo(_targets, 0);
        fulls.CopyTo(_fulls, 0);
        Array.Clear(_values);
        Array.Fill(_slides, Hidden);
        (_row, _rowStage, _rowTime, _heads, _headTime) = (0, 0, 0f, 0, 0f);
        Console.WriteLine($"Score-O-matic: {string.Join(", ", _targets.Select((t, i) => $"{t}/{_fulls[i]}"))}, heads {s.HeadShots}");
    }

    /// <summary>The rows one at a time (0x4328a4); then the heads.</summary>
    private bool UpdateScore(float delta, float speed)
    {
        for (var i = 0; i < _slides.Length; i++)
        {
            if (_slides[i] >= 0f && _slides[i] < 1f)
            {
                _slides[i] = _skip ? 1f : MathF.Min(_slides[i] + SlideSpeed * speed * delta, 1f);
            }
        }

        if (_row < Rows.Length)
        {
            UpdateRow(delta, speed);
            return false;
        }

        return UpdateHeads(delta, speed);
    }

    /// <summary>Wait, show big (<c>SNIPER</c>), wait, count up (about 2 s), slide to the final place.</summary>
    private void UpdateRow(float delta, float speed)
    {
        _rowTime += delta * speed;
        switch (_rowStage)
        {
            case 0 when _rowTime >= RowWait || _skip:
                (_rowStage, _rowTime) = (1, 0f);
                _slides[_row] = Big;
                Play("SNIPER");
                break;
            case 1 when _rowTime >= RowWait || _skip:
                _rowStage = 2;
                break;
            case 2:
                CountRow(delta, speed);
                break;
        }
    }

    private void CountRow(float delta, float speed)
    {
        var target = _targets[_row];
        if (_skip)
        {
            _values[_row] = target;
        }
        else
        {
            _values[_row] = MathF.Min(_values[_row] + MathF.Max(1f, MathF.Round(target * delta * CountRate)) * speed, target);
            if (_row == KillsRow && !_ui.Audio.IsPlaying(_voices[0]))
            {
                Play("ALDIE");
            }
            else if (_row != KillsRow && !_ui.Audio.IsPlaying(_loopVoice))
            {
                _loopVoice = _ui.Play(_sounds.GetValueOrDefault("CGUN"));
            }

            if (_row is 1 or 3 && _random.Next(RicochetChance) == 0)
            {
                Play($"RICO{_random.Next(Ricochets) + 1}");
            }
        }

        if (_values[_row] < target)
        {
            return;
        }

        _ui.Audio.Stop(_loopVoice);
        _slides[_row] = 0f;
        (_row, _rowStage, _rowTime) = (_row + 1, 0, 0f);
    }

    /// <summary>Head shots (0x433268): one a second (two when fast, all at once on Esc), spinning.
    /// Without gore the row is left out (0x43290b).</summary>
    private bool UpdateHeads(float delta, float speed)
    {
        _headsView.Update(_heads, delta);
        if (!_ui.Settings.Gore)
        {
            return true;
        }

        var count = _state.Stats.HeadShots;
        if (_row == Rows.Length)
        {
            _rowTime += delta * speed;
            if (_rowStage == 0 && (_rowTime >= RowWait || _skip))
            {
                Play("SNIPER");
                (_rowStage, _rowTime) = (1, 0f);
            }
            else if (_rowStage == 1 && (_rowTime >= RowWait || _skip))
            {
                _row++;
                _headTime = 1f;
            }

            return false;
        }

        if (_heads >= count)
        {
            return true;
        }

        if (_skip)
        {
            _heads = count;
            return true;
        }

        _headTime += delta * speed;
        if (_headTime < 1f)
        {
            return false;
        }

        _headTime = 0f;
        _heads++;
        Play($"XGHEAD{_random.Next(2) + 1}");
        return _heads >= count;
    }

    // --- Drawing --------------------------------------------------------------------------------

    private void Draw()
    {
        var view = _ui.View;
        _image?.Draw(view, Vector2.Zero);
        var shown = _fadeIn >= 1f;
        if (_phase is Phase.Debriefing or Phase.Briefing && shown)
        {
            for (var i = 0; i < Math.Min(_current + 1, _texts.Count); i++)
            {
                DrawTyped(_texts[i], _textY[i], i < _current ? -1 : (int)MathF.Round(_count));
            }
        }
        else if (_phase == Phase.Score)
        {
            DrawScore(shown);
        }

        // Fades: the page is blended with white or black.
        var fade = _fadeOut < 0f ? 1f - _fadeIn : _fadeOut;
        if (fade > 0f)
        {
            var colour = _fadeOut < 0f ? _fadeColour : Ui.Black;
            view.Fill(new RectangleF(0f, 0f, ScreenView.Size.X, ScreenView.Size.Y), colour with { W = Math.Min(fade, 1f) });
        }
    }

    private void DrawScore(bool shown)
    {
        var view = _ui.View;
        var (big, small) = (_fonts.Big, _fonts.Small);
        var title = _ui.Fti.GetTextBytes("ST_SCR");
        view.Text(big, title, 300 - (big.Width(title) >> 1), TitleY);
        var name = _ui.Fti.GetTextBytes("ST_DAMP");
        view.Text(small, name, 300 - (small.Width(name) >> 1), NameY);
        if (_ui.Settings.Gore)
        {
            _headsView.Draw(_heads, _state.Stats.HeadShots, view.ToCanvas(new RectangleF(0f, 0f, ScreenView.Size.X, ScreenView.Size.Y)));
        }

        if (!shown)
        {
            return;
        }

        var palette = Palette.FromRgb(_systemRgb).Rgba;
        var barColour = new Vector4(palette[BarColour * 4], palette[BarColour * 4 + 1], palette[BarColour * 4 + 2], byte.MaxValue) / byte.MaxValue;
        for (var i = 0; i < Rows.Length; i++)
        {
            if (_slides[i] == Hidden)
            {
                continue;
            }

            var t = Math.Clamp(_slides[i], 0f, 1f);
            var place = Vector4.Lerp(Rows[i].From, Rows[i].To, t);
            place = new Vector4(MathF.Round(place.X), MathF.Round(place.Y), MathF.Round(place.Z), MathF.Round(place.W));
            var scale = place.W / FullScale;
            var label = _ui.Fti.GetTextBytes(Rows[i].Label);
            view.Text(big, label, place.X - MathF.Round(big.Width(label) * scale * 0.5f), place.Y, scale);
            var bar = new Vector2(place.X - place.Z / 2f, MathF.Round(place.Y + BarRise * scale));
            var value = (int)_values[i];
            if (_fulls[i] > 0 && value > 0)
            {
                view.Fill(new RectangleF(bar.X, bar.Y, MathF.Floor(place.Z * value / _fulls[i]), BarHeight), barColour);
            }

            var text = Encoding.ASCII.GetBytes(ValueText(i, value, _fulls[i]));
            view.Text(small, text, bar.X - small.Width(text) - ValueGap, bar.Y + ValueDrop);
        }

        if (_row >= Rows.Length && _ui.Settings.Gore)
        {
            var label = _ui.Fti.GetTextBytes("ST_HEAD");
            view.Text(big, label, HeadLabel.X - (big.Width(label) >> 1), HeadLabel.Y);
        }
    }

    /// <summary>A row's value: <c>%d/%d</c> for kills, <c>%d%%</c> for accuracies, else <c>%d</c>.</summary>
    public static string ValueText(int row, int value, int full) => row switch
    {
        KillsRow => string.Create(CultureInfo.InvariantCulture, $"{value}/{full}"),
        1 or 3 => string.Create(CultureInfo.InvariantCulture, $"{value}%"),
        _ => value.ToString(CultureInfo.InvariantCulture),
    };

    public void Dispose() => _ui.Audio.StopAll();
}
