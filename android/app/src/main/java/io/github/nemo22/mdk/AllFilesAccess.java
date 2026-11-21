package io.github.nemo22.mdk;

import android.Manifest;
import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;

import java.util.concurrent.CompletableFuture;

import io.github.nemo22.mdk.game.FolderAccess;

/**
 * Access to shared storage by path: checked, and asked for (the system's All files access
 * screen from Android 11, the storage permission dialog before). Asking runs on the setup
 * screen's worker thread and waits for the user's answer, which the activity passes on.
 * <p>
 * Reading the MDK folder in place: All files access from Android 11, the storage permission
 * before (Android 10 also needs requestLegacyExternalStorage, in the manifest). A store build
 * (-PstoreBuild=true) leaves them out (src/standard/AndroidManifest.xml has them) and always
 * copies.
 */
final class AllFilesAccess {
    /** The All files access screen's request code ("MA"). */
    static final int SETTINGS_REQUEST = 0x4d41;
    /** The storage permission's request code ("MP"). */
    static final int PERMISSION_REQUEST = 0x4d50;
    private static final String PACKAGE_SCHEME = "package:";

    private static final String[] PERMISSIONS =
        { Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE };

    private final Activity activity;
    private volatile CompletableFuture<Void> answered;

    AllFilesAccess(Activity activity) {
        this.activity = activity;
    }

    /** All files access: Android 11 (API 30) on. */
    private static boolean hasAllFiles() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.R;
    }

    FolderAccess current() {
        if (BuildConfig.STORE_BUILD) {
            return FolderAccess.UNAVAILABLE;
        }

        return granted() ? FolderAccess.GRANTED : FolderAccess.ASKABLE;
    }

    /** Asks for the access; returns whether it's granted once the user is back. */
    boolean request() {
        if (current() != FolderAccess.ASKABLE) {
            return current() == FolderAccess.GRANTED;
        }

        CompletableFuture<Void> wait = new CompletableFuture<>();
        answered = wait;
        activity.runOnUiThread(this::ask);
        wait.join();
        return granted();
    }

    /** The settings screen closed or the permission dialog was answered (the activity passes
     * them on). Returns whether it was ours. */
    boolean onAnswer(int request) {
        if (request != SETTINGS_REQUEST && request != PERMISSION_REQUEST) {
            return false;
        }

        CompletableFuture<Void> wait = answered;
        if (wait != null) {
            wait.complete(null);
        }

        return true;
    }

    private boolean granted() {
        if (hasAllFiles()) {
            return Environment.isExternalStorageManager();
        }

        for (String permission : PERMISSIONS) {
            if (activity.checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
                return false;
            }
        }

        return true;
    }

    /** All files access's settings screen from Android 11, the storage permission dialog before. */
    private void ask() {
        if (!hasAllFiles()) {
            activity.requestPermissions(PERMISSIONS, PERMISSION_REQUEST);
            return;
        }

        openSettings();
    }

    /** The app's All files access switch; some devices only have the list of apps. */
    private void openSettings() {
        try {
            Uri app = Uri.parse(PACKAGE_SCHEME + activity.getPackageName());
            activity.startActivityForResult(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, app), SETTINGS_REQUEST);
        } catch (ActivityNotFoundException e) {
            try {
                activity.startActivityForResult(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION), SETTINGS_REQUEST);
            } catch (ActivityNotFoundException again) {
                CompletableFuture<Void> wait = answered;
                if (wait != null) {
                    wait.complete(null);
                }
            }
        }
    }
}
