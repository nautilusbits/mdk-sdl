package io.github.nemo22.mdk.game;

/** An exception of the game's own code (C# in the original, C++ here) that isn't an IOException
 * (those arrive as java.io.IOException), with its C# type name: Java's catches of Java
 * exceptions let it through, as the C# did. */
public final class NativeException extends RuntimeException {
    private final String typeName;

    public NativeException(String typeName, String message) {
        super(message);
        this.typeName = typeName;
    }

    /** C#'s GetType().Name, e.g. "InvalidDataException". */
    public String typeName() {
        return typeName;
    }
}
