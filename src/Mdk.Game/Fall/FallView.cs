using System.Drawing;
using System.Numerics;
using Mdk.Engine.Render;
using Mdk.Formats;
using Mdk.Game.Mods;

namespace Mdk.Game.Fall;

/// <summary>Draws the fall (<see cref="FallSim"/>): a camera looking straight down with a focal
/// length of 250 pixels on the 360-high view (wider windows see more on the sides). The original's
/// 2D layers behind the models (the ground, the minecrawler, the haze; the intro's space, moon and
/// earth) are screen-aligned quads far below the camera, so the models stay in front of them.
/// <code>
///   camera ──►  models (Kurt, Bones, missiles, pickups, chutes, explosions), radar beam, trails
///      │
///      ▼ 6970  minecrawler             (fall)
///        6980  earth                   (intro)
///        6990  haze        │ moon      (fall: white streaks by the wind level │ intro)
///        7000  ground      │ space
/// </code>
/// The palette effects change the palette, as the original does (<see cref="FallPalette"/>); the
/// intro's whitening and the darkening go over everything as canvas fills.</summary>
public sealed class FallView
{
    private const float Focal = 250f;
    private const float ViewHeight = 360f;
    private const float ViewCentreY = ViewHeight / 2f;
    private const float Near = 0.5f;
    private const float Far = 8000f;
    private const float BackDepth = 7000f;
    private const float LayerStep = 10f;
    private const int BackQuads = 16;
    private const int QuadVertices = 6;
    /// <summary>Blend of each haze table towards white (0x490d1c, / 256), tables 1-24.</summary>
    private static readonly int[] HazeBlend = [0, 0, 0, 0, 0, 0, 0, 0, 3, 6, 12, 18, 24, 48, 72, 96, 120, 144, 168, 192, 215, 230, 245, 255];
    private const int HazeLevels = 8;
    private const float BlendUnit = 256f;
    // The radar: rings 1-4 of 6 points, translucent green by these / 256 (approximated colours).
    private static readonly int[] RadarAlpha = [48, 64, 80, 96, 128];
    private const int RadarRings = 4;
    private const int RadarSides = 6;
    private const float RadarBaseFactor = -4f;
    private const float RadarFirstAngle = 60f;
    private const int RadarVertices = 138;
    /// <summary>A trail point's width by its age in ticks (0x491d64): the ribbon swells just behind the
    /// missile, then tapers (<see cref="TrailWidth"/>).</summary>
    private static readonly float[] TrailSwell = [1f, 1.25f, 1.2f, 1.1f, 1.05f, 1f];
    /// <summary>The trail's colours (0x410874's translucent table): white at 215/255, and over the last
    /// segments before the missile less and less blue (yellow) and more opaque.</summary>
    private const byte TrailAlpha = 215;
    private const int TrailYellowSegments = 8;
    private const int TrailMissiles = 24;
    // The intro as f goes 0 to 1 (0x41106c): space (600 x 360) behind, the moon (128²) centred at
    // (300, 270 − 90 f) scaled (64 + 256 f) / 256, the earth (512²) centred at (300, 488 − 224 f)
    // scaled (300 + 128 f) / 256 by (100 + 42 f) / 256: a flattened disc rising from below.
    private static readonly Vector2 ViewSize = new(600f, ViewHeight);
    private const float SpriteUnit = 256f;
    private static readonly Vector2 MoonStart = new(300f, 270f);
    private const float MoonRise = 90f;
    private static readonly Vector2 MoonScale = new(64f, 256f);
    private static readonly Vector2 EarthStart = new(300f, 488f);
    private const float EarthRise = 224f;
    private static readonly Vector2 EarthScaleX = new(300f, 128f);
    private static readonly Vector2 EarthScaleY = new(100f, 42f);
    /// <summary>Kurt lies face down (+0x4c yaw 90°, +0x13c pitch −90°).</summary>
    private static readonly Matrix4x4 KurtTurn = Matrix4x4.CreateRotationY(MathF.PI / 2f) * Matrix4x4.CreateRotationZ(MathF.PI / 2f);
    private const float ChuteAbove = 2f;
    /// <summary>Models and trails nearer below the camera aren't drawn: a missile past Kurt (5 units
    /// above him, 5 below the camera) or a pickup dropped above him would fill the view.</summary>
    private const float NearClearance = 5f;
    /// <summary>An explosion grows to twice its size over its frames.</summary>
    private const float ExplosionGrowth = 2f;
    /// <summary>The explosion is pitched 90° (+0x13c), before its random yaw: the model is a fireball
    /// thin along x, so this turns its face to the camera above. Unpitched, it was seen edge-on, a
    /// sliver beside Kurt.</summary>
    private static readonly Matrix4x4 ExplosionTurn = Matrix4x4.CreateRotationY(-MathF.PI / 2f);
    private const float ExplosionMinScale = 0.01f;

    private readonly Renderer _renderer;
    private readonly int _index;
    private readonly FallPalette _effects;
    private readonly int _palette;
    private readonly int _spacePalette;
    private readonly int _whitePalette;
    private readonly FallGround _ground;
    private readonly int _groundTexture;
    private readonly List<(int Texture, Vector2 Size)> _crawler = [];
    private readonly (int Texture, Vector2 Size) _space;
    private readonly (int Texture, Vector2 Size) _moon;
    private readonly (int Texture, Vector2 Size) _earth;
    private readonly List<int> _haze = [];
    private readonly int _backMesh;
    private readonly int _radarMesh;
    private readonly int _trailMesh;
    /// <summary>The trail ribbon's ends in the missile's model space (0x43911c): its leftmost and
    /// rightmost vertices (by x), the tips of its tail fins.</summary>
    private readonly (Vector3 Left, Vector3 Right) _trailEnds;
    private readonly List<Vertex> _back = [];
    private readonly List<(int First, Material Material, int Frame)> _backDraws = [];

    public FallModels Models { get; }
    public FallHud Hud { get; }

    public FallView(Renderer renderer, Bni bni, TextureArchive mti, int index, Fti fti, ModImages? mods = null)
    {
        _renderer = renderer;
        _index = index;
        var n = index + 1;
        var palette = PaletteOf(bni, $"FALLP{n}");
        _effects = new FallPalette(palette.Rgba);
        _palette = renderer.CreatePalette(palette.Rgba);
        _spacePalette = renderer.CreatePalette(PaletteOf(bni, "SPACEPAL").Rgba);
        var white = new byte[Palette.Size * 4];
        Array.Fill(white, byte.MaxValue);
        _whitePalette = renderer.CreatePalette(white);
        Models = new FallModels(renderer, bni, mti, _effects, _palette);
        var missileVertices = Models.Find("MISSILE")?.PartList.SelectMany(p => p.Vertices).ToList() ?? [];
        _trailEnds = missileVertices.Count == 0
            ? (-Vector3.UnitX, Vector3.UnitX)
            : (missileVertices.MinBy(v => v.X), missileVertices.MaxBy(v => v.X));
        Hud = new FallHud(renderer, bni, _palette, fti, palette, mods);

        _ground = new FallGround(mti.Textures[$"LEVEL{n}"], mti.Textures[$"POD{n}"]);
        _groundTexture = renderer.CreateIndexTexture(FallGround.Size, FallGround.Size, _ground.Indices);
        for (var i = 1; i <= FallSim.CrawlerFrames; i++)
        {
            if (mti.Textures.TryGetValue($"L{n}_C{i:0000}", out var frame))
            {
                _crawler.Add(Image(frame));
            }
        }

        _space = Image(bni.GetImage("SPACE"));
        _moon = Image(bni.GetImage("MOON"));
        _earth = Image(bni.GetImage("EARTH"));
        for (var i = 0; i < FallSim.HazeFrames; i++)
        {
            _haze.Add(HazeTexture(Fall3d.Haze(bni, $"ZOOM{i:0000}")));
        }

        _backMesh = renderer.CreateDynamicMesh(BackQuads * QuadVertices);
        _radarMesh = renderer.CreateDynamicMesh(RadarVertices);
        _trailMesh = renderer.CreateDynamicMesh(TrailMissiles * FallMissile.TrailPoints * QuadVertices);
    }

    /// <summary>A palette entry (768 RGB bytes); colour 0 is forced to black.</summary>
    private static Palette PaletteOf(Bni bni, string name) =>
        Palette.FromRgb(bni.Bytes.AsSpan(bni.Entries[name].Offset, Palette.Size * 3));

    private (int, Vector2) Image(Texture texture) =>
        (_renderer.CreateIndexTexture(texture.Width, texture.Height, texture.Indices), new Vector2(texture.Width, texture.Height));

    /// <summary>A haze frame as 8 stacked masks, one per blend offset b (1-8): index 1 where the
    /// pixel's offset is b, so each level draws with its own alpha.</summary>
    private int HazeTexture(byte[] haze)
    {
        var size = Fall3d.HazeWidth * Fall3d.HazeRows;
        var masks = new byte[size * HazeLevels];
        for (var i = 0; i < size; i++)
        {
            var b = haze[i];
            if (b is > 0 and <= HazeLevels)
            {
                masks[(b - 1) * size + i] = 1;
            }
        }

        return _renderer.CreateIndexTexture(Fall3d.HazeWidth, Fall3d.HazeRows * HazeLevels, masks);
    }

    /// <summary>The colour behind the scene: palette colour 0 with the effects.</summary>
    public Vector4 Backdrop => _effects.Colour(0);

    /// <summary>Queues the frame's 3D scene and returns its camera.</summary>
    public View Draw(FallSim sim)
    {
        SetEffects(sim);
        var camera = sim.CameraPosition;
        var width = _renderer.CanvasWidth;
        _back.Clear();
        _backDraws.Clear();
        if (sim.InIntro)
        {
            QueueIntro(sim, camera, width);
        }
        else
        {
            QueueGround(sim, camera, width, ViewArea(width));
        }

        _renderer.UpdateMesh(_backMesh, System.Runtime.InteropServices.CollectionsMarshal.AsSpan(_back));
        foreach (var (first, material, frame) in _backDraws)
        {
            _renderer.Draw(_backMesh, first, QuadVertices, material, frame);
        }

        QueueModels(sim);
        QueueRadar(sim);
        QueueTrails(sim);

        var view = Matrix4x4.CreateLookAt(camera, camera - Vector3.UnitZ, Vector3.UnitY);
        var fieldOfView = FieldOfView();
        var projection = Matrix4x4.CreatePerspectiveFieldOfView(fieldOfView, _renderer.AspectRatio, Near, Far);
        return new View(view * projection, Matrix4x4.Identity, camera, new ViewCamera(-Vector3.UnitZ, Vector3.UnitY, projection, Near));
    }

    /// <summary>The vertical field of view: the original's 360 pixels high at 4:3 and wider, taller
    /// on a narrower screen to keep the width (<see cref="Kurt.CameraMath.KeepWidth"/>).</summary>
    private float FieldOfView()
    {
        var fieldOfView = 2f * MathF.Atan(ViewCentreY / Focal);
        var widened = Kurt.CameraMath.KeepWidth(float.RadiansToDegrees(fieldOfView), _renderer.AspectRatio);
        return widened == float.RadiansToDegrees(fieldOfView) ? fieldOfView : float.DegreesToRadians(widened);
    }

    /// <summary>The view pixels the camera sees, centred on (<paramref name="width"/> / 2, 180): a
    /// near-square foldable sees above and below the 360 rows too.</summary>
    private RectangleF ViewArea(float width)
    {
        var half = new Vector2(_renderer.AspectRatio, 1f) * MathF.Tan(FieldOfView() / 2f) * Focal;
        return new RectangleF(width / 2f - half.X, ViewCentreY - half.Y, 2f * half.X, 2f * half.Y);
    }

    /// <summary>The effects over everything: towards white in the intro, then darkened.</summary>
    public void DrawEffects(FallSim sim)
    {
        var canvas = new RectangleF(0f, 0f, _renderer.CanvasWidth, Renderer.CanvasHeight);
        if (sim.InIntro && sim.Whiten < 1f)
        {
            _renderer.FillRect(canvas, new Vector4(1f, 1f, 1f, 1f - sim.Whiten));
        }

        if (sim.Dark < 1f)
        {
            _renderer.FillRect(canvas, new Vector4(0f, 0f, 0f, 1f - sim.Dark));
        }
    }

    /// <summary>The fall's whitening and the red of death in the palette; colour 0 is whitened in
    /// the first second only.</summary>
    private void SetEffects(FallSim sim)
    {
        var brightness = sim.InIntro ? 1f : sim.Whiten;
        var zero = sim.Time < 1f ? FallPalette.Zero.Whitened : FallPalette.Zero.Kept;
        if (_effects.Set(brightness, sim.Red, zero))
        {
            _renderer.UpdateTexture(_palette, Palette.Size, 1, _effects.Rgba);
        }
    }

    /// <summary>Space as the background, the moon and the flattened earth rising and growing.</summary>
    private void QueueIntro(FallSim sim, Vector3 camera, float width)
    {
        var f = 1f - sim.IntroLeft / (float)FallSim.IntroTicks;
        var origin = new Vector2((width - ViewSize.X) / 2f, 0f);
        Quad(camera, width, new RectangleF(origin.X, origin.Y, ViewSize.X, ViewSize.Y), BackDepth, Plain(_space.Texture, _spacePalette));
        float Grow(Vector2 scale) => (scale.X + scale.Y * f) / SpriteUnit;
        var moon = origin + MoonStart - new Vector2(0f, MoonRise * f);
        Sprite(camera, width, _moon, moon, new Vector2(Grow(MoonScale)), BackDepth - LayerStep, _spacePalette);
        var earth = origin + EarthStart - new Vector2(0f, EarthRise * f);
        Sprite(camera, width, _earth, earth, new Vector2(Grow(EarthScaleX), Grow(EarthScaleY)), BackDepth - 2f * LayerStep, _spacePalette);
    }

    /// <summary>The ground with the track, the haze over it, the minecrawler over both (fall.gd).</summary>
    private void QueueGround(FallSim sim, Vector3 camera, float width, RectangleF area)
    {
        var centreRow = FallGround.CentreRow(sim.Time);
        if (_ground.UpdateTrack(centreRow))
        {
            _renderer.UpdateTexture(_groundTexture, FallGround.Size, FallGround.Size, _ground.Indices);
        }

        var (screen, texels) = FallGround.Visible(camera, centreRow, area);
        if (screen.Width > 0f)
        {
            var uv = new RectangleF(texels.X / FallGround.Size, texels.Y / FallGround.Size, texels.Width / FallGround.Size, texels.Height / FallGround.Size);
            Quad(camera, width, screen, BackDepth, Plain(_groundTexture, _palette), uv);
        }

        if (_crawler.Count != 0)
        {
            var (centre, scale) = FallGround.Crawler(camera, width);
            Sprite(camera, width, _crawler[(int)sim.CrawlerFrame % _crawler.Count], centre, new Vector2(scale), BackDepth - 3f * LayerStep, _palette);
        }

        // Each blend offset b uses table (wind level + b): white by its alpha.
        var level = sim.Wind >> FallSim.WindShift;
        for (var b = 1; b <= HazeLevels; b++)
        {
            var alpha = HazeBlend[Math.Clamp(level + b, 1, HazeBlend.Length) - 1] / BlendUnit;
            if (alpha <= 0f)
            {
                continue;
            }

            var material = new Material(_haze[sim.HazeFrame], _whitePalette, new Vector4(1f, 1f, 1f, alpha), HazeLevels, Pass.Blended);
            Quad(camera, width, area, BackDepth - LayerStep, material, frame: b - 1);
        }
    }

    private static Material Plain(int texture, int palette) => new(texture, palette, Vector4.One, 1, Pass.DoubleSided);

    /// <summary>An image centred on <paramref name="centre"/> (view pixels), scaled.</summary>
    private void Sprite(Vector3 camera, float width, (int Texture, Vector2 Size) image, Vector2 centre, Vector2 scale, float depth, int palette)
    {
        var size = image.Size * scale;
        Quad(camera, width, new RectangleF(centre.X - size.X / 2f, centre.Y - size.Y / 2f, size.X, size.Y), depth, Plain(image.Texture, palette));
    }

    /// <summary>A quad covering <paramref name="screen"/> (view pixels) at <paramref name="depth"/>
    /// below the camera.</summary>
    private void Quad(Vector3 camera, float width, RectangleF screen, float depth, Material material, RectangleF? uv = null, int frame = 0)
    {
        if (_back.Count + QuadVertices > BackQuads * QuadVertices)
        {
            return;
        }

        var t = uv ?? new RectangleF(0f, 0f, 1f, 1f);
        Vertex Corner(float x, float y, float u, float v) => new(
            new Vector3(camera.X + (x - width / 2f) * depth / Focal, camera.Y - (y - ViewCentreY) * depth / Focal, camera.Z - depth),
            new Vector2(u, v));
        var first = _back.Count;
        _back.Add(Corner(screen.Left, screen.Top, t.Left, t.Top));
        _back.Add(Corner(screen.Right, screen.Top, t.Right, t.Top));
        _back.Add(Corner(screen.Right, screen.Bottom, t.Right, t.Bottom));
        _back.Add(Corner(screen.Left, screen.Top, t.Left, t.Top));
        _back.Add(Corner(screen.Right, screen.Bottom, t.Right, t.Bottom));
        _back.Add(Corner(screen.Left, screen.Bottom, t.Left, t.Bottom));
        _backDraws.Add((first, material, frame));
    }

    private void QueueModels(FallSim sim)
    {
        if (sim.KurtVisible)
        {
            var animation = sim.KurtPose == FallSim.KurtAnimation.Hit ? "KURT_HIT" : "KURTANIM";
            Models.Draw("KURT", KurtTurn * Matrix4x4.CreateTranslation(sim.KurtPosition), animation, (int)sim.KurtFrame);
        }

        if (sim.BonesPosition is { } bones)
        {
            Models.Draw("BONES", KurtTurn * Matrix4x4.CreateTranslation(bones), "BONESANM", (int)sim.BonesFrame);
        }

        var camera = sim.CameraPosition;
        foreach (var missile in sim.Missiles)
        {
            if (NearCamera(missile.Position, camera))
            {
                continue;
            }

            Models.Draw("MISSILE", Orient(missile.Velocity) * Matrix4x4.CreateTranslation(missile.Position));
        }

        foreach (var pickup in sim.Pickups)
        {
            if (NearCamera(pickup.Position, camera))
            {
                continue;
            }

            var turn = Matrix4x4.CreateRotationZ(float.DegreesToRadians(pickup.Yaw));
            Models.Draw(pickup.Name, turn * Matrix4x4.CreateTranslation(pickup.Position));
            var chute = pickup.Position + new Vector3(0f, 0f, ChuteAbove);
            if (pickup.Chute && !NearCamera(chute, camera))
            {
                Models.Draw("CHUTE", turn * Matrix4x4.CreateTranslation(chute));
            }
        }

        // Explosions grow and play their texture's frames, over everything (sorted at camera z + 5).
        foreach (var explosion in sim.Explosions)
        {
            var frame = (int)explosion.Ticks;
            var scale = MathF.Max(ExplosionGrowth * frame / FallSim.ExplosionTicks, ExplosionMinScale);
            var world = Matrix4x4.CreateScale(scale) * ExplosionTurn * Matrix4x4.CreateRotationZ(explosion.Yaw) * Matrix4x4.CreateTranslation(explosion.Position);
            Models.Draw("EXPLODE", world, textureFrame: frame, pass: Pass.OnTop);
        }
    }

    /// <summary>Whether a model or trail point is too near the camera to draw.</summary>
    public static bool NearCamera(Vector3 position, Vector3 camera) => position.Z > camera.Z - NearClearance;

    /// <summary>Points the model's +y along a direction, z up (0x4123e8).</summary>
    private static Matrix4x4 Orient(Vector3 direction)
    {
        var y = Vector3.Normalize(direction);
        var x = Vector3.Cross(y, Vector3.UnitZ);
        x = x.LengthSquared() < 1e-12f ? Vector3.UnitX : Vector3.Normalize(x);
        var z = Vector3.Cross(x, y);
        return new Matrix4x4(x.X, x.Y, x.Z, 0f, y.X, y.Y, y.Z, 0f, z.X, z.Y, z.Z, 0f, 0f, 0f, 0f, 1f);
    }

    /// <summary>The radar's beam (0x412f94): a fan from its base to ring 1, rings 1-3 at
    /// <c>1 − 2^−k</c> of the way to the spot with radius <c>r (1 − 2^−k)</c>, ring 4 on the spot, and a cap on ring 4.
    /// <code>
    ///   base ◄── fan ── ring 1 ══ ring 2 ══ ring 3 ══ ring 4 (cap) ── spot
    /// </code></summary>
    private void QueueRadar(FallSim sim)
    {
        if (!sim.RadarActive)
        {
            return;
        }

        var camera = sim.CameraPosition;
        var spot = sim.RadarSpot;
        var basePoint = new Vector3(RadarBaseFactor * camera.X, RadarBaseFactor * camera.Y, 0f);
        var rings = new Vector3[RadarRings][];
        for (var k = 0; k < RadarRings; k++)
        {
            var (centre, radius) = RadarRing(basePoint, spot, sim.RadarSpotRadius, k);
            rings[k] = new Vector3[RadarSides];
            for (var m = 0; m < RadarSides; m++)
            {
                var angle = float.DegreesToRadians(RadarFirstAngle * (m + 1));
                rings[k][m] = centre + new Vector3(MathF.Cos(angle), MathF.Sin(angle), 0f) * radius;
            }
        }

        var vertices = new List<Vertex>(RadarVertices);
        void Add(params Vector3[] points) => vertices.AddRange(points.Select(p => new Vertex(p, Vector2.Zero)));
        var bands = new List<int> { 0 };
        for (var m = 0; m < RadarSides; m++)
        {
            Add(basePoint, rings[0][m], rings[0][(m + 1) % RadarSides]);
        }

        for (var k = 0; k < RadarRings - 1; k++)
        {
            bands.Add(vertices.Count);
            for (var m = 0; m < RadarSides; m++)
            {
                var (a, b) = (rings[k][m], rings[k][(m + 1) % RadarSides]);
                var (c, d) = (rings[k + 1][m], rings[k + 1][(m + 1) % RadarSides]);
                Add(a, b, c, b, d, c);
            }
        }

        bands.Add(vertices.Count);
        for (var m = 1; m < RadarSides - 1; m++)
        {
            Add(rings[^1][0], rings[^1][m], rings[^1][m + 1]);
        }

        bands.Add(vertices.Count);
        _renderer.UpdateMesh(_radarMesh, System.Runtime.InteropServices.CollectionsMarshal.AsSpan(vertices));
        for (var c = 0; c < RadarAlpha.Length; c++)
        {
            var colour = Material.Flat(_effects.Apply(new Vector4(0f, 1f, 0f, RadarAlpha[c] / BlendUnit)), Pass.Blended);
            _renderer.Draw(_radarMesh, bands[c], bands[c + 1] - bands[c], colour);
        }
    }

    /// <summary>The centre and radius of the beam's ring <paramref name="ring"/> (0-3): rings 1-3
    /// halve the way left to the spot, ring 4 is the spot (0x412af8).</summary>
    public static (Vector3 Centre, float Radius) RadarRing(Vector3 basePoint, Vector3 spot, float radius, int ring)
    {
        var f = ring == RadarRings - 1 ? 1f : 1f - MathF.Pow(2f, -(ring + 1));
        return (Vector3.Lerp(basePoint, spot, f), radius * f);
    }

    /// <summary>The smoke trails (0x439454; set up by 0x43911c, sampled by 0x4392a4): a ribbon along the
    /// last 32 points of each missile (oldest first), between its tail fins' tips as the missile was
    /// turned at each point, widened by <see cref="TrailWidth"/>; each segment in one colour
    /// (<see cref="TrailColour"/>).
    /// <code>
    ///   tail ─────── white (215/255) ───────╴╴▶ last 8 segments: yellower, more opaque ─▶ missile
    ///   width ×0 ◄──── tapering ────── ×1, ×1.05, ×1.1, ×1.2, ×1.25 (swell), ×1 at the missile
    /// </code></summary>
    private void QueueTrails(FallSim sim)
    {
        var vertices = new List<Vertex>();
        var camera = sim.CameraPosition;
        var middle = (_trailEnds.Left + _trailEnds.Right) / 2f;
        Span<Vector3> lefts = stackalloc Vector3[FallMissile.TrailPoints];
        Span<Vector3> rights = stackalloc Vector3[FallMissile.TrailPoints];
        foreach (var missile in sim.Missiles.Take(TrailMissiles))
        {
            var trail = missile.Trail;
            var count = Math.Min(trail.Count, FallMissile.TrailPoints);
            for (var i = 0; i < count; i++)
            {
                // The fins' tips around their middle, as turned then, scaled by the point's age.
                var turn = Orient(missile.TrailHeadings[i]);
                var centre = trail[i] + Vector3.TransformNormal(middle, turn);
                var width = TrailWidth(count - 1 - i);
                lefts[i] = centre + Vector3.TransformNormal(_trailEnds.Left - middle, turn) * width;
                rights[i] = centre + Vector3.TransformNormal(_trailEnds.Right - middle, turn) * width;
            }

            for (var k = 1; k < count; k++)
            {
                if (NearCamera(trail[k - 1], camera) || NearCamera(trail[k], camera))
                {
                    continue;
                }

                var colour = TrailColour(k, count);
                Vector3[] quad = [lefts[k - 1], rights[k - 1], rights[k], lefts[k]];
                foreach (var q in (int[])[0, 1, 2, 0, 2, 3])
                {
                    vertices.Add(new Vertex(quad[q], Vector2.Zero, colour));
                }
            }
        }

        if (vertices.Count == 0)
        {
            return;
        }

        _renderer.UpdateMesh(_trailMesh, System.Runtime.InteropServices.CollectionsMarshal.AsSpan(vertices));
        _renderer.Draw(_trailMesh, 0, vertices.Count, Material.Flat(_effects.Apply(Vector4.One), Pass.Blended));
    }

    /// <summary>A trail point's width factor by its age in ticks: the swell's (0-5), then down to 0 at
    /// the 32nd.</summary>
    public static float TrailWidth(int age)
    {
        if (age < TrailSwell.Length)
        {
            return TrailSwell[age];
        }

        var taper = FallMissile.TrailPoints - TrailSwell.Length;
        return 1f - (float)(age - TrailSwell.Length) / taper;
    }

    /// <summary>Segment <paramref name="k"/> (1 to count − 1, the last one at the missile) of a trail of
    /// <paramref name="count"/> points: white at 215/255, the last 8 with blue 255 down to 31 and alpha
    /// 215 up to 250 (8 steps).</summary>
    public static uint TrailColour(int k, int count)
    {
        var step = k - (count - TrailYellowSegments);
        if (step < 0)
        {
            return Vertex.Rgba(byte.MaxValue, byte.MaxValue, byte.MaxValue, TrailAlpha);
        }

        var blue = (2040 - byte.MaxValue * step) >> 3;
        var alpha = ((TrailYellowSegments - step) * TrailAlpha + byte.MaxValue * step) >> 3;
        return Vertex.Rgba(byte.MaxValue, byte.MaxValue, (byte)blue, (byte)alpha);
    }
}
