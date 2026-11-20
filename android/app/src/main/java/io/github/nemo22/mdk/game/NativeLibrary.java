package io.github.nemo22.mdk.game;

/**
 * libmain.so (the C++ game, with SDL3 under it), loaded once by the first class of this package
 * used: the setup screen calls the game's code before SDL's activity loads it again (a no-op).
 */
final class NativeLibrary {
    private static final String[] LIBRARIES = { "SDL3", "main" };
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
