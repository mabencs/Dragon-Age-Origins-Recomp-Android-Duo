package dev.daorecompandroid.duo;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * The areas' own maps for the deck's Map tab (PLAN 8.5): the parchment map the game's map
 * screen shows, named by the data model's {@code Area.Minimap} ("ost000t_mmap_ph" at
 * Ostagar's King's Camp) - an .xds in the area's GPU archive,
 * {@code packages/core/env/<area>/<area>.gpu.rim} (the texture's name up to "_mmap"),
 * DXT1 and tiled like the UI's atlases. Decoded once at half size into the deck's cache,
 * from the owner's own game data; nothing of it in the APK or the repository.
 */
final class DeckMaps {

    private static final String TAG = "WardenDeck";
    private static final int VERSION = 1;

    private DeckMaps() {
    }

    /** The map named by Area.Minimap, or null; slow the first time (call off the UI thread). */
    static Bitmap load(Context context, String texture) {
        if (texture == null || texture.isEmpty() || !texture.matches("[A-Za-z0-9_]+")) {
            return null;
        }
        File dir = new File(context.getCacheDir(), "deck-maps-v" + VERSION);
        File cached = new File(dir, texture + ".png");
        if (cached.isFile()) {
            return BitmapFactory.decodeFile(cached.getPath());
        }
        int cut = texture.indexOf("_mmap");
        if (cut <= 0) {
            return null;
        }
        String area = texture.substring(0, cut);
        File data = GameData.destination(context);
        File[] archives = {
            new File(data, "packages/core/env/" + area + "/" + area + ".gpu.rim"),
            new File(data, "modules/single player/env/" + area + "/" + area + ".gpu.rim"),
        };
        for (File archive : archives) {
            if (!archive.isFile()) {
                continue;
            }
            try {
                long started = System.nanoTime();
                DeckArt.Page page = DeckArt.decode(DeckFonts.readErfEntry(archive, texture + ".xds"));
                Bitmap whole = page.crop(0, 0, page.width, page.height);
                Bitmap half = Bitmap.createScaledBitmap(whole, page.width / 2, page.height / 2, true);
                whole.recycle();
                dir.mkdirs();
                File partial = new File(dir, texture + ".png.part");
                try (FileOutputStream out = new FileOutputStream(partial)) {
                    half.compress(Bitmap.CompressFormat.PNG, 100, out);
                }
                if (!partial.renameTo(cached)) {
                    throw new IOException("cannot rename " + partial);
                }
                Log.i(TAG, "deck: the map " + texture + " taken from " + archive.getName() + " in "
                        + (System.nanoTime() - started) / 1_000_000 + " ms");
                return half;
            } catch (Exception e) {
                Log.w(TAG, "deck: the map " + texture + " is not available - " + e);
                return null;
            }
        }
        Log.w(TAG, "deck: no archive holds the map " + texture);
        return null;
    }
}
