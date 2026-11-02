package dev.daorecompandroid.duo;

import android.app.Activity;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Finding the disc image and the title update, and extracting the image (tasks 4.3 and
 * 4.4).
 *
 * <p><b>The user picks the files, not a folder.</b> Scoped storage means there is no
 * readable {@code /sdcard} for an app to go looking in, so access comes through the
 * Storage Access Framework. This used to ask for a folder, so that the image and the
 * title update beside it came with one gesture. But Android 11 and later refuse a folder
 * grant on the root of {@code Download} - "To protect your privacy, choose another
 * folder" - and {@code Download} is exactly where a handheld's files end up. Picking
 * documents has no such restriction, so the picker opens on {@code Download} and takes
 * several files at once: the disc image and the title update together.
 *
 * <p>Each picked file is identified by its contents, never by its name:
 * <ul>
 *   <li>a <b>disc image</b> by its XEX2 execution header ({@link #rankUri}). It is not
 *       copied - it is 7.3 GiB - so its read grant is persisted instead, and an
 *       interrupted extraction resumes on the next launch without asking again.</li>
 *   <li>a <b>title update</b> by its STFS header ({@link #isTitleUpdate}). It is copied
 *       into app storage: it is 3.5 MB, native code already looks for it there
 *       ({@code FindTitleUpdate} in {@code warden_boot.cc}), and a copy needs no grant
 *       that could later be revoked.</li>
 * </ul>
 *
 * <p><b>The file descriptor is the trick.</b> SAF is slow if every read goes through a
 * {@code ContentResolver}, which is the usual reason people warn against reading large
 * files through it. But {@code openFileDescriptor} hands back a real, seekable fd, and
 * once native code has that it uses {@code pread} like any other file. The framework is
 * out of the way after the first call.
 */
final class GameData {

    private static final String TAG = "Warden";
    private static final String PREFS = "warden";
    private static final String KEY_IMAGE = "disc_image_uri";

    /** Dragon Age: Origins, as the STFS header and the XEX2 header both record it. */
    private static final int TITLE_ID = 0x454108C0;
    /** STFS content type for a title update. */
    private static final int CONTENT_TITLE_UPDATE = 0x000B0000;
    private static final int STFS_CONTENT_TYPE = 0x344;
    private static final int STFS_TITLE_ID = 0x360;
    private static final int STFS_HEADER = STFS_TITLE_ID + 4;

    /** Where the extraction lands: app-scoped, on the big volume, needs no permission. */
    static File destination(Context context) {
        return new File(context.getExternalFilesDir(null), "gamedata");
    }

    /**
     * How good a candidate a disc image is, from its own XEX2 execution header.
     *
     * <p>Not from its filename, and not from its size. The handheld's Download folder
     * holds three disc images, two of them other games, and both Dragon Age discs are
     * byte-for-byte the same length - so "the largest .iso" picked correctly here by
     * luck, and "the one called Dragon Age" would depend on how the dump was named.
     * The image says what it is: title {@code 0x454108C0} for Dragon Age, media
     * {@code 0x186A1115} for Origins and {@code 0x2E6F7560} for Awakening.
     *
     * <p>The ranking itself lives in native code ({@code warden_title.h}), so those
     * ids are written down once.
     *
     * @return 2 for Origins, 1 for another Dragon Age disc, 0 for anything else
     */
    private static int rankFile(File image) {
        try {
            ParcelFileDescriptor descriptor = ParcelFileDescriptor.open(
                    image, ParcelFileDescriptor.MODE_READ_ONLY);
            // Native closes the descriptor - see NativeBridge.imageIdentity.
            int[] identity = NativeBridge.imageIdentity(descriptor.detachFd());
            return identity != null && identity.length >= 4 ? identity[3] : 0;
        } catch (Exception e) {
            Log.w(TAG, "cannot identify " + image.getName() + ": " + e.getMessage());
            return 0;
        }
    }

    private static int rankUri(Context context, Uri uri) {
        try (ParcelFileDescriptor descriptor =
                     context.getContentResolver().openFileDescriptor(uri, "r")) {
            if (descriptor == null) {
                return 0;
            }
            int[] identity = NativeBridge.imageIdentity(descriptor.detachFd());
            return identity != null && identity.length >= 4 ? identity[3] : 0;
        } catch (Exception e) {
            Log.w(TAG, "cannot identify " + uri + ": " + e.getMessage());
            return 0;
        }
    }

    /**
     * Looks for a disc image the app can already open without any grant.
     *
     * <p>{@code getExternalFilesDir} is app-owned: readable and writable with no
     * permission, and reachable from a desktop over adb or MTP. So a user who put the
     * image there - or a test that pushed it there - needs no Storage Access Framework
     * dialog at all, and the app can get on with extracting.
     *
     * <p>Checked before the remembered picked image, because a plain descriptor is
     * strictly better than a content URI when both are available: no grant to lose,
     * and nothing to re-ask for after a reboot.
     *
     * @return the best Dragon Age image found, or null
     */
    static File directImage(Context context) {
        File[] roots = {context.getExternalFilesDir(null), context.getFilesDir()};
        File best = null;
        int bestRank = 0;
        for (File root : roots) {
            if (root == null) {
                continue;
            }
            File[] entries = root.listFiles();
            if (entries == null) {
                continue;
            }
            for (File entry : entries) {
                if (!entry.isFile() || !entry.getName().toLowerCase().endsWith(".iso")) {
                    continue;
                }
                int rank = rankFile(entry);
                if (rank > bestRank) {
                    bestRank = rank;
                    best = entry;
                }
            }
        }
        return best;
    }

    /** Starts extraction from a plain file, with no content provider involved. */
    static boolean startExtraction(Context context, File image) {
        try {
            ParcelFileDescriptor descriptor = ParcelFileDescriptor.open(
                    image, ParcelFileDescriptor.MODE_READ_ONLY);
            int fd = descriptor.detachFd();
            return NativeBridge.extractBegin(
                    fd, destination(context).getAbsolutePath());
        } catch (Exception e) {
            Log.e(TAG, "cannot open " + image, e);
            return false;
        }
    }

    /**
     * The document picker, opening on {@code Download} and accepting several files.
     *
     * <p>{@code *}{@code /*} because neither file has a type the system knows: an
     * {@code .iso} is at best {@code application/octet-stream}, and the title update has
     * no extension at all. Several at once because the image and the title update are
     * both needed, and one trip through the picker is better than two - a long press
     * starts a multiple selection.
     */
    static Intent pickFilesIntent() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        // A hint, not a requirement: a picker that does not know the URI opens wherever
        // it was last, which is no worse.
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                DocumentsContract.buildDocumentUri(
                        "com.android.externalstorage.documents", "primary:Download"));
        return intent;
    }

    /** The picker's result: one URI in {@code getData}, several in {@code getClipData}. */
    static List<Uri> pickedUris(Intent data) {
        List<Uri> uris = new ArrayList<>();
        ClipData clip = data.getClipData();
        if (clip != null) {
            for (int i = 0; i < clip.getItemCount(); ++i) {
                Uri uri = clip.getItemAt(i).getUri();
                if (uri != null) {
                    uris.add(uri);
                }
            }
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        return uris;
    }

    static final class Picked {
        Uri imageUri;
        String imageName;
        long imageSize;
        /** 2 Origins, 1 another Dragon Age disc, 0 none picked. */
        int imageRank;
        /** Title updates copied into app storage this time. */
        final List<String> titleUpdates = new ArrayList<>();
        /** One verdict per picked file, for the report - so a wrong pick is obvious. */
        final List<String> verdicts = new ArrayList<>();
    }

    /**
     * Works out what each picked file is, copies any title update into app storage, and
     * keeps the best disc image.
     *
     * <p>Every file is opened and read rather than judged by its name: one 64 KiB read
     * for a disc image and under 1 KiB for a package, which is nothing against building
     * from the wrong file.
     */
    static Picked identify(Context context, List<Uri> uris) {
        Picked picked = new Picked();
        ContentResolver resolver = context.getContentResolver();
        for (Uri uri : uris) {
            String name = uri.getLastPathSegment();
            long size = -1;
            try (Cursor cursor = resolver.query(uri, new String[] {
                    OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE}, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    if (!cursor.isNull(0)) {
                        name = cursor.getString(0);
                    }
                    if (!cursor.isNull(1)) {
                        size = cursor.getLong(1);
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "cannot query " + uri + ": " + e.getMessage());
            }
            String label = name + "  " + size + " bytes";

            byte[] header;
            try (InputStream in = resolver.openInputStream(uri)) {
                header = in == null ? new byte[0] : head(in, STFS_HEADER);
            } catch (Exception e) {
                picked.verdicts.add(label + "\n    cannot be read: " + e.getMessage());
                continue;
            }

            if (isTitleUpdate(header)) {
                File copy = new File(context.getExternalFilesDir(null), safeName(name));
                if (copyIntoAppStorage(context, uri, copy, size)) {
                    picked.titleUpdates.add(copy.getName());
                    picked.verdicts.add(label + "\n    title update - copied to app storage");
                } else {
                    picked.verdicts.add(label + "\n    title update - copy FAILED, see logcat");
                }
                continue;
            }
            if (isPackage(header)) {
                picked.verdicts.add(label
                        + "\n    an Xbox 360 package, but not a Dragon Age title update");
                continue;
            }

            int rank = rankUri(context, uri);
            if (rank == 2) {
                picked.verdicts.add(label + "\n    disc image - Dragon Age: Origins");
            } else if (rank == 1) {
                picked.verdicts.add(label + "\n    disc image - Dragon Age, but not the"
                        + " Origins disc this port is built for (media 186A1115)");
            } else {
                picked.verdicts.add(label
                        + "\n    not a Dragon Age disc image or title update");
            }
            if (rank > picked.imageRank) {
                picked.imageRank = rank;
                picked.imageUri = uri;
                picked.imageName = name;
                picked.imageSize = size;
            }
        }
        return picked;
    }

    /**
     * Whether a title update is already in one of the places native code looks for one.
     *
     * <p>The same three directories {@code Runtime::LoadTitle} searches, and the same
     * two forms: a loose {@code default.xexp}, or a package. Only a package's header is
     * read here; whether it is the right title update - TU6 or TU7 rather than TU2 - is
     * native's call, made from the {@code default.xexp} inside it, and its report says
     * so if it is not.
     */
    static boolean hasTitleUpdate(Context context) {
        return titleUpdateName(context) != null;
    }

    /**
     * The title update in place, by its file's name ({@link #hasTitleUpdate}'s search), or
     * null - for the pre-launch settings' Title update row.
     */
    static String titleUpdateName(Context context) {
        TitleUpdate update = titleUpdate(context);
        return update == null ? null : update.file;
    }

    /** A title update in place: its file and its number (7, 6; 0 when unrecognised). */
    static final class TitleUpdate {
        final File path;
        final String file;
        final int number;

        TitleUpdate(File path, String file, int number) {
            this.path = path;
            this.file = file;
            this.number = number;
        }

        /** PLAN 12.3: disc 2's, copied by the disc 2 row (installDlc). */
        boolean fromDisc2(Context context) {
            return path.equals(disc2TitleUpdate(context));
        }
    }

    /** PLAN 12.3: where the disc 2 row copies disc 2's Title Update 6 (native installDlc). */
    static File disc2TitleUpdate(Context context) {
        return new File(context.getExternalFilesDir(null), "title_update_disc2.bin");
    }

    /**
     * The title update in place ({@link #hasTitleUpdate}'s search): the newest of those
     * there - a chosen Title Update 7 before disc 2's 6 (PLAN 12.3) - or null.
     */
    static TitleUpdate titleUpdate(Context context) {
        File[] dirs = {destination(context), context.getExternalFilesDir(null),
                context.getFilesDir()};
        TitleUpdate best = null;
        for (File dir : dirs) {
            if (dir == null) {
                continue;
            }
            for (String loose : new String[] {"default.xexp", "title_update/default.xexp"}) {
                File file = new File(dir, loose);
                if (file.isFile()) {
                    int number = file.length() == TU7_XEXP_SIZE ? 7
                            : file.length() == TU6_XEXP_SIZE ? 6 : 0;
                    if (best == null || number > best.number) {
                        best = new TitleUpdate(file, loose, number);
                    }
                }
            }
            File[] entries = dir.listFiles();
            if (entries == null) {
                continue;
            }
            for (File entry : entries) {
                String entryName = entry.getName();
                if (!entry.isFile() || entryName.startsWith(".")
                        || entryName.endsWith(".part")) {
                    continue;
                }
                try (InputStream in = new FileInputStream(entry)) {
                    byte[] header = head(in, STFS_NAME_END);
                    if (isTitleUpdate(header)) {
                        int number = titleUpdateNumber(header);
                        if (best == null || number > best.number) {
                            best = new TitleUpdate(entry, entryName, number);
                        }
                    }
                } catch (IOException e) {
                    // Unreadable is not a title update; keep looking.
                }
            }
        }
        return best;
    }

    // The title updates' executable patches, by size (warden_boot.cc has the same).
    private static final long TU7_XEXP_SIZE = 2758656;
    private static final long TU6_XEXP_SIZE = 2684928;
    // An STFS package's display name, UTF-16BE: "Dragon Age: Origins Title Update #6".
    private static final int STFS_DISPLAY_NAME = 0x411;
    private static final int STFS_NAME_END = STFS_DISPLAY_NAME + 0x80;

    /** The number after "#" in a title update package's display name, or 0. */
    static int titleUpdateNumber(byte[] header) {
        StringBuilder name = new StringBuilder();
        for (int i = STFS_DISPLAY_NAME; i + 1 < header.length && i < STFS_NAME_END; i += 2) {
            char c = (char) (((header[i] & 0xFF) << 8) | (header[i + 1] & 0xFF));
            if (c == 0) {
                break;
            }
            name.append(c);
        }
        int hash = name.lastIndexOf("#");
        if (hash < 0) {
            return 0;
        }
        int number = 0;
        for (int i = hash + 1; i < name.length() && Character.isDigit(name.charAt(i)); i++) {
            number = number * 10 + (name.charAt(i) - '0');
        }
        return number;
    }

    /** The owner's Lossless.dll, where frame generation reads it: the app's files folder. */
    static File losslessFile(Context context) {
        return new File(context.getFilesDir(), "Lossless.dll");
    }

    static boolean hasLossless(Context context) {
        File file = losslessFile(context);
        return file.isFile() && file.length() > 0;
    }

    /**
     * The picked file as Lossless.dll, if it is a Windows DLL - "MZ", then "PE\0\0" where
     * the header's e_lfanew says - copied into the files folder (as .part, then renamed).
     * Whether it is Lossless Scaling's own, frame generation finds out as it starts (its
     * shaders read from the DLL's resources; the log says so if not).
     *
     * @return what happened, for the pre-launch row
     */
    static String takeLossless(Context context, Uri uri) {
        String name = uri.getLastPathSegment();
        try (Cursor cursor = context.getContentResolver().query(uri,
                new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) {
                name = cursor.getString(0);
            }
        } catch (Exception e) {
            Log.w(TAG, "cannot query " + uri + ": " + e.getMessage());
        }
        byte[] header;
        try (InputStream in = context.getContentResolver().openInputStream(uri)) {
            header = in == null ? new byte[0] : head(in, 4096);
        } catch (Exception e) {
            return name + " cannot be read: " + e.getMessage();
        }
        boolean dll = header.length >= 64 && header[0] == 'M' && header[1] == 'Z';
        if (dll) {
            int peAt = (header[60] & 0xFF) | ((header[61] & 0xFF) << 8)
                    | ((header[62] & 0xFF) << 16) | ((header[63] & 0xFF) << 24);
            dll = peAt > 0 && peAt + 4 <= header.length && header[peAt] == 'P'
                    && header[peAt + 1] == 'E' && header[peAt + 2] == 0 && header[peAt + 3] == 0;
        }
        if (!dll) {
            return name + " is not a Windows DLL - choose Lossless.dll from Lossless Scaling.";
        }
        File target = losslessFile(context);
        File partial = new File(target.getPath() + ".part");
        boolean copied = false;
        try (InputStream in = context.getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(partial)) {
            if (in != null) {
                byte[] buffer = new byte[1 << 16];
                int read;
                while ((read = in.read(buffer)) > 0) {
                    out.write(buffer, 0, read);
                }
                copied = true;
            }
        } catch (IOException e) {
            Log.e(TAG, "cannot copy " + uri + " to " + target, e);
        }
        if (!copied || !partial.renameTo(target)) {
            partial.delete();
            return name + " could not be copied - see logcat.";
        }
        Log.i(TAG, "Lossless.dll: " + name + ", " + target.length() + " bytes, in place");
        return name + " in place (" + target.length() / 1024 + " KB).";
    }

    /**
     * Keeps read access to the picked disc image across reboots.
     *
     * <p>{@code takePersistableUriPermission} is the whole point: without it the grant
     * dies with the process, and an extraction interrupted by a restart could not
     * resume without asking for the file again.
     */
    static void rememberImage(Context context, Uri image) {
        context.getContentResolver().takePersistableUriPermission(
                image, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit().putString(KEY_IMAGE, image.toString()).apply();
        Log.i(TAG, "disc image: " + image);
    }

    /** The remembered disc image, or null if there is none or the grant is gone. */
    static Uri rememberedImage(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String stored = prefs.getString(KEY_IMAGE, null);
        if (stored == null) {
            return null;
        }
        Uri uri = Uri.parse(stored);
        // A persisted grant can still be revoked - the user can clear it in settings,
        // or the file can be on a volume that is no longer mounted. Check rather than
        // trusting what was written.
        for (android.content.UriPermission held : context.getContentResolver()
                .getPersistedUriPermissions()) {
            if (held.getUri().equals(uri) && held.isReadPermission()) {
                return uri;
            }
        }
        Log.w(TAG, "the grant for " + uri + " is gone; asking again");
        prefs.edit().remove(KEY_IMAGE).apply();
        return null;
    }

    /**
     * Hands the image to the native extractor.
     *
     * <p>{@code detachFd} rather than passing the {@link ParcelFileDescriptor}: native
     * owns the descriptor from here and closes it when the extraction ends. Keeping the
     * Java wrapper alive alongside would mean two owners of one fd and a close from
     * whichever side ran its finalizer first.
     */
    static boolean startExtraction(Context context, Uri isoUri) {
        File target = destination(context);
        try {
            ParcelFileDescriptor descriptor =
                    context.getContentResolver().openFileDescriptor(isoUri, "r");
            if (descriptor == null) {
                Log.e(TAG, "no descriptor for " + isoUri);
                return false;
            }
            int fd = descriptor.detachFd();
            boolean started = NativeBridge.extractBegin(fd, target.getAbsolutePath());
            if (!started) {
                Log.e(TAG, "native extractor refused to start");
            }
            return started;
        } catch (Exception e) {
            Log.e(TAG, "cannot open " + isoUri, e);
            return false;
        }
    }

    // ---- PLAN 10.10: Awakening and the DLC, from the Ultimate Edition's disc 2 ----

    /**
     * Disc 2's own executable: the content installer (title {@code FFED2000}, media
     * {@code 6FB2B193}). Its packages - Awakening, Leliana's Song, The Golems of Amgarrak,
     * Witch Hunt, The Darkspawn Chronicles, Return to Ostagar, Warden's Keep, The Stone
     * Prisoner, Blood Dragon Armor, Feastday Gifts and Pranks - are Live packages for this
     * game, installed as downloadable content ({@code warden_dlc.cc}).
     */
    static final int ULTIMATE_TITLE = 0xFFED2000;
    static final int ULTIMATE_MEDIA = 0x6FB2B193;

    /**
     * The folder Awakening had as a package of its own when it came from the standalone
     * Awakening disc (session 38, runs 487-492): disc 2's Awakening package carries the same
     * add-in, so that one is taken away once disc 2's is in.
     */
    static final String STANDALONE_AWAKENING = "DAO_PRC_EP_1";

    /** Whether a picked file is the Ultimate Edition's disc 2, by its own executable. */
    static boolean isUltimateDisc2(Context context, Uri uri) {
        try (ParcelFileDescriptor descriptor =
                     context.getContentResolver().openFileDescriptor(uri, "r")) {
            if (descriptor == null) {
                return false;
            }
            int[] identity = NativeBridge.imageIdentity(descriptor.detachFd());
            return identity != null && identity.length >= 4 && identity[0] == 1
                    && identity[1] == ULTIMATE_TITLE && identity[2] == ULTIMATE_MEDIA;
        } catch (Exception e) {
            Log.w(TAG, "cannot identify " + uri + ": " + e.getMessage());
            return false;
        }
    }

    /** Disc 2's Awakening package, by its content ID (the header's SHA-1). */
    static final String AWAKENING_PACKAGE = "E92188F50C483B8A709579B2A65681AE08A92FE6";

    /** The standalone disc's Awakening package taken away, once disc 2's is in. */
    static void removeStandaloneAwakening(Context context) {
        File folder = new File(dlcFolder(context), STANDALONE_AWAKENING);
        if (folder.exists() && new File(dlcFolder(context), AWAKENING_PACKAGE).isDirectory()) {
            deleteTree(folder);
            new File(dlcFolder(context), STANDALONE_AWAKENING + ".name").delete();
            Log.i(TAG, "the standalone disc's Awakening package removed (disc 2's is in)");
        }
    }

    /** The runtime's folder for downloadable content (content type 2). */
    static File dlcFolder(Context context) {
        return new File(context.getFilesDir(), "content/00000002");
    }

    /** PLAN 10.10: a picked package, or a disc image holding packages, installed. */
    static String installDlc(Context context, Uri uri) {
        try {
            ParcelFileDescriptor descriptor =
                    context.getContentResolver().openFileDescriptor(uri, "r");
            if (descriptor == null) {
                return "A file could not be opened.";
            }
            return NativeBridge.installDlc(descriptor.detachFd(),
                    dlcFolder(context).getAbsolutePath(),
                    disc2TitleUpdate(context).getAbsolutePath());
        } catch (Exception e) {
            Log.e(TAG, "cannot open " + uri, e);
            return "A file could not be opened.";
        }
    }

    /**
     * The downloadable content packages installed (Awakening among them) - not the add-ins
     * installed from mods (mod-&lt;id&gt;, PLAN 10.19), which the Mods page lists.
     */
    static int dlcCount(Context context) {
        File[] entries = dlcFolder(context).listFiles();
        int count = 0;
        if (entries != null) {
            for (File entry : entries) {
                if (entry.isDirectory() && !entry.getName().startsWith(".")
                        && !entry.getName().startsWith("mod-")) {
                    ++count;
                }
            }
        }
        return count;
    }

    private static boolean deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                if (!deleteTree(child)) {
                    return false;
                }
            }
        }
        return file.delete();
    }


    /** The first {@code length} bytes, or fewer if the stream is shorter. */
    private static byte[] head(InputStream in, int length) throws IOException {
        byte[] buffer = new byte[length];
        int got = 0;
        while (got < length) {
            int read = in.read(buffer, got, length - got);
            if (read < 0) {
                break;
            }
            got += read;
        }
        return got == length ? buffer : Arrays.copyOf(buffer, got);
    }

    private static boolean isPackage(byte[] header) {
        if (header.length < 4) {
            return false;
        }
        String magic = new String(header, 0, 4, StandardCharsets.US_ASCII);
        return magic.equals("LIVE") || magic.equals("PIRS") || magic.equals("CON ");
    }

    /**
     * An STFS package whose header says it is a title update for Dragon Age: Origins.
     * The same fields {@code tools/verify_assets.py} reads, big-endian like the rest of
     * the header.
     */
    static boolean isTitleUpdate(byte[] header) {
        return header.length >= STFS_HEADER && isPackage(header)
                && bigEndian32(header, STFS_CONTENT_TYPE) == CONTENT_TITLE_UPDATE
                && bigEndian32(header, STFS_TITLE_ID) == TITLE_ID;
    }

    private static int bigEndian32(byte[] bytes, int offset) {
        return ((bytes[offset] & 0xFF) << 24) | ((bytes[offset + 1] & 0xFF) << 16)
                | ((bytes[offset + 2] & 0xFF) << 8) | (bytes[offset + 3] & 0xFF);
    }

    /** A display name is the provider's to choose; keep it to one path component. */
    private static String safeName(String name) {
        if (name == null || name.isEmpty()) {
            return "title_update.stfs";
        }
        return name.replace('/', '_').replace('\\', '_');
    }

    /**
     * Copies a picked file into app storage, written as {@code .part} and renamed on
     * completion - the same scheme as the extractor, so a file at the real name is
     * always a whole one. Skipped if a file of the same name and size is already there.
     */
    private static boolean copyIntoAppStorage(Context context, Uri source, File target,
                                              long size) {
        if (size >= 0 && target.isFile() && target.length() == size) {
            return true;
        }
        File partial = new File(target.getPath() + ".part");
        boolean copied = false;
        try (InputStream in = context.getContentResolver().openInputStream(source);
             OutputStream out = new FileOutputStream(partial)) {
            if (in != null) {
                byte[] buffer = new byte[1 << 16];
                int read;
                while ((read = in.read(buffer)) > 0) {
                    out.write(buffer, 0, read);
                }
                copied = true;
            }
        } catch (IOException e) {
            Log.e(TAG, "cannot copy " + source + " to " + target, e);
        }
        if (!copied) {
            partial.delete();
            return false;
        }
        if (!partial.renameTo(target)) {
            Log.e(TAG, "cannot rename " + partial + " to " + target);
            partial.delete();
            return false;
        }
        Log.i(TAG, "title update copied to " + target);
        return true;
    }
}
