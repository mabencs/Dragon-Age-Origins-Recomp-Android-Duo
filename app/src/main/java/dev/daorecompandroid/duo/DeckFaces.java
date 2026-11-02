package dev.daorecompandroid.duo;

import android.graphics.Bitmap;

import java.nio.ByteBuffer;

/**
 * The deck's faces: the HUD's party portraits, which the game renders from the 3D models
 * as it runs and the native side reads back from the GPU (warden_deck.cc). Polled on the
 * deck's pull thread: each party slot's generation is compared with the last one seen,
 * and only a changed face is fetched (146x146, 85 KB) and made a bitmap.
 */
final class DeckFaces {

    static final int SLOTS = 4;
    private static final int SIDE = 146;

    /** Where the faces come from: the native side, or DeckService from another process. */
    interface Fetch {
        int[] generations();

        byte[] face(int slot);
    }

    /** A changed face, for the UI thread. */
    interface Sink {
        void face(int slot, Bitmap face);
    }

    private final int[] known = new int[SLOTS];

    /** Fetches what changed since the last poll and hands each face on. Pull thread. */
    void poll(Fetch fetch, Sink sink) {
        int[] generations = fetch.generations();
        if (generations == null) {
            return;
        }
        for (int slot = 0; slot < SLOTS && slot < generations.length; slot++) {
            if (generations[slot] == 0 || generations[slot] == known[slot]) {
                continue;
            }
            byte[] rgba = fetch.face(slot);
            if (rgba == null || rgba.length != SIDE * SIDE * 4) {
                continue;
            }
            known[slot] = generations[slot];
            Bitmap face = Bitmap.createBitmap(SIDE, SIDE, Bitmap.Config.ARGB_8888);
            // ARGB_8888 is R G B A in memory, premultiplied - as the native side gives it.
            face.copyPixelsFromBuffer(ByteBuffer.wrap(rgba));
            sink.face(slot, face);
        }
    }
}
