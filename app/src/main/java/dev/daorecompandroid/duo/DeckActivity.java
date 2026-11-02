package dev.daorecompandroid.duo;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.util.Log;
import android.view.WindowManager;

import java.io.File;
import java.util.Map;

/**
 * The second screen's deck (PLAN 8.2), on the handheld's other display - the AYN Thor's
 * lower screen - while the game runs. Launched by GameActivity with
 * {@code ActivityOptions.setLaunchDisplayId} (8.1, Route A: a Presentation is the other
 * route, files/deck-route).
 *
 * <p><b>Its own process</b> ({@code :deck}, 8.7): an exception or a hang here ends this
 * process, never the game's - and the game process's main thread, which carries the
 * controller's input, never draws the deck. The data comes through {@link DeckService}
 * in the game's process, pulled ten times a second on a thread of the deck's own; when
 * the game's process goes, the binding dies and the deck closes.
 *
 * <p><b>Never focused:</b> the window cannot take key focus, so touching the deck leaves
 * the controller with the game (the Thor moves key focus to the display last touched).
 */
public final class DeckActivity extends Activity {

    private static final String TAG = "WardenDeck";
    private static final long PULL_MS = 100;

    private volatile DeckView deck;
    /** The pull thread's: what the game was last told the deck has open. */
    private String watchesSent = "";
    private HandlerThread pullThread;
    private Handler pull;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile IBinder service;
    private boolean bound;
    private volatile boolean closing;
    /** The party's faces, fetched when the game redraws them (every fifth pull). */
    private final DeckFaces faces = new DeckFaces();
    /**
     * The faces given so far, by slot - for the deck made again with the game's art (a
     * deck opened with the faces already drawn - the game back from Home - gets them at
     * its first pull, before that remake, which lost them).
     */
    private final android.graphics.Bitmap[] facesGiven = new android.graphics.Bitmap[DeckFaces.SLOTS];
    private int pulls;

    private final ServiceConnection connection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            Log.i(TAG, "deck: connected to the game");
            service = binder;
            giveFocusBack();
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            Log.i(TAG, "deck: the game's process is gone - closing");
            service = null;
            close();
        }

        @Override
        public void onBindingDied(ComponentName name) {
            onServiceDisconnected(name);
        }
    };

    private final Runnable pullOnce = new Runnable() {
        @Override
        public void run() {
            if (closing) {
                return;
            }
            IBinder binder = service;
            String snapshot = null;
            long[] frames = null;
            if (binder != null) {
                sendWatches(binder);
                snapshot = call(binder, DeckService.SNAPSHOT, false).text;
                frames = call(binder, DeckService.FRAMES, true).numbers;
            }
            final Map<String, String> values = DeckSource.parse(snapshot);
            final long[] frameStats = frames;
            final boolean connected = binder != null && snapshot != null;
            // The "Performance tab/overlay" option, taken at once (the same files dir).
            final boolean perf = "on".equals(
                    Options.read(getFilesDir(), Options.byFile("perf")));
            ui.post(() -> {
                if (!closing && deck != null) {
                    try {
                        deck.setPerfVisible(perf);
                        deck.update(values, frameStats, connected);
                    } catch (RuntimeException e) {
                        // The deck's own fault: it closes, the game is untouched.
                        Log.e(TAG, "deck: update failed - closing", e);
                        close();
                    }
                }
            });
            if (binder != null && pulls++ % 5 == 0) {
                faces.poll(new DeckFaces.Fetch() {
                    @Override
                    public int[] generations() {
                        return call(binder, DeckService.PORTRAITS, 0).ints;
                    }

                    @Override
                    public byte[] face(int slot) {
                        return call(binder, DeckService.PORTRAIT, slot).bytes;
                    }
                }, (slot, face) -> ui.post(() -> {
                    facesGiven[slot] = face;
                    if (!closing && deck != null) {
                        deck.setFace(slot, face);
                    }
                }));
            }
            pull.postDelayed(this, PULL_MS);
        }
    };

    /** What the deck has open, told to the game when it changes. */
    private void sendWatches(IBinder binder) {
        DeckView view = deck;
        String watches = view != null ? view.watches() : "";
        if (watches.equals(watchesSent)) {
            return;
        }
        Parcel data = Parcel.obtain();
        Parcel answer = Parcel.obtain();
        try {
            data.writeString(watches);
            if (binder.transact(DeckService.WATCH, data, answer, 0)) {
                watchesSent = watches;
            }
        } catch (Exception e) {
            // The game's process is gone: the connection's callback closes the deck.
        } finally {
            data.recycle();
            answer.recycle();
        }
    }

    private static final class Reply {
        String text;
        long[] numbers;
        int[] ints;
        byte[] bytes;
    }

    /** The deck's faces' calls: PORTRAITS answers ints, PORTRAIT (with the slot) bytes. */
    private Reply call(IBinder binder, int code, int argument) {
        Reply reply = new Reply();
        Parcel data = Parcel.obtain();
        Parcel answer = Parcel.obtain();
        try {
            data.writeInt(argument);
            if (binder.transact(code, data, answer, 0)) {
                if (code == DeckService.PORTRAITS) {
                    reply.ints = answer.createIntArray();
                } else {
                    reply.bytes = answer.createByteArray();
                }
            }
        } catch (Exception e) {
            // The game's process is gone: the connection's callback closes the deck.
        } finally {
            data.recycle();
            answer.recycle();
        }
        return reply;
    }

    /**
     * PLAN 8.8: a quickbar tap sent to the game on the pull thread - the deck's one write
     * call - and the answer shown on the deck ("" pressed, else why not, null unreachable).
     */
    private void sendQuickslot(int slot) {
        Handler handler = pull;
        if (handler == null) {
            return;
        }
        handler.post(() -> {
            IBinder binder = service;
            String why = null;
            if (binder != null) {
                Parcel data = Parcel.obtain();
                Parcel answer = Parcel.obtain();
                try {
                    data.writeInt(slot);
                    if (binder.transact(DeckService.QUICKSLOT, data, answer, 0)) {
                        why = answer.readString();
                    }
                } catch (Exception e) {
                    // The game's process is gone: the connection's callback closes the deck.
                } finally {
                    data.recycle();
                    answer.recycle();
                }
            }
            final String result = why;
            ui.post(() -> {
                if (!closing && deck != null) {
                    deck.quickslotAnswer(slot, result);
                }
            });
        });
    }

    /** The Items tab's buttons, on the pull thread; the answer back on the deck. */
    private void sendItem(int action, int item, int ability) {
        Handler handler = pull;
        if (handler == null) {
            return;
        }
        handler.post(() -> {
            IBinder binder = service;
            String why = null;
            if (binder != null) {
                Parcel data = Parcel.obtain();
                Parcel answer = Parcel.obtain();
                try {
                    data.writeInt(action);
                    data.writeInt(item);
                    data.writeInt(ability);
                    if (binder.transact(DeckService.ITEM, data, answer, 0)) {
                        why = answer.readString();
                    }
                } catch (Exception e) {
                    // The game's process is gone: the connection's callback closes the deck.
                } finally {
                    data.recycle();
                    answer.recycle();
                }
            }
            final String result = why;
            ui.post(() -> {
                if (!closing && deck != null) {
                    deck.itemAnswer(result);
                }
            });
        });
    }

    private Reply call(IBinder binder, int code, boolean numbers) {
        Reply reply = new Reply();
        Parcel data = Parcel.obtain();
        Parcel answer = Parcel.obtain();
        try {
            if (binder.transact(code, data, answer, 0)) {
                if (numbers) {
                    reply.numbers = answer.createLongArray();
                } else {
                    reply.text = answer.readString();
                }
            }
        } catch (Exception e) {
            // The game's process is gone (DeadObjectException): the connection's
            // callback closes the deck.
        } finally {
            data.recycle();
            answer.recycle();
        }
        return reply;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Before the window is added: never focusable, so the controller stays the game's.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        boolean showData = new File(getFilesDir(), "deck-trace").exists();
        deck = new DeckView(this, showData);
        deck.setQuickbar(this::sendQuickslot);
        deck.setItems(this::sendItem);
        setContentView(deck);
        // PLAN 8.4: the game's own typefaces and HUD art, made from its data the first
        // time (a few hundred milliseconds, off this thread) - the deck made again with
        // them.
        new Thread(() -> {
            DeckFonts fonts = DeckFonts.load(this);
            DeckArt art = DeckArt.load(this);
            if (fonts != null || art != null) {
                ui.post(() -> {
                    if (!closing) {
                        deck = new DeckView(this, showData, fonts, art);
                        deck.setQuickbar(this::sendQuickslot);
                        deck.setItems(this::sendItem);
                        for (int slot = 0; slot < facesGiven.length; slot++) {
                            if (facesGiven[slot] != null) {
                                deck.setFace(slot, facesGiven[slot]);
                            }
                        }
                        setContentView(deck);
                    }
                });
            }
        }, "deck-art").start();
        Log.i(TAG, "deck: on display " + getDisplay().getDisplayId() + ", "
                + getResources().getDisplayMetrics().widthPixels + "x"
                + getResources().getDisplayMetrics().heightPixels + ", in its own process");

        // Not BIND_AUTO_CREATE's restart of a dead game process: bound once, closed when
        // it goes.
        bound = bindService(new Intent(this, DeckService.class), connection,
                Context.BIND_AUTO_CREATE);
        if (!bound) {
            Log.w(TAG, "deck: the game's service cannot be bound - closing");
            finish();
            return;
        }
        pullThread = new HandlerThread("deck-pull");
        pullThread.start();
        pull = new Handler(pullThread.getLooper());
        pull.post(pullOnce);
    }

    /**
     * Launching the deck made its display the focused one - the controller's keys would go
     * to it, where no window takes them (run 454). The game's activity brought back to the
     * front makes the main display focused again, and a tap on the deck, whose window
     * cannot take focus, leaves it there (run 455). The game holds for the moment it takes
     * (0.0 s), once, as the deck opens. Only once connected: the game's process is alive,
     * so this never starts it again.
     */
    private boolean focusGiven;

    private void giveFocusBack() {
        if (focusGiven || closing) {
            return;
        }
        focusGiven = true;
        focusToGame();
    }

    /**
     * BUG-106: the deck's window takes no keys, so with this display on top - anything
     * that closes on this screen (the Thor's dual-screen assistant) leaves it there - the
     * first key waits on no window and Android calls the deck not responding. Each time
     * it comes to the top, the top goes back to the game, if the game is what is in front
     * (never over the player's Home screen).
     */
    @Override
    public void onTopResumedActivityChanged(boolean top) {
        super.onTopResumedActivityChanged(top);
        Handler handler = pull;
        if (!top || handler == null) {
            return;
        }
        handler.post(() -> {
            IBinder binder = service;
            if (binder != null && gameInFront(binder)) {
                Log.i(TAG, "deck: this display came to the top - back to the game");
                focusToGame();
            }
        });
    }

    private boolean gameInFront(IBinder binder) {
        Parcel data = Parcel.obtain();
        Parcel answer = Parcel.obtain();
        try {
            return binder.transact(DeckService.GAME_IN_FRONT, data, answer, 0)
                    && answer.readInt() != 0;
        } catch (Exception e) {
            return false;  // The game's process is gone: the connection's callback closes the deck.
        } finally {
            data.recycle();
            answer.recycle();
        }
    }

    private void focusToGame() {
        ui.post(() -> {
            if (closing) {
                return;
            }
            try {
                android.app.ActivityOptions options = android.app.ActivityOptions.makeBasic();
                options.setLaunchDisplayId(android.view.Display.DEFAULT_DISPLAY);
                startActivity(new Intent(this, GameActivity.class)
                        .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
                                | Intent.FLAG_ACTIVITY_NEW_TASK), options.toBundle());
                Log.i(TAG, "deck: the game's display focused again");
            } catch (RuntimeException e) {
                Log.e(TAG, "deck: could not give the focus back", e);
            }
        });
    }

    private void close() {
        if (closing) {
            return;
        }
        closing = true;
        ui.post(this::finishAndRemoveTask);
    }

    @Override
    protected void onDestroy() {
        closing = true;
        if (pull != null) {
            pull.removeCallbacksAndMessages(null);
        }
        if (pullThread != null) {
            pullThread.quitSafely();
        }
        if (bound) {
            try {
                unbindService(connection);
            } catch (IllegalArgumentException e) {
                // Already gone with the game's process.
            }
            bound = false;
        }
        super.onDestroy();
        Log.i(TAG, "deck: closed");
    }
}
