package io.github.nemo22.mdk.game;

/** A copy's progress: files done of all, after each file (called on the copying thread). */
public interface Progress {
    void update(int done, int total);
}
