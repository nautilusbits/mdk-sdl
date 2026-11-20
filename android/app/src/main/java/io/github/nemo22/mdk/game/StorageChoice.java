package io.github.nemo22.mdk.game;

/**
 * The MDK folder the user chose to play from in place, remembered in a file of the app's own
 * folder and checked each start (C++ Mdk::Game::Flow::StorageChoice).
 * <pre>
 *   chosen? ─yes─► access and MDK there? ─yes─► InPlace
 *      │                  └no─► Lost (forget, plan again)
 *      └no─► copied before? ─yes─► Copied
 *               └no─► store build? ─yes─► CopyFlow
 *                        └no─► AskAccess
 * </pre>
 */
public final class StorageChoice {
    static {
        NativeLibrary.load();
    }

    private final String appFolder;

    public StorageChoice(String appFolder) {
        this.appFolder = appFolder;
    }

    /** The chosen folder (its installation's), or null. */
    public String folder() {
        return folder(appFolder);
    }

    public void choose(String folder) {
        choose(appFolder, folder);
    }

    public void forget() {
        forget(appFolder);
    }

    /** What this start does, given the access and the copy made before (or null). */
    public StoragePlan plan(FolderAccess access, MdkData copy) {
        return plan(appFolder, access.ordinal(), copy);
    }

    /** Settings and saves follow the user to a new folder; files already there are kept. */
    public static native void copyUserFiles(String from, String to);

    /** Writes and deletes a probe file: settings, saves and the log go there. */
    public static native boolean isWritable(String folder);

    private static native String folder(String appFolder);

    private static native void choose(String appFolder, String folder);

    private static native void forget(String appFolder);

    private static native StoragePlan plan(String appFolder, int access, MdkData copy);
}
