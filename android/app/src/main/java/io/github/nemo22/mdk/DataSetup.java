package io.github.nemo22.mdk;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.File;
import java.util.concurrent.CompletableFuture;

import io.github.nemo22.mdk.game.FolderAccess;
import io.github.nemo22.mdk.game.HdGenerator;
import io.github.nemo22.mdk.game.MdkData;
import io.github.nemo22.mdk.game.ModCatalog;
import io.github.nemo22.mdk.game.NativeException;
import io.github.nemo22.mdk.game.Progress;
import io.github.nemo22.mdk.game.StorageChoice;
import io.github.nemo22.mdk.game.StorageKind;
import io.github.nemo22.mdk.game.StoragePlan;
import io.github.nemo22.mdk.game.TreePaths;

/** The game's files and the user folder (settings, saves, mods, mdk.log). */
final class GameFiles {
    final MdkData data;
    final String userFolder;

    GameFiles(MdkData data, String userFolder) {
        this.data = data;
        this.userFolder = userFolder;
    }
}

/**
 * The game's files on first start. With All files access the user picks the MDK folder copied
 * to the phone and the game plays from it in place, keeping settings, saves and mods there
 * ({@link StorageChoice} remembers it, each start checks it). Without (declined, a store build)
 * its game folders are copied into the app's own folder once, and its {@code mods} (HD textures
 * made on a PC among them) into the user folder. An install that copied before keeps its copy.
 * Options, "Import from folder" picks it again: in place, another folder (or, from the copy, the
 * folder in place); copied, what's new or changed. Runs on the setup screen's worker thread;
 * dialogs show on the UI thread.
 * <pre>
 *   chosen folder readable? ─yes─► MdkData in place
 *      └gone─► "can't read it" ─► forget ─┐
 *   copied before? ─yes─► MdkData in the app ◄┘
 *      └no─► "play in place?" ─allow─► All files access ─► folder picker ─► path, MDK, writable? ─► remember
 *              └copy instead, refused, store build ─► folder picker ─► MDK in it? ─► copy (progress)
 * </pre>
 */
final class DataSetup {
    /** The folder picker's request code ("MD"). */
    static final int PICK_REQUEST = 0x4d44;
    /** The progress shows every this many files. */
    private static final int PROGRESS_STEP = 20;
    private static final Prompt INTRO = new Prompt("Copy the game files", "Copy your MDK installation (GOG, Steam or CD: the folder with TRAVERSE, MISC, FALL3D and STREAM) to this device, then pick that folder.\n\nIts game files (about 170 MB) are copied into the app once.");
    private static final Prompt NOT_MDK = new Prompt("Not an MDK folder", "That folder has no MDK game files (TRAVERSE/TRAVSPRT.BNI). Pick the MDK folder.");
    private static final String COPY_FAILED = "Copying failed";
    private static final Prompt AGAIN = new Prompt("Import from folder", "Pick your MDK folder again: new or changed game files are copied, and its mods (HD textures made on a PC among them).");
    private static final Prompt ACCESS_INTRO = new Prompt("Play from your MDK folder?", "MDK can play straight from your MDK installation (GOG, Steam or CD: the folder with TRAVERSE, MISC, FALL3D and STREAM) copied to this device, keeping settings, saves and mods there.\n\nThat needs All files access: switch it on for MDK on the next screen, then come back.\n\nOr copy the game files (about 170 MB) into the app instead.");
    private static final Prompt PICK_INTRO = new Prompt("Pick your MDK folder", "Pick the MDK folder on this device's storage (e.g. MDK on the internal storage). The game reads it in place and keeps its settings, saves and mods there.");
    private static final Prompt PICK_AGAIN = new Prompt("Pick your MDK folder", "Pick the MDK folder to play from. Settings and saves are copied there; the game reads it from the next start.");
    private static final Prompt SWITCH = new Prompt("Import from folder", "Play from your MDK folder in place (All files access; settings and saves are copied there, from the next start), or copy new or changed game files into the app again?");
    private static final Prompt NO_ACCESS = new Prompt("No All files access", "MDK can't read the folder in place without All files access.");
    private static final Prompt NO_PATH = new Prompt("Not on this device", "That folder isn't on this device's storage (a cloud, the Downloads list?).\n\nIn the picker's menu, show the internal storage, then pick your MDK folder.");
    private static final Prompt NOT_WRITABLE = new Prompt("Folder is read-only", "MDK can't write to that folder (settings and saves go there). Pick a folder on the internal storage.");
    private static final Prompt LOST = new Prompt("MDK folder not found", "MDK can't read {0} any more (moved, deleted, or All files access switched off). Saves there stay there; pick the folder again to play from it.");
    private static final Prompt NEXT_START = new Prompt("Folder changed", "MDK plays from {0} from the next start; your settings and saves were copied there.");
    private static final Prompt IMPORTED = new Prompt("Import done", "HD textures apply from the next level.");
    /** string.Format's first argument. */
    private static final String ARGUMENT = "{0}";
    /** The primary storage's usual root, when the app's folder doesn't tell. */
    private static final String DEFAULT_PRIMARY = "/storage/emulated/0";

    private final Activity activity;
    private final String dir;
    private final String userFolder;
    private final AllFilesAccess access;
    private final StorageChoice choice;
    private volatile CompletableFuture<Uri> picked;

    DataSetup(Activity activity, String dir, String userFolder) {
        this.activity = activity;
        this.dir = dir;
        this.userFolder = userFolder;
        access = new AllFilesAccess(activity);
        choice = new StorageChoice(userFolder);
    }

    /** The installation: the chosen folder, the copy, or asked for; null when the user quits. */
    GameFiles get() {
        StoragePlan plan = choice.plan(access.current(), MdkData.imported(dir));
        if (plan.kind == StorageKind.LOST) {
            Dialogs.tell(activity, format(LOST, choice.folder()));
            choice.forget();
            plan = choice.plan(access.current(), MdkData.imported(dir));
        }

        if (plan.data != null) {
            return new GameFiles(plan.data, plan.kind == StorageKind.IN_PLACE ? plan.data.dir() : userFolder);
        }

        // Declined, refused or backed out of: the copy, as before.
        if (plan.kind == StorageKind.ASK_ACCESS
            && Dialogs.ask(activity, ACCESS_INTRO, "Allow", "Copy instead") == Answer.YES) {
            MdkData chosen = chooseInPlace(PICK_INTRO, "Copy instead");
            if (chosen != null) {
                return new GameFiles(chosen, chosen.dir());
            }
        }

        MdkData copy = pickAndCopy(INTRO, "Quit");
        return copy != null ? new GameFiles(copy, userFolder) : null;
    }

    /** Options' "Import from folder". In place: another folder, from the next start. Copied: the
     * folder in place from the next start (not on a store build), or picked and copied again. The
     * game goes on with the files it has when the user cancels. {@code inPlace}: the folder the
     * game plays from in place, or null (the copy). */
    void reimport(String inPlace) {
        if (inPlace != null) {
            moveTo(inPlace, PICK_AGAIN);
            return;
        }

        if (access.current() != FolderAccess.UNAVAILABLE && Dialogs.ask(activity, SWITCH, "Play in place", "Copy again") == Answer.YES) {
            moveTo(userFolder, PICK_AGAIN);
            return;
        }

        if (pickAndCopy(AGAIN, "Cancel") != null) {
            Dialogs.tell(activity, IMPORTED);
        }
    }

    /** A folder to play from in place from the next start; settings and saves follow. */
    private void moveTo(String current, Prompt intro) {
        MdkData chosen = chooseInPlace(intro, "Cancel");
        if (chosen == null || chosen.dir().equals(current)) {
            return;
        }

        StorageChoice.copyUserFiles(current, chosen.dir());
        Dialogs.tell(activity, format(NEXT_START, chosen.dir()));
    }

    /** All files access, then the MDK folder until one has the game and can be written
     * (remembered: its data) or the user refuses (null). */
    private MdkData chooseInPlace(Prompt intro, String no) {
        if (!access.request()) {
            Dialogs.tell(activity, NO_ACCESS);
            return null;
        }

        String root = TreePaths.primaryRoot(userFolder);
        String primary = root != null ? root : DEFAULT_PRIMARY;
        Prompt message = intro;
        while (Dialogs.ask(activity, message, "Pick folder", no) == Answer.YES) {
            Uri uri = pick();
            if (uri == null) {
                message = intro;
                continue;
            }

            String path = pathOf(uri, primary);
            if (path == null) {
                message = NO_PATH;
                continue;
            }

            MdkData data = MdkData.at(path);
            if (data == null) {
                message = NOT_MDK;
                continue;
            }

            if (!StorageChoice.isWritable(data.dir())) {
                message = NOT_WRITABLE;
                continue;
            }

            // An older build's textures-hd there moves into its mod, as a copy's does.
            HdGenerator.migrate(data.dir());
            choice.choose(data.dir());
            return data;
        }

        return null;
    }

    /** The picked tree's file path ({@link TreePaths}), or null. */
    private static String pathOf(Uri tree, String primary) {
        String authority = tree.getAuthority();
        if (authority == null) {
            return null;
        }

        String document = DocumentsContract.getTreeDocumentId(tree);
        if (document == null) {
            return null;
        }

        return TreePaths.resolve(authority, document, primary);
    }

    /** Asks for the MDK folder until one is copied (its data) or the user refuses (null). */
    private MdkData pickAndCopy(Prompt intro, String no) {
        Prompt message = intro;
        while (Dialogs.ask(activity, message, "Pick folder", no) == Answer.YES) {
            Uri uri = pick();
            if (uri == null) {
                message = intro;
                continue;
            }

            DocumentTree tree = new DocumentTree(activity.getContentResolver(), uri);
            String root = MdkData.findIn(tree);
            if (root == null) {
                message = NOT_MDK;
                continue;
            }

            // The C# caught its IOException and Java's exceptions: the game's other errors go on.
            try {
                return copy(tree, root);
            } catch (NativeException e) {
                throw e;
            } catch (Exception e) {
                message = new Prompt(COPY_FAILED, e.getMessage() != null ? e.getMessage() : e.toString());
            }
        }

        return null;
    }

    /** The folder picker's answer, the All files access screen's or the storage permission's
     * (the activity passes them on). Returns whether it was ours. */
    boolean onResult(int request, int result, Intent data) {
        if (access.onAnswer(request)) {
            return true;
        }

        if (request != PICK_REQUEST) {
            return false;
        }

        CompletableFuture<Uri> wait = picked;
        if (wait != null) {
            wait.complete(result == Activity.RESULT_OK && data != null ? data.getData() : null);
        }

        return true;
    }

    /** The storage permission's answer (the activity passes it on). Returns whether it was ours. */
    boolean onPermissions(int request) {
        return access.onAnswer(request);
    }

    /** Android's folder picker; null when the user backs out. */
    private Uri pick() {
        CompletableFuture<Uri> wait = new CompletableFuture<>();
        picked = wait;
        activity.runOnUiThread(() -> activity.startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_REQUEST));
        return wait.join();
    }

    /** Copies the installation and its mods (HD textures made on a PC among them; an older
     * build's textures-hd moved into its mod), showing the progress. */
    private MdkData copy(DocumentTree tree, String root) throws java.io.IOException {
        ProgressBox progress = Dialogs.show(activity, "Copying the game files", "Reading the folder…");
        try {
            MdkData data = MdkData.importFrom(tree, root, dir, progress(progress, "Game files"));
            MdkData.copyFolder(tree, root, ModCatalog.FOLDER_NAME, ModCatalog.folderIn(userFolder), progress(progress, "Mods"));
            MdkData.copyFolder(tree, root, HdGenerator.OLD_FOLDER, new File(userFolder, HdGenerator.OLD_FOLDER).getPath(), progress(progress, "HD textures"));
            HdGenerator.migrate(userFolder);
            return data;
        } finally {
            progress.close();
        }
    }

    /** The progress of copying {@code what}, shown every PROGRESS_STEP files and at the end. */
    private static Progress progress(ProgressBox box, String what) {
        return (done, total) -> {
            if (done % PROGRESS_STEP == 0 || done == total) {
                box.update(what, done, total);
            }
        };
    }

    /** C#'s {@code prompt with { Text = string.Format(prompt.Text, value) }}. */
    private static Prompt format(Prompt prompt, String value) {
        return prompt.withText(prompt.text.replace(ARGUMENT, value != null ? value : ""));
    }
}
