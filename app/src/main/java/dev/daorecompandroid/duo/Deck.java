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
 * <p>Two routes, Q6: by default a {@link Presentation} in the game's process (Route B);
 * files/deck-route holding "activity" makes it {@link DeckActivity} in its own process,
 * launched onto the display (Route A), as it was until BUG-116. Either way the window never
 * takes key focus.
 *
 * <p>BUG-116 (the owner chose Route B, 2026-11-03): on the AYN Thor an app with an activity
 * on the second screen is a second-screen app - AYN's Game Assistant lists its uid in
 * sys.audio.uids and AYN's AudioFlinger gives its sound the second screen's own volume
 * instead of the media volume. With DeckActivity there, the whole game had that volume. A
 * Presentation is a window of the game's activity, no task on that display, so the game is
 * never listed and Android applies its media volume to the game as to any app.
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

    /** Route A, DeckActivity on the second display - a test switch since BUG-116. */
    static boolean usesActivity(File filesDir) {
        return "activity".equals(readWord(new File(filesDir, "deck-route")));
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
        boolean asActivity = usesActivity(filesDir);
        Log.i(TAG, "deck: display " + display.getDisplayId() + " (" + display.getName()
                + "), " + (asActivity ? "DeckActivity in its own process"
                : "a Presentation in the game's process"));
        try {
            if (!asActivity) {
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
        final Presentation shown = presentation;
        final boolean showData = new File(filesDir, "deck-trace").exists();
        final DeckSource source = new DeckSource.Native();
        final Handler ui = new Handler(Looper.getMainLooper());
        pullThread = new HandlerThread("deck-pull");
        pullThread.start();
        final Handler pull = new Handler(pullThread.getLooper());
        // The faces given so far, by slot, for a deck made again with the game's art.
        final android.graphics.Bitmap[] facesGiven = new android.graphics.Bitmap[DeckFaces.SLOTS];
        // The deck in the window: plain first, then again with the game's typefaces and HUD
        // art (PLAN 8.4, as DeckActivity does), its taps wired each time.
        final DeckView[] current = new DeckView[1];
        java.util.function.Function<DeckView, DeckView> wire = view -> {
            // PLAN 8.8: a quickbar tap, off the UI thread; the answer back on it.
            view.setQuickbar(slot -> pull.post(() -> {
                String why = source.quickslot(slot);
                ui.post(() -> view.quickslotAnswer(slot, why));
            }));
            view.setItems((action, item, ability) -> pull.post(() -> {
                String why = source.item(action, item, ability);
                ui.post(() -> view.itemAnswer(why));
            }));
            for (int slot = 0; slot < facesGiven.length; slot++) {
                if (facesGiven[slot] != null) {
                    view.setFace(slot, facesGiven[slot]);
                }
            }
            current[0] = view;
            shown.setContentView(view);
            return view;
        };
        wire.apply(new DeckView(presentation.getContext(), showData));
        presentation.show();
        new Thread(() -> {
            DeckFonts fonts = DeckFonts.load(activity);
            DeckArt art = DeckArt.load(activity);
            if (fonts != null || art != null) {
                ui.post(() -> {
                    if (presentation == shown) {
                        wire.apply(new DeckView(shown.getContext(), showData, fonts, art));
                    }
                });
            }
        }, "deck-art").start();
        pull.post(new Runnable() {
            @Override
            public void run() {
                if (presentation != shown) {
                    return;
                }
                source.watch(current[0].watches());
                final Map<String, String> values = DeckSource.parse(source.snapshot());
                final long[] frames = source.frames();
                final boolean perf = "on".equals(
                        Options.read(filesDir, Options.byFile("perf")));
                ui.post(() -> {
                    if (presentation != shown) {
                        return;
                    }
                    try {
                        current[0].setPerfVisible(perf);
                        current[0].update(values, frames, true);
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
                        if (presentation == shown) {
                            facesGiven[slot] = face;
                            current[0].setFace(slot, face);
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
        stopWaiting();
        // BUG-110: wanted back whether or not it was found here: one gone with its process,
        // or one that never came back, opens with the game too.
        hidden = true;
        if (found) {
            Log.i(TAG, "deck: put away with the game");
        }
    }

    /** BUG-110: until the second display is on: the listener, and who it was asked of. */
    private DisplayManager.DisplayListener waiting;
    private DisplayManager waitingOn;

    /**
     * BUG-110: the game can come back before the second screen does - from the lid, by a
     * millisecond - and the one try found no display on. The deck opens as it comes on.
     */
    private void waitForDisplay(Activity activity, File filesDir) {
        DisplayManager displays = activity.getSystemService(DisplayManager.class);
        if (displays == null || waiting != null) {
            return;
        }
        Log.i(TAG, "deck: waiting for the second display");
        waitingOn = displays;
        waiting = new DisplayManager.DisplayListener() {
            private void check() {
                if (findDisplay(activity) != null) {
                    stopWaiting();
                    open(activity, filesDir);
                }
            }

            @Override
            public void onDisplayAdded(int displayId) {
                check();
            }

            @Override
            public void onDisplayChanged(int displayId) {
                check();
            }

            @Override
            public void onDisplayRemoved(int displayId) {
            }
        };
        Handler main = new Handler(Looper.getMainLooper());
        displays.registerDisplayListener(waiting, main);
        // It may have come on before the listener was there.
        DisplayManager.DisplayListener registered = waiting;
        main.post(() -> {
            if (waiting == registered) {
                registered.onDisplayChanged(Display.DEFAULT_DISPLAY);
            }
        });
    }

    private void stopWaiting() {
        if (waiting != null) {
            waitingOn.unregisterDisplayListener(waiting);
            waiting = null;
            waitingOn = null;
        }
    }

    /** The game back in front: the deck put away by {@link #hide} opened again. */
    void show(Activity activity, File filesDir) {
        if (!hidden) {
            return;
        }
        hidden = false;
        if (presentation != null) {
            // Android dismisses a Presentation whose display goes (BUG-110's wake, a cable):
            // shown again only on a display still there and on, else made afresh.
            Display shownOn = presentation.getDisplay();
            if (shownOn != null && shownOn.isValid() && shownOn.getState() == Display.STATE_ON) {
                presentation.show();
                Log.i(TAG, "deck: back with the game");
                return;
            }
            close();
        }
        Log.i(TAG, "deck: back with the game");
        // Not asking findDisplay again here: the display can come on between the two, and
        // then the deck neither opened nor waited.
        if (!open(activity, filesDir)
                && !"off".equals(Options.read(filesDir, Options.byFile("deck")))) {
            waitForDisplay(activity, filesDir);
        }
    }

    /** Closes a Presentation deck (DeckActivity closes with the game's process). */
    void close() {
        stopWaiting();
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
