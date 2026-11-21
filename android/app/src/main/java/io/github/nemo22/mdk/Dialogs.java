package io.github.nemo22.mdk;

import android.app.Activity;
import android.app.AlertDialog;
import android.view.View;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.util.concurrent.CompletableFuture;

/** An answer of {@link Dialogs#ask}. */
enum Answer { YES, NO }

/** A dialog's title and message. */
final class Prompt {
    final String title;
    final String text;

    Prompt(String title, String text) {
        this.title = title;
        this.text = text;
    }

    /** C#'s {@code with { Text = text }}. */
    Prompt withText(String text) {
        return new Prompt(title, text);
    }
}

/**
 * Android's dialogs, waited for from a worker thread (they show on the UI thread): platform
 * Material in MDK's colours (res/values/styles.xml), beside the {@link Backdrop} while it's up.
 */
final class Dialogs {
    private Dialogs() {
    }

    /** A message with two buttons; returns the one tapped. */
    static Answer ask(Activity activity, Prompt prompt, String yes, String no) {
        CompletableFuture<Answer> answer = new CompletableFuture<>();
        activity.runOnUiThread(() -> present(activity, build(activity, prompt.title)
            .setMessage(prompt.text)
            .setPositiveButton(yes, (dialog, which) -> answer.complete(Answer.YES))
            .setNegativeButton(no, (dialog, which) -> answer.complete(Answer.NO))));
        return answer.join();
    }

    /** A message with an OK button; returns once it's tapped. */
    static void tell(Activity activity, Prompt prompt) {
        CompletableFuture<Void> closed = new CompletableFuture<>();
        activity.runOnUiThread(() -> present(activity, build(activity, prompt.title)
            .setMessage(prompt.text)
            .setPositiveButton("OK", (dialog, which) -> closed.complete(null))));
        closed.join();
    }

    /** A progress without buttons; the caller updates and closes it. */
    static ProgressBox show(Activity activity, String title, String first) {
        CompletableFuture<ProgressBox> shown = new CompletableFuture<>();
        activity.runOnUiThread(() -> {
            View view = activity.getLayoutInflater().inflate(R.layout.progress, null);
            ((TextView) view.findViewById(R.id.progress_label)).setText(first);
            AlertDialog dialog = present(activity, build(activity, title).setView(view));
            shown.complete(new ProgressBox(activity, dialog, view));
        });
        return shown.join();
    }

    private static AlertDialog.Builder build(Activity activity, String title) {
        return new AlertDialog.Builder(activity, R.style.MdkDialog)
            .setTitle(title)
            .setCancelable(false);
    }

    private static AlertDialog present(Activity activity, AlertDialog.Builder builder) {
        AlertDialog dialog = builder.show();
        Backdrop.place(activity, dialog);
        return dialog;
    }
}

/** A shown progress ({@link Dialogs#show}): what's copied, a bar, done of all. Updated from any thread. */
final class ProgressBox {
    private final Activity activity;
    private final AlertDialog dialog;
    private final TextView label;
    private final ProgressBar bar;
    private final TextView count;

    ProgressBox(Activity activity, AlertDialog dialog, View view) {
        this.activity = activity;
        this.dialog = dialog;
        label = view.findViewById(R.id.progress_label);
        bar = view.findViewById(R.id.progress_bar);
        count = view.findViewById(R.id.progress_count);
    }

    /** E.g. "Game files", 120 of 401. */
    void update(String what, int done, int total) {
        activity.runOnUiThread(() -> {
            label.setText(what);
            bar.setIndeterminate(false);
            bar.setMax(total);
            bar.setProgress(done);
            count.setText(done + " of " + total);
        });
    }

    void close() {
        activity.runOnUiThread(dialog::dismiss);
    }
}
