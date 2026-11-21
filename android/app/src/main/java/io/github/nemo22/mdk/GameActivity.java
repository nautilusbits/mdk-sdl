package io.github.nemo22.mdk;

import android.content.Context;
import android.content.Intent;

import org.libsdl.app.SDLActivity;

/**
 * The game: SDL's activity (window, input, audio, the Vulkan surface). SDL runs SDL_main from
 * libmain.so (src/Mdk.Android/GameActivity.cpp) on SDL's thread with getArguments(): the folders
 * SetupActivity found, the level to start (0: the menu) and a bench build's options ("": none).
 */
public class GameActivity extends SDLActivity {
    private static final String DATA_EXTRA = "data";
    private static final String USER_EXTRA = "user";
    private static final String LEVEL_EXTRA = "level";
    private static final String OPTIONS_EXTRA = "options";

    /** The intent that plays {@code files}, from {@code level} (0: the menu), with the desktop's
     * {@code options} (null: none). */
    static Intent intentFor(Context context, GameFiles files, int level, String options) {
        return new Intent().setClass(context, GameActivity.class)
            .putExtra(DATA_EXTRA, files.data.dir())
            .putExtra(USER_EXTRA, files.userFolder)
            .putExtra(LEVEL_EXTRA, level)
            .putExtra(OPTIONS_EXTRA, options);
    }

    @Override
    protected String[] getLibraries() {
        // SDL is inside libmain.so unless built with -PsharedSdl=true.
        return BuildConfig.SHARED_SDL ? new String[] { "SDL3", "main" } : new String[] { "main" };
    }

    @Override
    protected String[] getArguments() {
        String data = getIntent().getStringExtra(DATA_EXTRA);
        String user = getIntent().getStringExtra(USER_EXTRA);
        int level = getIntent().getIntExtra(LEVEL_EXTRA, 0);
        String options = getIntent().getStringExtra(OPTIONS_EXTRA);
        return new String[] { data != null ? data : "", user != null ? user : "", Integer.toString(level), options != null ? options : "" };
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        // A bench run's process ends with the game: the script waits for it.
        if (getIntent().getStringExtra(OPTIONS_EXTRA) != null) {
            System.exit(0);
        }
    }
}
