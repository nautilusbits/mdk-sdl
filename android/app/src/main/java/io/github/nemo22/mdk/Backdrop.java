package io.github.nemo22.mdk;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;

import io.github.nemo22.mdk.game.DotNetRandom;

/**
 * The setup screen's backdrop ({@link SetupActivity}) behind its dialogs: a night sky in the
 * icon's teal, the icon and the name on the left, the dialogs on the right ({@link #place}).
 * The project's own art only, no game files.
 * <pre>
 *   ┌──────────────────────────────────────────┐
 *   │  .     *    .  ┌──────────────────────┐  │
 *   │   ╭──────╮     │ Title                │  │
 *   │   │ icon │     │ message              │  │
 *   │   ╰──────╯     │      Secondary [Main]│  │
 *   │     MDK    *   └──────────────────────┘  │
 *   └──────────────────────────────────────────┘
 * </pre>
 */
final class Backdrop extends View {
    /** Finds the backdrop in the activity ("MDK"). */
    private static final int VIEW_ID = 0x4d444b;
    private static final int STAR_COUNT = 140;
    /** The same sky on every start. */
    private static final int STAR_SEED = 1997;
    /** Star radii (dp) and alphas (0-255): a range from a minimum. */
    private static final float STAR_MIN_RADIUS = 0.6f;
    private static final float STAR_RADIUS_RANGE = 2.2f;
    private static final int STAR_MIN_ALPHA = 40;
    private static final int STAR_ALPHA_RANGE = 180;
    /** The icon's centre (fractions of the view) and side (of the height). */
    private static final float ICON_X = 0.19f;
    private static final float ICON_Y = 0.42f;
    private static final float ICON_SIDE = 0.38f;
    /** The name's size (of the height) and spacing (ems). */
    private static final float NAME_SIZE = 0.085f;
    private static final float NAME_SPACING = 0.3f;
    /** The name's baseline below the icon, in its sizes. */
    private static final float NAME_GAP = 1.4f;
    private static final String NAME = "MDK";
    /** The glow around the icon, its radius of the height. */
    private static final float GLOW_RADIUS = 0.6f;
    /** The dialogs' width and right margin, fractions of the screen's width. */
    private static final float DIALOG_WIDTH = 0.6f;
    private static final float DIALOG_MARGIN = 0.025f;

    private static final int SKY_TOP = Color.parseColor("#0E2A31");
    private static final int SKY_BOTTOM = Color.parseColor("#020608");
    private static final int GLOW = Color.parseColor("#38F39A2B");
    private static final int GOLD = Color.parseColor("#FFC861");

    /** A star: x, y (fractions), radius (dp), alpha. */
    private static final class Star {
        final float x;
        final float y;
        final float radius;
        final int alpha;

        Star(float x, float y, float radius, int alpha) {
            this.x = x;
            this.y = y;
            this.radius = radius;
            this.alpha = alpha;
        }
    }

    private final Paint sky = new Paint();
    private final Paint glow = new Paint();
    private final Paint star = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint icon = new Paint(Paint.FILTER_BITMAP_FLAG | Paint.ANTI_ALIAS_FLAG);
    private final Paint name = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Bitmap bitmap;
    private final Star[] stars;

    private Backdrop(Activity activity) {
        super(activity);
        setId(VIEW_ID);
        bitmap = BitmapFactory.decodeResource(activity.getResources(), R.drawable.icon);
        name.setColor(GOLD);
        name.setTextAlign(Paint.Align.CENTER);
        name.setFakeBoldText(true);
        name.setLetterSpacing(NAME_SPACING);
        star.setColor(Color.WHITE);

        // .NET's Random: the C# build's sky, star for star.
        stars = new Star[STAR_COUNT];
        try (DotNetRandom random = new DotNetRandom(STAR_SEED)) {
            for (int i = 0; i < STAR_COUNT; i++) {
                float x = random.nextSingle();
                float y = random.nextSingle();
                float radius = STAR_MIN_RADIUS + random.nextSingle() * STAR_RADIUS_RANGE;
                int alpha = STAR_MIN_ALPHA + random.next(STAR_ALPHA_RANGE);
                stars[i] = new Star(x, y, radius, alpha);
            }
        }
    }

    /** Shows the backdrop over SDL's surface (from any thread). */
    static void show(Activity activity) {
        int full = ViewGroup.LayoutParams.MATCH_PARENT;
        activity.runOnUiThread(() -> activity.addContentView(new Backdrop(activity), new ViewGroup.LayoutParams(full, full)));
    }

    /** A shown dialog moves right of the icon, undimmed, while the backdrop is up. UI thread. */
    static void place(Activity activity, Dialog dialog) {
        Window window = dialog.getWindow();
        if (!(activity.findViewById(VIEW_ID) instanceof Backdrop) || window == null) {
            return;
        }

        // The window's width, else the screen's long side (the activity is landscape): before the
        // first layout the window is 0 wide (an invisible dialog), and before the turn to
        // landscape the screen's width is the portrait one.
        int width = activity.getWindow().getDecorView().getWidth();
        if (width == 0) {
            DisplayMetrics screen = activity.getResources().getDisplayMetrics();
            width = Math.max(screen.widthPixels, screen.heightPixels);
        }

        window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        window.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        WindowManager.LayoutParams attributes = window.getAttributes();
        attributes.x = (int) (width * DIALOG_MARGIN);
        window.setAttributes(attributes);
        window.setLayout((int) (width * DIALOG_WIDTH), ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    /** Hidden for good: the icon's pixels go before the game's. */
    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        if (bitmap != null) {
            bitmap.recycle();
        }
    }

    /** The gradients follow the size. */
    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        sky.setShader(new LinearGradient(0, 0, 0, h, SKY_TOP, SKY_BOTTOM, Shader.TileMode.CLAMP));
        glow.setShader(new RadialGradient(w * ICON_X, h * ICON_Y, h * GLOW_RADIUS, GLOW, Color.TRANSPARENT, Shader.TileMode.CLAMP));
        name.setTextSize(h * NAME_SIZE);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        canvas.drawRect(0, 0, w, h, sky);
        canvas.drawRect(0, 0, w, h, glow);

        // A sparse sky, smaller stars fainter.
        float density = getResources().getDisplayMetrics().density;
        for (Star s : stars) {
            star.setAlpha(s.alpha);
            canvas.drawCircle(s.x * w, s.y * h, s.radius * density, star);
        }

        // The icon, the name under it.
        float half = h * ICON_SIDE / 2;
        float cx = w * ICON_X;
        float cy = h * ICON_Y;
        if (bitmap != null) {
            canvas.drawBitmap(bitmap, null, new RectF(cx - half, cy - half, cx + half, cy + half), icon);
        }

        canvas.drawText(NAME, cx, cy + half + name.getTextSize() * NAME_GAP, name);
    }
}
