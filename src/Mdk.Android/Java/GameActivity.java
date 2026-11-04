package io.github.nemo22.mdk;

import org.libsdl.app.SDLActivity;

/**
 * The game under NativeAOT: SDL's activity in Java (a C# subclass of the SDL binding needs
 * Reflection.Emit, which NativeAOT lacks). SDL runs SDL_main from libMdk.so (GameActivity.cs)
 * with getArguments(): the folders SetupActivity found and the level to start (0: the menu).
 */
public class GameActivity extends SDLActivity {
    private static final String DATA_EXTRA = "data";
    private static final String USER_EXTRA = "user";
    private static final String LEVEL_EXTRA = "level";

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "Mdk" };
    }

    @Override
    protected String[] getArguments() {
        String data = getIntent().getStringExtra(DATA_EXTRA);
        String user = getIntent().getStringExtra(USER_EXTRA);
        int level = getIntent().getIntExtra(LEVEL_EXTRA, 0);
        return new String[] { data != null ? data : "", user != null ? user : "", Integer.toString(level) };
    }
}
