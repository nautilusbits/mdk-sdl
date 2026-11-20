package io.github.nemo22.mdk.game;

/** A start's plan; data the installation for IN_PLACE and COPIED. */
public final class StoragePlan {
    public final StorageKind kind;
    public final MdkData data;

    // Made by the native code.
    private StoragePlan(int kind, MdkData data) {
        this.kind = StorageKind.values()[kind];
        this.data = data;
    }
}
