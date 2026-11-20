package io.github.nemo22.mdk.game;

/**
 * The file path of a folder picked with the system's folder picker (a tree URI's authority and
 * document id), for the game to read it in place with All files access (C++
 * Mdk::Game::Flow::TreePaths).
 * <pre>
 *   externalstorage  primary:MDK        ─► /storage/emulated/0/MDK
 *                    home:MDK           ─► /storage/emulated/0/Documents/MDK
 *                    1234-ABCD:MDK      ─► /storage/1234-ABCD/MDK (SD card)
 *   downloads        raw:/storage/...   ─► /storage/...
 *   anything else (cloud, msf:42)       ─► null
 * </pre>
 */
public final class TreePaths {
    static {
        NativeLibrary.load();
    }

    private TreePaths() {
    }

    /** The picked folder's path; null when it has none (a cloud, a Downloads list entry). */
    public static native String resolve(String authority, String documentId, String primaryRoot);

    /** The primary storage's root, from the app's external files folder; null when that isn't on it. */
    public static native String primaryRoot(String appFiles);
}
