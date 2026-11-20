package io.github.nemo22.mdk.game;

/** The enhanced look's HD textures (C++ Mdk::Game::HdTextures::HdGenerator): an older build's
 * folder of them, moved into its mod. */
public final class HdGenerator {
    static {
        NativeLibrary.load();
    }

    /** Where older builds kept the HD textures, in the user folder. */
    public static final String OLD_FOLDER = oldFolder();

    private HdGenerator() {
    }

    /** Moves older builds' textures-hd/ into the mod (once; its images keep their keys). Returns
     * how many images moved. */
    public static native int migrate(String userFolder);

    private static native String oldFolder();
}
