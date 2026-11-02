package dev.daorecompandroid.duo;

import android.app.Activity;
import android.app.ActivityOptions;
import android.app.Presentation;
import android.content.Context;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.WindowManager;

import java.io.File;
import java.util.Map;

/**
 * Opening the second screen's deck as the game starts (PLAN 8.1, 8.2), from GameActivity.
 *
 * <p>The display is found by what it can do, never by the handheld's name
 * (SECOND-SCREEN.md §2): a presentation display other than the default one, switched on.
 * Without one - a single-screen device - there is no deck and nothing else changes.
 *
 * <p>Two routes, Q6: by default {@link DeckActivity} in its own process, launched onto
 * the display (Route A); files/deck-route holding "presentation" makes it a
 * {@link Presentation} in the game's process instead (Route B), to compare. Either way the
 * window never takes key focus.
 */
final class Deck {

    private static final String TAG = "Warden";

    private Presentation presentation;
    private HandlerThread pullThread;

    /** The second display to put the deck on, or null. */
    static Display findDisplay(Context context) {
        DisplayManager displays = context.getSystemService(DisplayManager.class);
        if (displays == null) {
            return null;
        }
        for (Display display : displays.getDisplays(
                DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (display.getDisplayId() != Display.DEFAULT_DISPLAY
                    && display.getState() == Display.STATE_ON) {
                return display;
            }
        }
        return null;
    }

    /**
     * Opens the deck if the option wants it and there is a display for it; whether it
     * opened (the performance overlay shows on the main screen when it did not).
     */
    boolean open(Activity activity, File filesDir) {
        if ("off".equals(Options.read(filesDir, Options.byFile("deck")))) {
            Log.i(TAG, "deck: off (the options)");
            return false;
        }
        Display display = findDisplay(activity);
        if (display == null) {
            Log.i(TAG, "deck: no second display");
            return false;
        }
        String route = readWord(new File(filesDir, "deck-route"));
        Log.i(TAG, "deck: display " + display.getDisplayId() + " (" + display.getName()
                + "), " + ("presentation".equals(route) ? "a Presentation in the game's process"
                : "DeckActivity in its own process"));
        try {
            if ("presentation".equals(route)) {
                openPresentation(activity, display, filesDir);
            } else {
                ActivityOptions options = ActivityOptions.makeBasic();
                options.setLaunchDisplayId(display.getDisplayId());
                Intent intent = new Intent(activity, DeckActivity.class);
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                        | Intent.FLAG_ACTIVITY_MULTIPLE_TASK);
                Bundle bundle = options.toBundle();
                activity.startActivity(intent, bundle);
            }
            return true;
        } catch (RuntimeException e) {
            // No deck is better than no game.
            Log.e(TAG, "deck: could not be opened", e);
            return false;
        }
    }

    /** Route B: the deck as a Presentation, pulling straight from the native side. */
    private void openPresentation(Activity activity, Display display, File filesDir) {
        presentation = new Presentation(activity, display);
        presentation.getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        DeckView view = new DeckView(presentation.getContext(),
                new File(filesDir, "deck-trace").exists());
        presentation.setContentView(view);
        presentation.show();
        DeckSource source = new DeckSource.Native();
        Handler ui = new Handler(Looper.getMainLooper());
        pullThread = new HandlerThread("deck-pull");
        pullThread.start();
        Handler pull = new Handler(pullThread.getLooper());
        // PLAN 8.8: a quickbar tap, off the UI thread; the answer back on it.
        view.setQuickbar(slot -> pull.post(() -> {
            String why = source.quickslot(slot);
            ui.post(() -> view.quickslotAnswer(slot, why));
        }));
        pull.post(new Runnable() {
            @Override
            public void run() {
                if (presentation == null) {
                    return;
                }
                final Map<String, String> values = DeckSource.parse(source.snapshot());
                final long[] frames = source.frames();
                final boolean perf = "on".equals(
                        Options.read(filesDir, Options.byFile("perf")));
                ui.post(() -> {
                    if (presentation == null) {
                        return;
                    }
                    try {
                        view.setPerfVisible(perf);
                        view.update(values, frames, true);
                    } catch (RuntimeException e) {
                        Log.e(TAG, "deck: update failed - closing", e);
                        close();
                    }
                });
                if (pulls++ % 5 == 0) {
                    faces.poll(new DeckFaces.Fetch() {
                        @Override
                        public int[] generations() {
                            return source.portraitGenerations();
                        }

                        @Override
                        public byte[] face(int slot) {
                            return source.portrait(slot);
                        }
                    }, (slot, face) -> ui.post(() -> {
                        if (presentation != null) {
                            view.setFace(slot, face);
                        }
                    }));
                }
                pull.postDelayed(this, 100);
            }

            private final DeckFaces faces = new DeckFaces();
            private int pulls;
        });
        Log.i(TAG, "deck: Presentation shown on display " + display.getDisplayId());
    }

    /** Whether the deck was put away with the game ({@link #hide}), to come back with it. */
    private boolean hidden;

    /**
     * The game gone to the background (Home, Recents, another app): the deck goes too, so
     * the second screen is the system's again. Before (the owner, 2026-11-02), the deck
     * stayed while the main screen went home, and a second Home press - pressed for the
     * second screen - reached the home app already showing, which on the Thor
     * can bring the game back on the main screen. DeckActivity's task is finished (it is
     * opened again by {@link #show}); a Presentation is hidden.
     */
    void hide(Activity activity) {
        boolean found = presentation != null;
        if (presentation != null) {
            presentation.hide();
        }
        android.app.ActivityManager manager =
                activity.getSystemService(android.app.ActivityManager.class);
        if (manager != null) {
            for (android.app.ActivityManager.AppTask task : manager.getAppTasks()) {
                try {
                    android.content.ComponentName top = task.getTaskInfo().topActivity;
                    android.content.ComponentName first = task.getTaskInfo().baseActivity;
                    String deck = DeckActivity.class.getName();
                    if ((top != null && deck.equals(top.getClassName()))
                            || (first != null && deck.equals(first.getClassName()))) {
                        task.finishAndRemoveTask();
                        found = true;
                    }
                } catch (RuntimeException e) {
                    // A task gone meanwhile.
                }
            }
        }
        if (found) {
            hidden = true;
            Log.i(TAG, "deck: put away with the game");
        }
    }

    /** The game back in front: the deck put away by {@link #hide} opened again. */
    void show(Activity activity, File filesDir) {
        if (!hidden) {
            return;
        }
        hidden = false;
        if (presentation != null) {
            presentation.show();
            Log.i(TAG, "deck: back with the game");
            return;
        }
        Log.i(TAG, "deck: back with the game");
        open(activity, filesDir);
    }

    /** Closes a Presentation deck (DeckActivity closes with the game's process). */
    void close() {
        if (presentation != null) {
            presentation.dismiss();
            presentation = null;
        }
        if (pullThread != null) {
            pullThread.quitSafely();
            pullThread = null;
        }
    }

    private static String readWord(File file) {
        try (java.io.BufferedReader reader = new java.io.BufferedReader(
                new java.io.FileReader(file))) {
            String line = reader.readLine();
            return line == null ? "" : line.trim();
        } catch (Exception e) {
            return "";
        }
    }
}
