package io.github.nemo22.mdk.game;

/** The game's log (C++ Mdk::Engine::Diagnostics::Log and Console): what's printed goes to mdk.log
 * once the game knows the user folder. */
public final class Log {
    static {
        NativeLibrary.load();
    }

    private Log() {
    }

    /** Keeps what's printed from here (the ring of Log.Lines lines; the first call installs it). */
    public static native void capture();

    /** A line on the game's error output (C#'s Console.Error.WriteLine). */
    public static native void error(String line);
}
