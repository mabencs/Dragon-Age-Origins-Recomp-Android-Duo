package dev.daorecompandroid.duo;

import java.util.HashMap;
import java.util.Map;

/**
 * Where the deck's data comes from (PLAN 8.3): the state mirror's snapshot - the token
 * paths the game's own UI observes, each with the text it was last shown - and the frame
 * counts. Through DeckService from the deck's own process, or straight from the native
 * side when the deck is a Presentation in the game's (8.1's other route).
 */
interface DeckSource {

    /** The snapshot as "path\tvalue" lines, or null if the game cannot be reached. */
    String snapshot();

    /** {now ns, the game's swaps, presents, intervals ns...}, or null. */
    long[] frames();

    /**
     * PLAN 8.8, the deck's one write path: quick slot `slot` (0-5) tapped. Returns "" when
     * the game will press it, else why not; null if the game cannot be reached. Never on
     * the UI thread.
     */
    String quickslot(int slot);

    /** Each party slot's face generation (0 none yet), or null. */
    int[] portraitGenerations();

    /** A party slot's face, 146x146 RGBA premultiplied, or null. */
    byte[] portrait(int slot);

    /** The snapshot's lines as a map. */
    static Map<String, String> parse(String snapshot) {
        Map<String, String> values = new HashMap<>();
        if (snapshot == null) {
            return values;
        }
        int start = 0;
        while (start < snapshot.length()) {
            int end = snapshot.indexOf('\n', start);
            if (end < 0) {
                end = snapshot.length();
            }
            int tab = snapshot.indexOf('\t', start);
            if (tab > start && tab < end) {
                values.put(snapshot.substring(start, tab), snapshot.substring(tab + 1, end));
            }
            start = end + 1;
        }
        return values;
    }

    /** The deck in the game's own process: the native side directly. */
    final class Native implements DeckSource {
        @Override
        public String snapshot() {
            return NativeBridge.deckSnapshot() + NativeBridge.deckRender();
        }

        @Override
        public long[] frames() {
            return NativeBridge.deckFrames();
        }

        @Override
        public String quickslot(int slot) {
            return NativeBridge.deckQuickslot(slot);
        }

        @Override
        public int[] portraitGenerations() {
            return NativeBridge.deckPortraitGenerations();
        }

        @Override
        public byte[] portrait(int slot) {
            return NativeBridge.deckPortrait(slot);
        }
    }
}
