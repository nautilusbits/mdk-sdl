package io.github.nemo22.mdk.game;

import java.io.IOException;
import java.io.InputStream;

/** A folder tree an installation is copied from (C++ Mdk::Formats::IDataTree, which calls it):
 * the folder the user picked, read through its content URIs. Paths are relative, '/'-separated:
 * "" is the root. */
public interface DataTree {
    /** The files and folders of a folder. */
    DataEntry[] list(String folder);

    InputStream open(String file) throws IOException;
}
