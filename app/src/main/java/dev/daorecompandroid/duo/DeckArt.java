package dev.daorecompandroid.duo;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The game's own UI art for the deck (PLAN 8.4): cells of the HUD's shared atlas - the
 * portrait's ring and its health and mana or stamina arcs, the menu's gold-edged bar, the
 * ornate divider, the journal's heading bar - read from the owner's extracted game data,
 * once, into the deck's cache. Nothing of it is in the APK or the repository.
 *
 * <p>The atlas: {@code atl_shared_dxt5_00.xds} and its {@code atl_shared_dxt5_dat.xml}
 * in {@code guiexport.erf}. An .xds is the texture as the Xenos GPU reads it - DXT5 4x4
 * blocks, tiled (Xenia's Tiled2D address), 16-bit words byte-swapped - with the texture
 * fetch constant in its last 24 bytes; the XML gives each cell's place on the 2048x2048
 * page. As {@code tools/dao_xds.py} decodes them on the computer.
 */
final class DeckArt {

    private static final String TAG = "WardenDeck";
    private static final int VERSION = 1;

    // The cells the deck uses (their names in the atlas, which are the movies' images).
    static final String PORTRAIT_DISC = "Portraits_IEF";
    static final String PORTRAIT_RING = "Portraits_I6A";
    static final String PORTRAIT_SELECTED = "Portraits_I8E";
    static final String HEALTH_ARC = "Portraits_I6F";
    static final String MANA_ARC = "Portraits_I80";
    static final String STAMINA_ARC = "Portraits_I89";
    static final String TAB_SELECTED = "MainMenu_I7F";
    static final String DIVIDER = "PopupLayer_IB5";
    static final String HEADING_BAR = "journal_I84";
    private static final String[] CELLS = {PORTRAIT_DISC, PORTRAIT_RING, PORTRAIT_SELECTED,
        HEALTH_ARC, MANA_ARC, STAMINA_ARC, TAB_SELECTED, DIVIDER, HEADING_BAR};

    private final Map<String, Bitmap> cells = new HashMap<>();
    // The items' icons: the shared DXT1 atlas's page (atl_shared_dxt1_00.xds, 890 cells
    // named as the items' Icon field - "ico_clothing", the plot items' "plt_ico_..."), each
    // cut out when first asked.
    private Bitmap iconPage;
    private final Map<String, int[]> iconRects = new HashMap<>();
    private final Map<String, Bitmap> icons = new HashMap<>();
    // An icon not in the atlas: the engine's next place for an img:// name, a texture of
    // its own in gui.erf (the promotional items' - "luckystone_module" ...), decoded when
    // first asked. The names found in neither are logged once.
    private File guiErf;
    private final java.util.Set<String> missing = new java.util.HashSet<>();

    /** An item's icon by its Icon field, or null. */
    Bitmap icon(String name) {
        if (name == null || name.isEmpty() || iconPage == null) {
            return null;
        }
        Bitmap icon = icons.get(name);
        if (icon == null) {
            int[] r = iconRects.get(name.toLowerCase(java.util.Locale.ROOT));
            if (r == null || r[0] + r[2] > iconPage.getWidth()
                    || r[1] + r[3] > iconPage.getHeight()) {
                return standaloneIcon(name);
            }
            icon = Bitmap.createBitmap(iconPage, r[0], r[1], r[2], r[3]);
            icons.put(name, icon);
        }
        return icon;
    }

    private Bitmap standaloneIcon(String name) {
        String key = name.toLowerCase(java.util.Locale.ROOT);
        if (missing.contains(key)) {
            return null;
        }
        Bitmap icon = null;
        if (guiErf != null && key.matches("[a-z0-9_]+")) {
            try {
                Page page = decode(DeckFonts.readErfEntry(guiErf, key + ".xds"));
                icon = page.crop(0, 0, page.width, page.height);
            } catch (Exception e) {
                icon = null;  // not there either
            }
        }
        if (icon == null) {
            missing.add(key);
            Log.i(TAG, "deck: no icon for " + name);
            return null;
        }
        icons.put(name, icon);
        return icon;
    }

    Bitmap get(String name) {
        return cells.get(name);
    }

    /** The cells, from the cache or decoded now; null if the game data cannot give them. */
    static DeckArt load(Context context) {
        File dir = new File(context.getCacheDir(), "deck-art-v" + VERSION);
        DeckArt art = new DeckArt();
        try {
            boolean cached = true;
            for (String cell : CELLS) {
                cached &= new File(dir, cell + ".png").isFile();
            }
            if (!cached) {
                long started = System.nanoTime();
                File erf = new File(GameData.destination(context),
                        "packages/core/data/guiexport.erf");
                byte[] xds = DeckFonts.readErfEntry(erf, "atl_shared_dxt5_00.xds");
                String xml = new String(DeckFonts.readErfEntry(erf, "atl_shared_dxt5_dat.xml"),
                        StandardCharsets.UTF_8);
                Page page = decode(xds);
                dir.mkdirs();
                for (String cell : CELLS) {
                    int[] rect = cellRect(xml, cell);
                    if (rect == null) {
                        throw new IOException(cell + " is not in the atlas");
                    }
                    Bitmap bitmap = page.crop(rect[0], rect[1], rect[2], rect[3]);
                    File partial = new File(dir, cell + ".png.part");
                    try (FileOutputStream out = new FileOutputStream(partial)) {
                        bitmap.compress(Bitmap.CompressFormat.PNG, 100, out);
                    }
                    if (!partial.renameTo(new File(dir, cell + ".png"))) {
                        throw new IOException("cannot rename " + partial);
                    }
                }
                Log.i(TAG, "deck: the game's art taken from its atlas in "
                        + (System.nanoTime() - started) / 1_000_000 + " ms");
            }
            for (String cell : CELLS) {
                Bitmap bitmap = BitmapFactory.decodeFile(new File(dir, cell + ".png").getPath());
                if (bitmap == null) {
                    throw new IOException("cannot read " + cell);
                }
                art.cells.put(cell, bitmap);
            }
            loadIcons(context, dir, art);
            art.guiErf = new File(GameData.destination(context), "packages/core/data/gui.erf");
            return art;
        } catch (Exception e) {
            Log.w(TAG, "deck: the game's art is not available - " + e);
            for (String cell : CELLS) {
                new File(dir, cell + ".png").delete();
            }
            return null;
        }
    }

    /** The items' icons: the DXT1 page (decoded once into the cache) and its cells. */
    private static void loadIcons(Context context, File dir, DeckArt art) {
        try {
            File erf = new File(GameData.destination(context), "packages/core/data/guiexport.erf");
            String xml = new String(DeckFonts.readErfEntry(erf, "atl_shared_dxt1_dat.xml"),
                    StandardCharsets.UTF_8);
            Matcher element = Pattern.compile("<SourceTexture\\b([^>]*?)/?>").matcher(xml);
            Pattern attributes = Pattern.compile("(\\w+)=\"([^\"]*)\"");
            while (element.find()) {
                Map<String, String> a = new HashMap<>();
                Matcher m = attributes.matcher(element.group(1));
                while (m.find()) {
                    a.put(m.group(1), m.group(2));
                }
                String name = a.get("Name");
                String lower = name == null ? "" : name.toLowerCase(java.util.Locale.ROOT);
                // The items' ("ico_clothing") and the plot items' ("plt_ico_commem_coin",
                // the Runic Worry Token's - run 480); and, for the quickbar (PLAN 8.8), the
                // talents', spells', skills', effects' and recipes' ("tlt_ico_",
                // "spl_ico_", "skl_ico_survival_4" ...).
                if (!lower.startsWith("ico_") && !lower.contains("_ico_")) {
                    continue;
                }
                String[] offset = a.get("OffsetAndScale").trim().split("\\s+");
                String[] atlas = a.get("AtlasSize").trim().split("\\s+");
                String[] size = a.get("OriginalImageSize").trim().split("\\s+");
                art.iconRects.put(name.substring(0, name.length() - 4).toLowerCase(java.util.Locale.ROOT),
                        new int[] {Math.round(Float.parseFloat(offset[0]) * Integer.parseInt(atlas[0])),
                            Math.round(Float.parseFloat(offset[1]) * Integer.parseInt(atlas[1])),
                            Integer.parseInt(size[0]), Integer.parseInt(size[1])});
            }
            File pageFile = new File(dir, "icons_page.png");
            if (!pageFile.isFile()) {
                long started = System.nanoTime();
                Page page = decode(DeckFonts.readErfEntry(erf, "atl_shared_dxt1_00.xds"));
                Bitmap whole = page.crop(0, 0, page.width, page.height);
                File partial = new File(dir, "icons_page.png.part");
                try (FileOutputStream out = new FileOutputStream(partial)) {
                    whole.compress(Bitmap.CompressFormat.PNG, 100, out);
                }
                if (!partial.renameTo(pageFile)) {
                    throw new IOException("cannot rename " + partial);
                }
                art.iconPage = whole;
                Log.i(TAG, "deck: the items' icons taken from their atlas in "
                        + (System.nanoTime() - started) / 1_000_000 + " ms ("
                        + art.iconRects.size() + " icons)");
            } else {
                art.iconPage = BitmapFactory.decodeFile(pageFile.getPath());
            }
        } catch (Exception e) {
            Log.w(TAG, "deck: the items' icons are not available - " + e);
            art.iconPage = null;
        }
    }

    /** The cell's {x, y, width, height} on its page, from the atlas XML. */
    private static int[] cellRect(String xml, String cell) {
        Matcher element = Pattern.compile("<SourceTexture\\b([^>]*?)/?>").matcher(xml);
        while (element.find()) {
            Map<String, String> attributes = new HashMap<>();
            Matcher attribute = Pattern.compile("(\\w+)=\"([^\"]*)\"").matcher(element.group(1));
            while (attribute.find()) {
                attributes.put(attribute.group(1), attribute.group(2));
            }
            String name = attributes.get("Name");
            if (name == null || !name.equalsIgnoreCase(cell + ".dds")) {
                continue;
            }
            String[] offset = attributes.get("OffsetAndScale").trim().split("\\s+");
            String[] atlas = attributes.get("AtlasSize").trim().split("\\s+");
            String[] size = attributes.get("OriginalImageSize").trim().split("\\s+");
            int atlasWidth = Integer.parseInt(atlas[0]);
            int atlasHeight = Integer.parseInt(atlas[1]);
            return new int[] {Math.round(Float.parseFloat(offset[0]) * atlasWidth),
                Math.round(Float.parseFloat(offset[1]) * atlasHeight),
                Integer.parseInt(size[0]), Integer.parseInt(size[1])};
        }
        return null;
    }

    // ---- the .xds page ----

    static final class Page {
        int width;
        int height;
        int[] argb;

        Bitmap crop(int x, int y, int w, int h) {
            Bitmap bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            int[] row = new int[w];
            for (int j = 0; j < h; j++) {
                System.arraycopy(argb, (y + j) * width + x, row, 0, w);
                bitmap.setPixels(row, 0, w, 0, j, w, 1);
            }
            return bitmap;
        }
    }

    static Page decode(byte[] data) throws IOException {
        if (data.length < 52) {
            throw new IOException("too short for an .xds");
        }
        ByteBuffer trailer = ByteBuffer.wrap(data, data.length - 24, 24).order(ByteOrder.BIG_ENDIAN);
        int word0 = trailer.getInt();
        int word1 = trailer.getInt();
        int word2 = trailer.getInt();
        boolean tiled = (word0 >>> 31) != 0;
        int pitch = ((word0 >>> 22) & 0x1FF) * 32;
        int format = word1 & 0x3F;
        int endian = (word1 >>> 6) & 0x3;
        int width = (word2 & 0x1FFF) + 1;
        int height = ((word2 >>> 13) & 0x1FFF) + 1;
        int block;
        if (format == 0x14) {
            block = 16;  // DXT5
        } else if (format == 0x12) {
            block = 8;   // DXT1
        } else {
            throw new IOException("texture format " + format + " is not DXT1 or DXT5");
        }
        int blocksWide = (width + 3) / 4;
        int blocksHigh = (height + 3) / 4;
        int pitchBlocks = Math.max(pitch / 4, (blocksWide + 31) & ~31);
        int log2 = block == 16 ? 4 : 3;
        Page page = new Page();
        page.width = width;
        page.height = height;
        page.argb = new int[width * height];
        byte[] one = new byte[block];
        int[] pixels = new int[16];
        for (int by = 0; by < blocksHigh; by++) {
            for (int bx = 0; bx < blocksWide; bx++) {
                int src = tiled ? tiled2d(bx, by, pitchBlocks, log2)
                        : (by * pitchBlocks + bx) * block;
                if (src + block > data.length - 52) {
                    continue;
                }
                System.arraycopy(data, src, one, 0, block);
                swap(one, endian);
                if (block == 16) {
                    dxt5(one, pixels);
                } else {
                    colours(one, 0, pixels, true);
                }
                for (int py = 0; py < 4; py++) {
                    int y = by * 4 + py;
                    if (y >= height) {
                        break;
                    }
                    for (int px = 0; px < 4; px++) {
                        int x = bx * 4 + px;
                        if (x < width) {
                            page.argb[y * width + x] = pixels[py * 4 + px];
                        }
                    }
                }
            }
        }
        return page;
    }

    /** Xenia's texture_address::Tiled2D: the byte offset of block (x, y). */
    private static int tiled2d(int x, int y, int pitchAligned, int bytesPerBlockLog2) {
        int outer = (((y >> 5) * (pitchAligned >> 5)) + (x >> 5)) << 6;
        int inner = (((y >> 1) & 0b111) << 3) | (x & 0b111);
        int oib = (outer | inner) << bytesPerBlockLog2;
        int bank = (y >> 4) & 1;
        int pipe = ((x >> 3) & 0b11) ^ (((y >> 3) & 1) << 1);
        return ((y & 1) << 4) | (pipe << 6) | (bank << 11) | (oib & 0b1111)
                | (((oib >> 4) & 1) << 5) | (((oib >> 5) & 0b111) << 8) | ((oib >> 8) << 12);
    }

    private static void swap(byte[] b, int endian) {
        if (endian == 1) {  // k_8in16
            for (int i = 0; i + 1 < b.length; i += 2) {
                byte t = b[i];
                b[i] = b[i + 1];
                b[i + 1] = t;
            }
        } else if (endian == 2) {  // k_8in32
            for (int i = 0; i + 3 < b.length; i += 4) {
                byte t0 = b[i];
                byte t1 = b[i + 1];
                b[i] = b[i + 3];
                b[i + 1] = b[i + 2];
                b[i + 2] = t1;
                b[i + 3] = t0;
            }
        } else if (endian == 3) {  // k_16in32
            for (int i = 0; i + 3 < b.length; i += 4) {
                byte t0 = b[i];
                byte t1 = b[i + 1];
                b[i] = b[i + 2];
                b[i + 1] = b[i + 3];
                b[i + 2] = t0;
                b[i + 3] = t1;
            }
        }
    }

    private static int u16(byte[] b, int at) {
        return (b[at] & 0xFF) | ((b[at + 1] & 0xFF) << 8);
    }

    /** A DXT5 block: the alpha's eight levels and 3-bit indices, then DXT1's colours. */
    private static void dxt5(byte[] b, int[] out) {
        int a0 = b[0] & 0xFF;
        int a1 = b[1] & 0xFF;
        int[] alpha = new int[8];
        alpha[0] = a0;
        alpha[1] = a1;
        if (a0 > a1) {
            for (int i = 1; i < 7; i++) {
                alpha[i + 1] = ((7 - i) * a0 + i * a1) / 7;
            }
        } else {
            for (int i = 1; i < 5; i++) {
                alpha[i + 1] = ((5 - i) * a0 + i * a1) / 5;
            }
            alpha[6] = 0;
            alpha[7] = 255;
        }
        long bits = 0;
        for (int i = 0; i < 6; i++) {
            bits |= (long) (b[2 + i] & 0xFF) << (8 * i);
        }
        colours(b, 8, out, false);
        for (int i = 0; i < 16; i++) {
            int a = alpha[(int) ((bits >> (3 * i)) & 7)];
            out[i] = (out[i] & 0x00FFFFFF) | (a << 24);
        }
    }

    /** DXT1's colour block at `at`: two 565 colours and 2-bit indices. */
    private static void colours(byte[] b, int at, int[] out, boolean dxt1) {
        int c0 = u16(b, at);
        int c1 = u16(b, at + 2);
        int[] r = new int[4];
        int[] g = new int[4];
        int[] bl = new int[4];
        int[] a = {255, 255, 255, 255};
        r[0] = ((c0 >> 11) & 31) * 255 / 31;
        g[0] = ((c0 >> 5) & 63) * 255 / 63;
        bl[0] = (c0 & 31) * 255 / 31;
        r[1] = ((c1 >> 11) & 31) * 255 / 31;
        g[1] = ((c1 >> 5) & 63) * 255 / 63;
        bl[1] = (c1 & 31) * 255 / 31;
        if (!dxt1 || c0 > c1) {
            r[2] = (2 * r[0] + r[1]) / 3;
            g[2] = (2 * g[0] + g[1]) / 3;
            bl[2] = (2 * bl[0] + bl[1]) / 3;
            r[3] = (r[0] + 2 * r[1]) / 3;
            g[3] = (g[0] + 2 * g[1]) / 3;
            bl[3] = (bl[0] + 2 * bl[1]) / 3;
        } else {
            r[2] = (r[0] + r[1]) / 2;
            g[2] = (g[0] + g[1]) / 2;
            bl[2] = (bl[0] + bl[1]) / 2;
            a[3] = 0;
        }
        int indices = (b[at + 4] & 0xFF) | ((b[at + 5] & 0xFF) << 8) | ((b[at + 6] & 0xFF) << 16)
                | ((b[at + 7] & 0xFF) << 24);
        for (int i = 0; i < 16; i++) {
            int k = (indices >>> (2 * i)) & 3;
            out[i] = (a[k] << 24) | (r[k] << 16) | (g[k] << 8) | bl[k];
        }
    }
}
