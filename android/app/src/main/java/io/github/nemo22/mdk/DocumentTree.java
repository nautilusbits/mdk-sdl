package io.github.nemo22.mdk;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import io.github.nemo22.mdk.game.DataEntry;
import io.github.nemo22.mdk.game.DataTree;
import io.github.nemo22.mdk.game.NativeException;

/** A folder the user picked (Storage Access Framework), read through its content URIs: shared
 * storage has no file paths the game could open. The C++ import reads it (JavaTree). */
final class DocumentTree implements DataTree {
    private static final String[] COLUMNS = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_SIZE,
    };

    private final ContentResolver resolver;
    private final Uri tree;
    /** Document ids by relative path ("" the picked folder), learnt while listing. */
    private final Map<String, String> ids = new HashMap<>();

    DocumentTree(ContentResolver resolver, Uri tree) {
        this.resolver = resolver;
        this.tree = tree;
        ids.put("", DocumentsContract.getTreeDocumentId(tree));
    }

    @Override
    public DataEntry[] list(String folder) {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id(folder));
        List<DataEntry> entries = new ArrayList<>();
        try (Cursor cursor = resolver.query(children, COLUMNS, null, null, null)) {
            if (cursor == null) {
                return entries.toArray(new DataEntry[0]);
            }

            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                int kind = DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2)) ? DataEntry.FOLDER : DataEntry.FILE;
                long size = cursor.isNull(3) ? DataEntry.UNKNOWN_SIZE : cursor.getLong(3);
                ids.put(folder.isEmpty() ? name : folder + "/" + name, id);
                entries.add(new DataEntry(name, kind, size));
            }
        }

        return entries.toArray(new DataEntry[0]);
    }

    @Override
    public InputStream open(String file) throws IOException {
        Uri uri = DocumentsContract.buildDocumentUriUsingTree(tree, id(file));
        InputStream stream = resolver.openInputStream(uri);
        if (stream == null) {
            throw new IOException("Can't open " + file);
        }

        return stream;
    }

    /** C#'s dictionary indexer: an unknown path throws KeyNotFoundException, a game error. */
    private String id(String path) {
        String id = ids.get(path);
        if (id == null) {
            throw new NativeException("KeyNotFoundException", "The given key '" + path + "' was not present in the dictionary.");
        }

        return id;
    }
}
