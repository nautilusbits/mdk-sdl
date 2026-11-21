package io.github.nemo22.mdk.game;

/** A file or folder of a tree (C++ Mdk::Formats::DataEntry); a file's size in bytes, or UNKNOWN_SIZE. */
public final class DataEntry {
    /** The kinds, in Mdk::Formats::EntryKind's order. */
    public static final int FILE = 0;
    public static final int FOLDER = 1;
    public static final long UNKNOWN_SIZE = -1;

    // Read by the native code.
    final String name;
    final int kind;
    final long size;

    public DataEntry(String name, int kind, long size) {
        this.name = name;
        this.kind = kind;
        this.size = size;
    }
}
