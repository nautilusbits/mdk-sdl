package io.github.nemo22.mdk;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;

import org.libsdl.app.SDL;

import java.io.File;
import java.util.concurrent.CompletableFuture;

import io.github.nemo22.mdk.game.Log;
import io.github.nemo22.mdk.game.NativeException;

/** What the setup screen is shown for. */
enum SetupMode { START, REIMPORT, STOPPED }

/**
 * The launcher: the setup screen ({@link Backdrop} with {@link Dialogs} beside it) while
 * {@link DataSetup} finds the game's files on a worker thread, then the game
 * ({@link GameActivity}). The game shows it again, and waits on SDL's thread, for Options'
 * "Import from folder" and to say why it stopped (src/android/SetupActivity.cpp calls
 * {@link #reimport} and {@link #stopped}).
 */
public final class SetupActivity extends Activity {
    /** The imported game files, under the app's files folder. */
    private static final String DATA_FOLDER = "mdk";
    private static final String MODE_EXTRA = "mode";
    private static final String IN_PLACE_EXTRA = "inPlace";
    private static final String MESSAGE_EXTRA = "message";
    /** The game's own action: the launcher's intent would only bring the task back. */
    private static final String GAME_ACTION = "io.github.nemo22.mdk.SETUP";
    private static final String STOPPED_TITLE = "MDK stopped";
    /** A debug build's intent extra "level" starts that level (the emulator's tests:
     * {@code am start -p io.github.nemo22.mdk --ei level 3}). */
    private static final String LEVEL_EXTRA = "level";
    /** A debug or bench build's intent extra "options": the desktop's, e.g. {@code am start -p
     * io.github.nemo22.mdk.bench.cpp --es options "--level=7 --perf=10 --wait=40"} (tools/android_bench.sh). */
    private static final String OPTIONS_EXTRA = "options";

    /** The game's wait for the screen it showed. */
    private static volatile CompletableFuture<Void> closed;

    private DataSetup setup;

    /** Options' "Import from folder" (SDL's thread): returns once it's done or cancelled. */
    static void reimport(String inPlace) {
        await(SetupMode.REIMPORT, IN_PLACE_EXTRA, inPlace);
    }

    /** Why the game stopped (SDL's thread): returns once it's read. */
    static void stopped(String message) {
        await(SetupMode.STOPPED, MESSAGE_EXTRA, message);
    }

    /** The setup screen over the game, until it closes. */
    private static void await(SetupMode mode, String key, String value) {
        CompletableFuture<Void> wait = new CompletableFuture<>();
        closed = wait;
        Context context = SDL.getContext().getApplicationContext();
        Intent intent = new Intent(context, SetupActivity.class).setAction(GAME_ACTION)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_NO_ANIMATION)
            .putExtra(MODE_EXTRA, mode.ordinal())
            .putExtra(key, value);
        context.startActivity(intent);
        wait.join();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // The log keeps what's printed from here: mdk.log gets it once the game starts.
        Log.capture();
        File external = getExternalFilesDir(null);
        String files = external != null ? external.getAbsolutePath() : getFilesDir().getAbsolutePath();
        DataSetup setup = new DataSetup(this, new File(files, DATA_FOLDER).getPath(), files);
        this.setup = setup;
        SetupMode mode = SetupMode.values()[getIntent().getIntExtra(MODE_EXTRA, SetupMode.START.ordinal())];
        Backdrop.show(this);
        new Thread(() -> run(setup, mode)).start();
    }

    /** The worker thread: the mode's dialogs, then the game (first start) and close. */
    private void run(DataSetup setup, SetupMode mode) {
        try {
            show(setup, mode);
        } catch (Throwable e) {
            Log.error(android.util.Log.getStackTraceString(e));
            String text = e.getMessage() != null ? e.getMessage() : "";
            Dialogs.tell(this, new Prompt(STOPPED_TITLE, typeName(e) + ": " + text));
        } finally {
            runOnUiThread(this::finish);
            CompletableFuture<Void> wait = closed;
            if (wait != null) {
                wait.complete(null);
            }
        }
    }

    private void show(DataSetup setup, SetupMode mode) {
        if (mode == SetupMode.REIMPORT) {
            setup.reimport(getIntent().getStringExtra(IN_PLACE_EXTRA));
            return;
        }

        if (mode == SetupMode.STOPPED) {
            String message = getIntent().getStringExtra(MESSAGE_EXTRA);
            Dialogs.tell(this, new Prompt(STOPPED_TITLE, message != null ? message : ""));
            return;
        }

        // Same task, no transition: one black screen into the game.
        GameFiles found = setup.get();
        if (found != null) {
            runOnUiThread(() -> startActivity(GameActivity.intentFor(this, found, askedLevel(), askedOptions()).addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION)));
        }
    }

    /** The level a debug build was asked to start; 0 (the menu) otherwise. */
    private int askedLevel() {
        return BuildConfig.DEBUG ? getIntent().getIntExtra(LEVEL_EXTRA, 0) : 0;
    }

    /** The options a debug or bench build was asked for; null otherwise. */
    private String askedOptions() {
        return BuildConfig.DEBUG || BuildConfig.BENCH ? getIntent().getStringExtra(OPTIONS_EXTRA) : null;
    }

    /** C#'s GetType().Name: the game's own exceptions keep their C# names. */
    private static String typeName(Throwable error) {
        if (error instanceof NativeException) {
            return ((NativeException) error).typeName();
        }

        return error.getClass().getSimpleName();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (setup != null && setup.onResult(requestCode, resultCode, data)) {
            return;
        }

        super.onActivityResult(requestCode, resultCode, data);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (setup != null) {
            setup.onPermissions(requestCode);
        }
    }
}
