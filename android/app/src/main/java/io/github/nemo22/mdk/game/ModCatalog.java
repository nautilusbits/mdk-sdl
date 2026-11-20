package io.github.nemo22.mdk.game;

/** The player's mods (C++ Mdk::Game::Mods::ModCatalog): their folder. */
public final class ModCatalog {
    static {
        NativeLibrary.load();
    }

    /** The mods' folder, in the user folder and in an installation. */
    public static final String FOLDER_NAME = folderName();

    private ModCatalog() {
    }

    /** The mods folder in a user folder. */
    public static native String folderIn(String userFolder);

    private static native String folderName();
}
