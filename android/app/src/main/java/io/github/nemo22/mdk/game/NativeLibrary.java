package io.github.nemo22.mdk.game;

import io.github.nemo22.mdk.BuildConfig;

/**
 * libmain.so (the C++ game, with SDL3 inside unless -PsharedSdl=true), loaded once by the first class of this package
 * used: the setup screen calls the game's code before SDL's activity loads it again (a no-op).
 */
final class NativeLibrary {
    private static final String[] LIBRARIES = BuildConfig.SHARED_SDL
        ? new String[] { "SDL3", "main" } : new String[] { "main" };
    private static boolean loaded;

    private NativeLibrary() {
    }

    static synchronized void load() {
        if (loaded) {
            return;
        }

        for (String library : LIBRARIES) {
            System.loadLibrary(library);
        }

        loaded = true;
    }
}
