package io.github.nemo22.mdk;

import android.content.Context;
import android.content.Intent;

import org.libsdl.app.SDLActivity;

/**
 * The game: SDL's activity (window, input, audio, the Vulkan surface). SDL runs SDL_main from
 * libmain.so (src/Mdk.Android/GameActivity.cpp) on SDL's thread with getArguments(): the folders
 * SetupActivity found and the level to start (0: the menu).
 */
public class GameActivity extends SDLActivity {
    private static final String DATA_EXTRA = "data";
    private static final String USER_EXTRA = "user";
    private static final String LEVEL_EXTRA = "level";

    /** The intent that plays {@code files}, from {@code level} (0: the menu). */
    static Intent intentFor(Context context, GameFiles files, int level) {
        return new Intent().setClass(context, GameActivity.class)
            .putExtra(DATA_EXTRA, files.data.dir())
            .putExtra(USER_EXTRA, files.userFolder)
            .putExtra(LEVEL_EXTRA, level);
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
        return new String[] { data != null ? data : "", user != null ? user : "", Integer.toString(level) };
    }
}
