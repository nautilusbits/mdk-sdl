using Android.App;
using Android.Graphics;
using Android.Views;

namespace Mdk.Android;

/// <summary>The setup screen's backdrop (<see cref="SetupActivity"/>) behind its dialogs: a night
/// sky in the icon's teal, the icon and the name on the left, the dialogs on the right
/// (<see cref="Place"/>). The project's own art only, no game files.
/// <code>
///   ┌──────────────────────────────────────────┐
///   │  .     *    .  ┌──────────────────────┐  │
///   │   ╭──────╮     │ Title                │  │
///   │   │ icon │     │ message              │  │
///   │   ╰──────╯     │      Secondary [Main]│  │
///   │     MDK    *   └──────────────────────┘  │
///   └──────────────────────────────────────────┘
/// </code></summary>
internal sealed class Backdrop : View
{
    /// <summary>Finds the backdrop in the activity ("MDK").</summary>
    private const int ViewId = 0x4d444b;
    private const int StarCount = 140;
    /// <summary>The same sky on every start.</summary>
    private const int StarSeed = 1997;
    /// <summary>Star radii (dp) and alphas (0-255): a range from a minimum.</summary>
    private const float StarMinRadius = 0.6f;
    private const float StarRadiusRange = 2.2f;
    private const int StarMinAlpha = 40;
    private const int StarAlphaRange = 180;
    /// <summary>The icon's centre (fractions of the view) and side (of the height).</summary>
    private const float IconX = 0.19f;
    private const float IconY = 0.42f;
    private const float IconSide = 0.38f;
    /// <summary>The name's size (of the height) and spacing (ems).</summary>
    private const float NameSize = 0.085f;
    private const float NameSpacing = 0.3f;
    /// <summary>The name's baseline below the icon, in its sizes.</summary>
    private const float NameGap = 1.4f;
    private const string Name = "MDK";
    /// <summary>The glow around the icon, its radius of the height.</summary>
    private const float GlowRadius = 0.6f;
    /// <summary>The dialogs' width and right margin, fractions of the screen's width.</summary>
    private const float DialogWidth = 0.6f;
    private const float DialogMargin = 0.025f;

    private static readonly Color SkyTop = Color.ParseColor("#0E2A31");
    private static readonly Color SkyBottom = Color.ParseColor("#020608");
    private static readonly Color Glow = Color.ParseColor("#38F39A2B");
    private static readonly Color Gold = Color.ParseColor("#FFC861");

    private readonly Paint _sky = new();
    private readonly Paint _glow = new();
    private readonly Paint _star = new(PaintFlags.AntiAlias);
    private readonly Paint _icon = new(PaintFlags.FilterBitmap | PaintFlags.AntiAlias);
    private readonly Paint _name = new(PaintFlags.AntiAlias);
    private readonly Bitmap? _bitmap;
    /// <summary>Stars: x, y (fractions), radius (dp), alpha.</summary>
    private readonly (float X, float Y, float Radius, int Alpha)[] _stars;

    private Backdrop(Activity activity) : base(activity)
    {
        Id = ViewId;
        _bitmap = BitmapFactory.DecodeResource(activity.Resources, Resource.Drawable.icon);
        _name.Color = Gold;
        _name.TextAlign = Paint.Align.Center;
        _name.FakeBoldText = true;
        _name.LetterSpacing = NameSpacing;
        _star.Color = Color.White;

        var random = new Random(StarSeed);
        _stars = Enumerable.Range(0, StarCount)
            .Select(_ => (random.NextSingle(), random.NextSingle(), StarMinRadius + random.NextSingle() * StarRadiusRange, StarMinAlpha + random.Next(StarAlphaRange)))
            .ToArray();
    }

    /// <summary>Shows the backdrop over SDL's surface (from any thread).</summary>
    public static void Show(Activity activity)
    {
        var full = ViewGroup.LayoutParams.MatchParent;
        activity.RunOnUiThread(() => activity.AddContentView(new Backdrop(activity), new ViewGroup.LayoutParams(full, full)));
    }

    /// <summary>A shown dialog moves right of the icon, undimmed, while the backdrop is up. UI thread.</summary>
    public static void Place(Activity activity, Dialog dialog)
    {
        if (activity.FindViewById(ViewId) is not Backdrop || dialog.Window is not { } window)
        {
            return;
        }

        // The window's width, else the screen's long side (the activity is landscape): before the
        // first layout the window is 0 wide (an invisible dialog), and before the turn to
        // landscape the screen's width is the portrait one.
        var width = activity.Window!.DecorView.Width;
        if (width == 0)
        {
            var screen = activity.Resources!.DisplayMetrics!;
            width = Math.Max(screen.WidthPixels, screen.HeightPixels);
        }

        window.ClearFlags(WindowManagerFlags.DimBehind);
        window.SetGravity(GravityFlags.End | GravityFlags.CenterVertical);
        var attributes = window.Attributes!;
        attributes.X = (int)(width * DialogMargin);
        window.Attributes = attributes;
        window.SetLayout((int)(width * DialogWidth), ViewGroup.LayoutParams.WrapContent);
    }

    /// <summary>Hidden for good: the icon's pixels go before the game's.</summary>
    protected override void OnDetachedFromWindow()
    {
        base.OnDetachedFromWindow();
        _bitmap?.Recycle();
    }

    /// <summary>The gradients follow the size.</summary>
    protected override void OnSizeChanged(int w, int h, int oldw, int oldh)
    {
        base.OnSizeChanged(w, h, oldw, oldh);
        _sky.SetShader(new LinearGradient(0, 0, 0, h, SkyTop, SkyBottom, Shader.TileMode.Clamp!));
        _glow.SetShader(new RadialGradient(w * IconX, h * IconY, h * GlowRadius, Glow, Color.Transparent, Shader.TileMode.Clamp!));
        _name.TextSize = h * NameSize;
    }

    protected override void OnDraw(Canvas canvas)
    {
        float w = Width;
        float h = Height;
        canvas.DrawRect(0, 0, w, h, _sky);
        canvas.DrawRect(0, 0, w, h, _glow);

        // A sparse sky, smaller stars fainter.
        var density = Resources!.DisplayMetrics!.Density;
        foreach (var (x, y, radius, alpha) in _stars)
        {
            _star.Alpha = alpha;
            canvas.DrawCircle(x * w, y * h, radius * density, _star);
        }

        // The icon, the name under it.
        var half = h * IconSide / 2;
        var cx = w * IconX;
        var cy = h * IconY;
        if (_bitmap != null)
        {
            canvas.DrawBitmap(_bitmap, null, new RectF(cx - half, cy - half, cx + half, cy + half), _icon);
        }

        canvas.DrawText(Name, cx, cy + half + _name.TextSize * NameGap, _name);
    }
}
