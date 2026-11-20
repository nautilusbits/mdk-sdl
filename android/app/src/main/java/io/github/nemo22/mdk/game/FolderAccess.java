package io.github.nemo22.mdk.game;

/** Whether the game may read and write shared storage by path (Android's All files access;
 * storage permission before Android 11). Mdk::Game::Flow::FolderAccess, in its order. */
public enum FolderAccess {
    GRANTED,
    /** Not granted, but the user can be asked. */
    ASKABLE,
    /** A store build: never asked, the files are copied. */
    UNAVAILABLE,
}
