package io.github.nemo22.mdk.game;

/** What a start does with the game's files (StorageChoice.plan). Mdk::Game::Flow::StorageKind,
 * in its order. */
public enum StorageKind {
    /** The chosen folder, read in place; it's the user folder too. */
    IN_PLACE,
    /** The copy in the app's folder (a store build, or an install from before). */
    COPIED,
    /** The chosen folder is gone or no longer readable: say so, forget it, plan again. */
    LOST,
    /** Nothing yet: ask for access to play in place (declined: copy). */
    ASK_ACCESS,
    /** Nothing yet and no access to ask for: copy. */
    COPY_FLOW,
}
