package io.github.nemo22.mdk.game;

/**
 * An installation of the original game (C++ Mdk::Formats::MdkData, which this holds).
 * <p>
 * Importing: an installation somewhere the game can't read files by path (Android's shared
 * storage) is copied once into a folder of the program's own.
 * <pre>
 *   picked folder ─findIn─► "" or "MDK" ─importFrom─► app folder/TRAVERSE, MISC... + stamp ─imported─► MdkData
 * </pre>
 */
public final class MdkData {
    static {
        NativeLibrary.load();
    }

    /** The C++ object: a std::shared_ptr's address, released when this is collected. */
    private final long handle;
    private final String dir;

    // Made by the native code.
    private MdkData(long handle, String dir) {
        this.handle = handle;
        this.dir = dir;
    }

    public String dir() {
        return dir;
    }

    /** An installation read in place (the folder the user chose): {@code folder} or a folder of
     * it (the user picked the folder above, e.g. "MDK"); null when there's none or it can't be read. */
    public static native MdkData at(String folder);

    /** A finished import in {@code dir}, or null. */
    public static native MdkData imported(String dir);

    /** The installation in a tree: its root (""), a folder of the root (the user picked the
     * folder above, e.g. "MDK"), or null. */
    public static native String findIn(DataTree tree);

    /** Copies the game's folders of {@code root} in the tree into {@code dir}, reporting (files
     * done, files) after each file. A file already there with the same size is kept (an import
     * again copies what changed). C#'s MdkData.Import. */
    public static native MdkData importFrom(DataTree tree, String root, String dir, Progress progress)
        throws java.io.IOException;

    /** Copies the folder {@code name} (any case) of {@code root} in the tree into {@code target},
     * e.g. HD textures made on a PC; returns the files copied (0 without that folder). Unchanged
     * files are kept, as in importFrom. */
    public static native int copyFolder(DataTree tree, String root, String name, String target, Progress progress)
        throws java.io.IOException;

    private static native void release(long handle);

    @SuppressWarnings("deprecation")
    @Override
    protected void finalize() throws Throwable {
        try {
            release(handle);
        } finally {
            super.finalize();
        }
    }
}
