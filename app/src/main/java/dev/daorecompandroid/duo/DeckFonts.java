package dev.daorecompandroid.duo;

import android.content.Context;
import android.graphics.Typeface;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;

/**
 * The game's own typefaces for the deck (PLAN 8.4): "Nogard", its headings' face, and
 * "Dragon Text", its body's - read from the owner's extracted game data and made into
 * TrueType on the device, once, into the deck's cache. Nothing of them is in the APK or
 * the repository.
 *
 * <p>Where they are: {@code fonts_en-us.gfx} in {@code packages/core/data/guiexport.erf}
 * (an ERF V2.1: 32-byte header, 44-byte entries - a 32-byte ASCII name, offset, packed and
 * unpacked lengths - zlib where they differ), a Scaleform movie ("CFX": zlib after 8
 * bytes) whose two DefineFont3 tags hold the glyphs as quadratic outlines - TrueType's own
 * kind of curve, so the conversion is a change of units (the EM's 20480 to 2048), of the
 * y axis, and of container. Checked first on the computer (session 37's prototype).
 */
final class DeckFonts {

    private static final String TAG = "WardenDeck";
    /** Bumped when the conversion changes, so a stale cache is made again. */
    private static final int VERSION = 1;

    Typeface heading;
    Typeface body;

    /** The typefaces, from the cache or made now; null if the game data cannot give them. */
    static DeckFonts load(Context context) {
        File dir = new File(context.getCacheDir(), "deck-fonts-v" + VERSION);
        File headingFile = new File(dir, "nogard.ttf");
        File bodyFile = new File(dir, "dragon_text.ttf");
        try {
            if (!headingFile.isFile() || !bodyFile.isFile()) {
                long started = System.nanoTime();
                File erf = new File(GameData.destination(context),
                        "packages/core/data/guiexport.erf");
                byte[] movie = inflateMovie(readErfEntry(erf, "fonts_en-us.gfx"));
                List<byte[]> fonts = definedFonts(movie);
                if (fonts.size() < 2) {
                    Log.w(TAG, "deck: fonts_en-us.gfx has " + fonts.size() + " fonts");
                    return null;
                }
                dir.mkdirs();
                write(headingFile, toTrueType(parseFont3(fonts.get(0)), "Dragon Age Nogard"));
                write(bodyFile, toTrueType(parseFont3(fonts.get(1)), "Dragon Age Text"));
                Log.i(TAG, "deck: the game's typefaces made in "
                        + (System.nanoTime() - started) / 1_000_000 + " ms");
            }
            DeckFonts fonts = new DeckFonts();
            fonts.heading = Typeface.createFromFile(headingFile);
            fonts.body = Typeface.createFromFile(bodyFile);
            return fonts;
        } catch (Exception e) {
            Log.w(TAG, "deck: the game's typefaces are not available - " + e);
            headingFile.delete();
            bodyFile.delete();
            return null;
        }
    }

    // ---- the archive and the movie ----

    static byte[] readErfEntry(File erf, String name) throws IOException {
        try (RandomAccessFile file = new RandomAccessFile(erf, "r")) {
            byte[] header = new byte[0x20];
            file.readFully(header);
            if (!new String(header, 0, 8, StandardCharsets.US_ASCII).equals("ERF V2.1")) {
                throw new IOException("not an ERF V2.1 archive");
            }
            int count = ByteBuffer.wrap(header, 0x10, 4).order(ByteOrder.LITTLE_ENDIAN).getInt();
            byte[] index = new byte[count * 44];
            file.readFully(index);
            ByteBuffer entries = ByteBuffer.wrap(index).order(ByteOrder.LITTLE_ENDIAN);
            for (int i = 0; i < count; i++) {
                int at = i * 44;
                int length = 0;
                while (length < 32 && index[at + length] != 0) {
                    length++;
                }
                if (!new String(index, at, length, StandardCharsets.US_ASCII)
                        .equalsIgnoreCase(name)) {
                    continue;
                }
                long offset = entries.getInt(at + 32) & 0xFFFFFFFFL;
                int packed = entries.getInt(at + 36);
                int unpacked = entries.getInt(at + 40);
                byte[] data = new byte[packed];
                file.seek(offset);
                file.readFully(data);
                return packed == unpacked ? data : inflate(data, 0, unpacked);
            }
        }
        throw new IOException(name + " is not in " + erf.getName());
    }

    private static byte[] inflateMovie(byte[] movie) throws IOException {
        String signature = new String(movie, 0, 3, StandardCharsets.US_ASCII);
        if (signature.equals("CFX") || signature.equals("CWS")) {
            byte[] rest = inflate(movie, 8, 0);
            byte[] whole = new byte[8 + rest.length];
            System.arraycopy(movie, 0, whole, 0, 8);
            System.arraycopy(rest, 0, whole, 8, rest.length);
            return whole;
        }
        return movie;
    }

    private static byte[] inflate(byte[] data, int from, int sizeHint) throws IOException {
        Inflater inflater = new Inflater();
        inflater.setInput(data, from, data.length - from);
        ByteArrayOutputStream out = new ByteArrayOutputStream(Math.max(sizeHint, 1024));
        byte[] buffer = new byte[65536];
        try {
            while (!inflater.finished()) {
                int n = inflater.inflate(buffer);
                if (n == 0 && (inflater.needsInput() || inflater.needsDictionary())) {
                    break;
                }
                out.write(buffer, 0, n);
            }
        } catch (DataFormatException e) {
            throw new IOException("inflate: " + e.getMessage());
        } finally {
            inflater.end();
        }
        return out.toByteArray();
    }

    /** The bodies of the movie's DefineFont3 tags, in order. */
    private static List<byte[]> definedFonts(byte[] movie) {
        List<byte[]> fonts = new ArrayList<>();
        int bits = (movie[8] & 0xFF) >> 3;
        int pos = 8 + (5 + 4 * bits + 7) / 8 + 4;
        ByteBuffer le = ByteBuffer.wrap(movie).order(ByteOrder.LITTLE_ENDIAN);
        while (pos + 2 <= movie.length) {
            int head = le.getShort(pos) & 0xFFFF;
            int code = head >> 6;
            int length = head & 0x3F;
            pos += 2;
            if (length == 0x3F) {
                length = le.getInt(pos);
                pos += 4;
            }
            if (code == 0) {
                break;
            }
            if (code == 75) {
                byte[] body = new byte[length];
                System.arraycopy(movie, pos, body, 0, length);
                fonts.add(body);
            }
            pos += length;
        }
        return fonts;
    }

    // ---- DefineFont3 ----

    private static final class Font {
        boolean bold;
        boolean italic;
        int ascent;
        int descent;
        // Each glyph's contours, each contour's points as {x, y, onCurve}.
        List<List<List<int[]>>> glyphs = new ArrayList<>();
        int[] codes;
        int[] advances;
    }

    private static final class Bits {
        final byte[] data;
        long pos;

        Bits(byte[] data, int start) {
            this.data = data;
            this.pos = start * 8L;
        }

        int ub(int n) {
            int v = 0;
            for (int i = 0; i < n; i++) {
                int b = data[(int) (pos >> 3)] & 0xFF;
                v = (v << 1) | ((b >> (7 - (int) (pos & 7))) & 1);
                pos++;
            }
            return v;
        }

        int sb(int n) {
            if (n == 0) {
                return 0;
            }
            int v = ub(n);
            if ((v & (1 << (n - 1))) != 0) {
                v -= 1 << n;
            }
            return v;
        }
    }

    private static List<List<int[]>> parseShape(byte[] data, int start) {
        Bits b = new Bits(data, start);
        int fillBits = b.ub(4);
        int lineBits = b.ub(4);
        List<List<int[]>> contours = new ArrayList<>();
        List<int[]> current = null;
        int x = 0;
        int y = 0;
        while (true) {
            if (b.ub(1) == 0) {
                int newStyles = b.ub(1);
                int lineStyle = b.ub(1);
                int fill1 = b.ub(1);
                int fill0 = b.ub(1);
                int move = b.ub(1);
                if ((newStyles | lineStyle | fill1 | fill0 | move) == 0) {
                    break;
                }
                if (move != 0) {
                    int n = b.ub(5);
                    x = b.sb(n);
                    y = b.sb(n);
                    current = new ArrayList<>();
                    current.add(new int[] {x, y, 1});
                    contours.add(current);
                }
                if (fill0 != 0) {
                    b.ub(fillBits);
                }
                if (fill1 != 0) {
                    b.ub(fillBits);
                }
                if (lineStyle != 0) {
                    b.ub(lineBits);
                }
                if (newStyles != 0) {
                    break;  // not in a font's glyphs
                }
            } else {
                int straight = b.ub(1);
                int n = b.ub(4) + 2;
                if (current == null) {
                    current = new ArrayList<>();
                    current.add(new int[] {x, y, 1});
                    contours.add(current);
                }
                if (straight != 0) {
                    if (b.ub(1) != 0) {
                        x += b.sb(n);
                        y += b.sb(n);
                    } else if (b.ub(1) != 0) {
                        y += b.sb(n);
                    } else {
                        x += b.sb(n);
                    }
                    current.add(new int[] {x, y, 1});
                } else {
                    int cx = x + b.sb(n);
                    int cy = y + b.sb(n);
                    x = cx + b.sb(n);
                    y = cy + b.sb(n);
                    current.add(new int[] {cx, cy, 0});
                    current.add(new int[] {x, y, 1});
                }
            }
        }
        List<List<int[]>> kept = new ArrayList<>();
        for (List<int[]> c : contours) {
            if (c.size() > 1) {
                int[] first = c.get(0);
                int[] last = c.get(c.size() - 1);
                if (first[0] == last[0] && first[1] == last[1]) {
                    c.remove(c.size() - 1);
                }
            }
            if (c.size() >= 2) {
                kept.add(c);
            }
        }
        return kept;
    }

    private static Font parseFont3(byte[] body) {
        ByteBuffer le = ByteBuffer.wrap(body).order(ByteOrder.LITTLE_ENDIAN);
        int flags = body[2] & 0xFF;
        int nameLength = body[4] & 0xFF;
        int pos = 5 + nameLength;
        int count = le.getShort(pos) & 0xFFFF;
        pos += 2;
        boolean wideOffsets = (flags & 0x08) != 0;
        int table = pos;
        int[] offsets = new int[count];
        int codeOffset;
        if (wideOffsets) {
            for (int i = 0; i < count; i++) {
                offsets[i] = le.getInt(pos + 4 * i);
            }
            codeOffset = le.getInt(pos + 4 * count);
        } else {
            for (int i = 0; i < count; i++) {
                offsets[i] = le.getShort(pos + 2 * i) & 0xFFFF;
            }
            codeOffset = le.getShort(pos + 2 * count) & 0xFFFF;
        }
        Font font = new Font();
        font.bold = (flags & 0x01) != 0;
        font.italic = (flags & 0x02) != 0;
        for (int i = 0; i < count; i++) {
            font.glyphs.add(parseShape(body, table + offsets[i]));
        }
        pos = table + codeOffset;
        font.codes = new int[count];
        for (int i = 0; i < count; i++) {
            font.codes[i] = le.getShort(pos + 2 * i) & 0xFFFF;
        }
        pos += 2 * count;
        font.advances = new int[count];
        if ((flags & 0x80) != 0) {
            font.ascent = le.getShort(pos) & 0xFFFF;
            font.descent = le.getShort(pos + 2) & 0xFFFF;
            pos += 6;
            for (int i = 0; i < count; i++) {
                font.advances[i] = le.getShort(pos + 2 * i);
            }
        }
        return font;
    }

    // ---- TrueType ----

    private static final int SCALE = 10;  // DefineFont3's EM of 20480 to TrueType's 2048

    private static byte[] toTrueType(Font font, String family) {
        int count = font.glyphs.size();
        ByteArrayOutputStream glyf = new ByteArrayOutputStream();
        int[] loca = new int[count + 2];
        int[] advance = new int[count + 1];
        int[] lsb = new int[count + 1];
        advance[0] = 1024;
        int maxPoints = 0;
        int maxContours = 0;
        int xMin = 0, yMin = 0, xMax = 0, yMax = 0;
        loca[0] = 0;
        loca[1] = 0;  // .notdef, empty
        for (int g = 0; g < count; g++) {
            List<List<int[]>> contours = font.glyphs.get(g);
            advance[g + 1] = Math.round(font.advances[g] / (float) SCALE);
            if (!contours.isEmpty()) {
                int points = 0;
                int gx0 = Integer.MAX_VALUE, gy0 = Integer.MAX_VALUE;
                int gx1 = Integer.MIN_VALUE, gy1 = Integer.MIN_VALUE;
                List<int[]> flat = new ArrayList<>();
                int[] ends = new int[contours.size()];
                for (int c = 0; c < contours.size(); c++) {
                    for (int[] p : contours.get(c)) {
                        int px = Math.round(p[0] / (float) SCALE);
                        int py = Math.round(-p[1] / (float) SCALE);
                        flat.add(new int[] {px, py, p[2]});
                        gx0 = Math.min(gx0, px);
                        gy0 = Math.min(gy0, py);
                        gx1 = Math.max(gx1, px);
                        gy1 = Math.max(gy1, py);
                    }
                    points += contours.get(c).size();
                    ends[c] = points - 1;
                }
                ByteBuffer out = ByteBuffer.allocate(10 + 2 * ends.length + 2 + flat.size() * 5)
                        .order(ByteOrder.BIG_ENDIAN);
                out.putShort((short) contours.size()).putShort((short) gx0).putShort((short) gy0)
                        .putShort((short) gx1).putShort((short) gy1);
                for (int end : ends) {
                    out.putShort((short) end);
                }
                out.putShort((short) 0);
                for (int[] p : flat) {
                    out.put((byte) (p[2] != 0 ? 1 : 0));
                }
                int last = 0;
                for (int[] p : flat) {
                    out.putShort((short) (p[0] - last));
                    last = p[0];
                }
                last = 0;
                for (int[] p : flat) {
                    out.putShort((short) (p[1] - last));
                    last = p[1];
                }
                byte[] data = out.array();
                glyf.write(data, 0, out.position());
                while (glyf.size() % 4 != 0) {
                    glyf.write(0);
                }
                lsb[g + 1] = gx0;
                maxPoints = Math.max(maxPoints, points);
                maxContours = Math.max(maxContours, contours.size());
                xMin = Math.min(xMin, gx0);
                yMin = Math.min(yMin, gy0);
                xMax = Math.max(xMax, gx1);
                yMax = Math.max(yMax, gy1);
            }
            loca[g + 2] = glyf.size();
        }
        int glyphs = count + 1;
        int ascent = font.ascent > 0 ? Math.round(font.ascent / (float) SCALE) : 1600;
        int descent = font.descent > 0 ? Math.round(font.descent / (float) SCALE) : 450;
        int maxAdvance = 0;
        for (int a : advance) {
            maxAdvance = Math.max(maxAdvance, a);
        }

        ByteBuffer head = be(54);
        head.putInt(0x00010000).putInt(0x00010000).putInt(0).putInt(0x5F0F3CF5)
                .putShort((short) 0x000B).putShort((short) 2048).putLong(0).putLong(0)
                .putShort((short) xMin).putShort((short) yMin).putShort((short) xMax)
                .putShort((short) yMax)
                .putShort((short) ((font.bold ? 1 : 0) | (font.italic ? 2 : 0)))
                .putShort((short) 8).putShort((short) 2).putShort((short) 1).putShort((short) 0);
        ByteBuffer hhea = be(36);
        hhea.putInt(0x00010000).putShort((short) ascent).putShort((short) -descent)
                .putShort((short) 0).putShort((short) maxAdvance).putShort((short) 0)
                .putShort((short) 0).putShort((short) xMax).putShort((short) 1)
                .putShort((short) 0).putShort((short) 0);
        for (int i = 0; i < 5; i++) {
            hhea.putShort((short) 0);
        }
        hhea.putShort((short) glyphs);
        ByteBuffer maxp = be(32);
        maxp.putInt(0x00010000).putShort((short) glyphs).putShort((short) maxPoints)
                .putShort((short) maxContours).putShort((short) 0).putShort((short) 0)
                .putShort((short) 2);
        for (int i = 0; i < 8; i++) {
            maxp.putShort((short) 0);
        }
        ByteBuffer hmtx = be(4 * glyphs);
        for (int i = 0; i < glyphs; i++) {
            hmtx.putShort((short) advance[i]).putShort((short) lsb[i]);
        }
        ByteBuffer locaTable = be(4 * loca.length);
        for (int offset : loca) {
            locaTable.putInt(offset);
        }

        // cmap: format 4, a segment for each code, sorted; glyph = code + delta.
        List<int[]> pairs = new ArrayList<>();
        int firstCode = 0xFFFF;
        int lastCode = 0;
        for (int i = 0; i < count; i++) {
            int code = font.codes[i];
            if (code < 0xFFFF) {
                pairs.add(new int[] {code, i + 1});
                firstCode = Math.min(firstCode, code);
                lastCode = Math.max(lastCode, code);
            }
        }
        pairs.sort((a, b) -> Integer.compare(a[0], b[0]));
        // A code given twice: its first glyph (the segments must rise strictly).
        for (int i = pairs.size() - 1; i > 0; i--) {
            if (pairs.get(i)[0] == pairs.get(i - 1)[0]) {
                pairs.remove(i);
            }
        }
        int segments = pairs.size() + 1;
        int searchRange = 2 * Integer.highestOneBit(segments);
        int subLength = 16 + 8 * segments;
        ByteBuffer cmap = be(12 + subLength);
        cmap.putShort((short) 0).putShort((short) 1).putShort((short) 3).putShort((short) 1)
                .putInt(12);
        cmap.putShort((short) 4).putShort((short) subLength).putShort((short) 0)
                .putShort((short) (segments * 2)).putShort((short) searchRange)
                .putShort((short) (Integer.numberOfTrailingZeros(searchRange / 2)))
                .putShort((short) (segments * 2 - searchRange));
        for (int[] p : pairs) {
            cmap.putShort((short) p[0]);
        }
        cmap.putShort((short) 0xFFFF).putShort((short) 0);
        for (int[] p : pairs) {
            cmap.putShort((short) p[0]);
        }
        cmap.putShort((short) 0xFFFF);
        for (int[] p : pairs) {
            cmap.putShort((short) ((p[1] - p[0]) & 0xFFFF));
        }
        cmap.putShort((short) 1);
        for (int i = 0; i < segments; i++) {
            cmap.putShort((short) 0);
        }

        String[] nameTexts = {family, font.bold ? "Bold" : "Regular", family,
            family.replace(" ", "")};
        int[] nameIds = {1, 2, 4, 6};
        ByteArrayOutputStream strings = new ByteArrayOutputStream();
        ByteBuffer records = be(12 * nameIds.length);
        for (int i = 0; i < nameIds.length; i++) {
            byte[] encoded = nameTexts[i].getBytes(StandardCharsets.UTF_16BE);
            records.putShort((short) 3).putShort((short) 1).putShort((short) 0x409)
                    .putShort((short) nameIds[i]).putShort((short) encoded.length)
                    .putShort((short) strings.size());
            strings.write(encoded, 0, encoded.length);
        }
        ByteBuffer name = be(6 + 12 * nameIds.length + strings.size());
        name.putShort((short) 0).putShort((short) nameIds.length)
                .putShort((short) (6 + 12 * nameIds.length)).put(records.array())
                .put(strings.toByteArray());
        ByteBuffer post = be(32);
        post.putInt(0x00030000).putInt(0).putShort((short) -100).putShort((short) 50);
        ByteBuffer os2 = be(96);
        os2.putShort((short) 4).putShort((short) 1000)
                .putShort((short) (font.bold ? 700 : 400)).putShort((short) 5).putShort((short) 0)
                .putShort((short) 650).putShort((short) 700).putShort((short) 0)
                .putShort((short) 140).putShort((short) 650).putShort((short) 700)
                .putShort((short) 0).putShort((short) 480).putShort((short) 50)
                .putShort((short) 250).putShort((short) 0);
        os2.put(new byte[10]).put(new byte[16]).put("NONE".getBytes(StandardCharsets.US_ASCII));
        os2.putShort((short) (font.bold ? 0x20 : 0x40)).putShort((short) firstCode)
                .putShort((short) lastCode).putShort((short) ascent).putShort((short) -descent)
                .putShort((short) 0).putShort((short) ascent).putShort((short) descent)
                .putInt(1).putInt(0).putShort((short) 500).putShort((short) 700)
                .putShort((short) 0).putShort((short) 32).putShort((short) 0);

        String[] tags = {"OS/2", "cmap", "glyf", "head", "hhea", "hmtx", "loca", "maxp",
            "name", "post"};
        byte[][] tables = {os2.array(), cmap.array(), glyf.toByteArray(), head.array(),
            hhea.array(), hmtx.array(), locaTable.array(), maxp.array(), name.array(),
            post.array()};
        int n = tags.length;
        int search = 16 * Integer.highestOneBit(n);
        int total = 12 + 16 * n;
        for (byte[] t : tables) {
            total += (t.length + 3) & ~3;
        }
        ByteBuffer font3 = be(total);
        font3.putInt(0x00010000).putShort((short) n).putShort((short) search)
                .putShort((short) Integer.numberOfTrailingZeros(search / 16))
                .putShort((short) (n * 16 - search));
        int offset = 12 + 16 * n;
        int headAt = 0;
        for (int i = 0; i < n; i++) {
            font3.put(tags[i].getBytes(StandardCharsets.US_ASCII)).putInt(checksum(tables[i]))
                    .putInt(offset).putInt(tables[i].length);
            if (tags[i].equals("head")) {
                headAt = offset;
            }
            offset += (tables[i].length + 3) & ~3;
        }
        for (byte[] t : tables) {
            font3.put(t);
            for (int pad = t.length; pad % 4 != 0; pad++) {
                font3.put((byte) 0);
            }
        }
        byte[] bytes = font3.array();
        int adjust = (int) (0xB1B0AFBAL - (checksum(bytes) & 0xFFFFFFFFL));
        ByteBuffer.wrap(bytes).order(ByteOrder.BIG_ENDIAN).putInt(headAt + 8, adjust);
        return bytes;
    }

    private static ByteBuffer be(int size) {
        return ByteBuffer.allocate(size).order(ByteOrder.BIG_ENDIAN);
    }

    private static int checksum(byte[] data) {
        long sum = 0;
        int padded = (data.length + 3) & ~3;
        for (int i = 0; i < padded; i += 4) {
            long word = 0;
            for (int j = 0; j < 4; j++) {
                word = (word << 8) | (i + j < data.length ? data[i + j] & 0xFF : 0);
            }
            sum = (sum + word) & 0xFFFFFFFFL;
        }
        return (int) sum;
    }

    private static void write(File file, byte[] data) throws IOException {
        File partial = new File(file.getPath() + ".part");
        try (FileOutputStream out = new FileOutputStream(partial)) {
            out.write(data);
        }
        if (!partial.renameTo(file)) {
            throw new IOException("cannot rename " + partial);
        }
    }
}
