package dev.daorecompandroid.duo;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;

/**
 * The deck's window onto the game (PLAN 8.3, 8.7): in the game's process, answering the
 * deck - another process, {@link DeckActivity} - with the state mirror's snapshot and the
 * frame counts. A plain Binder, no AIDL: a few calls, each one parcel.
 *
 * <p>The calls arrive on the game process's binder threads, never its main thread (which
 * carries the controller's input) and never the game's: each copies what the native
 * mirror last published (warden_deck.cc) and returns. If the deck is slow or dies, the
 * game does not notice.
 */
public final class DeckService extends Service {

    /** The state mirror's snapshot: "path\tvalue" lines, as one string. */
    static final int SNAPSHOT = IBinder.FIRST_CALL_TRANSACTION;
    /** {now ns, the game's swaps, presents, the last frame intervals in ns...}. */
    static final int FRAMES = IBinder.FIRST_CALL_TRANSACTION + 1;
    /**
     * PLAN 8.8, the deck's one write path: an int, the quick slot tapped (0-5); answers ""
     * when the game will press it, else why not.
     */
    static final int QUICKSLOT = IBinder.FIRST_CALL_TRANSACTION + 2;
    /** The deck's faces: each party slot's face generation (int[]). */
    static final int PORTRAITS = IBinder.FIRST_CALL_TRANSACTION + 3;
    /** An int, the party slot: its face, 146x146 RGBA (byte[], or null). */
    static final int PORTRAIT = IBinder.FIRST_CALL_TRANSACTION + 4;

    private final Binder binder = new Binder() {
        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            switch (code) {
                case SNAPSHOT:
                    reply.writeString(NativeBridge.deckSnapshot() + NativeBridge.deckRender());
                    return true;
                case FRAMES:
                    reply.writeLongArray(NativeBridge.deckFrames());
                    return true;
                case QUICKSLOT:
                    reply.writeString(NativeBridge.deckQuickslot(data.readInt()));
                    return true;
                case PORTRAITS:
                    reply.writeIntArray(NativeBridge.deckPortraitGenerations());
                    return true;
                case PORTRAIT:
                    reply.writeByteArray(NativeBridge.deckPortrait(data.readInt()));
                    return true;
                default:
                    return super.onTransact(code, data, reply, flags);
            }
        }
    };

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }
}
