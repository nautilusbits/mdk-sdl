package io.github.nemo22.mdk.game;

/** .NET's seeded System.Random (C++ Mdk::Random): the same sequence for the same seed. */
public final class DotNetRandom implements AutoCloseable {
    static {
        NativeLibrary.load();
    }

    private long handle;

    public DotNetRandom(int seed) {
        handle = create(seed);
    }

    /** Random.NextSingle: [0, 1). */
    public float nextSingle() {
        return nextSingle(handle);
    }

    /** Random.Next(maxValue): [0, maxValue). */
    public int next(int maxValue) {
        return next(handle, maxValue);
    }

    @Override
    public void close() {
        release(handle);
        handle = 0;
    }

    private static native long create(int seed);

    private static native float nextSingle(long handle);

    private static native int next(long handle, int maxValue);

    private static native void release(long handle);
}
