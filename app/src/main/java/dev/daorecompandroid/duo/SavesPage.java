package dev.daorecompandroid.duo;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Intent;
import android.net.Uri;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * PLAN 11.2, the launcher's Saves page: the game's saves exported to Downloads and
 * imported through Android's file picker - no root, no adb - with PLAN 10.15's restore of
 * the saves the game wrote over. Before the game only (its saves are not in use then).
 *
 * <p>A save is a folder in files/content/00000001/ - `0x<character>_<slot>_<u or a>!<n>`,
 * its files in Saves/ (savegame.das, the game's save: a GFF file headed "GFF V4.0X360SAVE";
 * savegame.met, its metadata; scrnshot.bin) - and beside it `<folder>.name`, the display
 * name the game lists (UTF-16, big-endian). An export is a .zip of such folders and their
 * names, as they are; an import takes any .zip holding them (or a save's files alone),
 * checks each is this game's (the das's header), and puts it in: a free slot when its own
 * is taken, unless the player chooses to replace that save - which is then kept in
 * .backup/ as the game's own overwrites are (Restore an earlier save puts it back).
 */
final class SavesPage {

    static final int REQUEST_SAVE_ZIP = 43;

    private static final String TAG = "Warden";
    private static final String FOLDER = Environment.DIRECTORY_DOWNLOADS + "/Dragon Age Origins";
    private static final Pattern SLOT = Pattern.compile("^(0x[0-9a-fA-F]+)_(\\d+)_([ua])!(\\d+)$");
    private static final byte[] SAVE_MAGIC = "GFF V4.0X360SAVE".getBytes(StandardCharsets.US_ASCII);
    private static final long MAX_IMPORT = 2L << 30;

    private final Activity activity;
    private final File filesDir;
    private final Runnable done;
    private final Handler ui = new Handler(Looper.getMainLooper());

    SavesPage(Activity activity, File filesDir, Runnable done) {
        this.activity = activity;
        this.filesDir = filesDir;
        this.done = done;
    }

    private AlertDialog.Builder builder() {
        return new AlertDialog.Builder(activity, android.R.style.Theme_DeviceDefault_Dialog_Alert);
    }

    private File savesDir() {
        return new File(filesDir, "content/00000001");
    }

    /** The game's saves on the device (folders with a save's metadata), newest first. */
    private List<File> saves() {
        List<File> saves = new ArrayList<>();
        File[] folders = savesDir().listFiles();
        if (folders != null) {
            for (File folder : folders) {
                if (folder.isDirectory() && !folder.getName().startsWith(".")
                        && new File(folder, "Saves/savegame.met").isFile()) {
                    saves.add(folder);
                }
            }
        }
        saves.sort((a, b) -> Long.compare(time(b), time(a)));
        return saves;
    }

    private static long time(File save) {
        return new File(save, "Saves/savegame.das").lastModified();
    }

    private static String label(File save) {
        return SaveRestore.label(new File(save, "Saves/savegame.met"), time(save));
    }

    /** The page. */
    void show() {
        String[] items = {"Export all saves", "Export one save", "Import saves from a .zip",
            "Restore an earlier save"};
        builder().setTitle("Saves")
                .setItems(items, (d, which) -> {
                    switch (which) {
                        case 0:
                            exportAll();
                            break;
                        case 1:
                            chooseOne();
                            break;
                        case 2:
                            pick();
                            break;
                        default:
                            SaveRestore.show(activity, filesDir, this::show);
                            break;
                    }
                })
                .setNegativeButton("Close", (d, w) -> done.run())
                .setOnCancelListener(d -> done.run())
                .show();
    }

    // ---- export ----

    private void exportAll() {
        List<File> saves = saves();
        if (saves.isEmpty()) {
            message("There are no saves yet.", this::show);
            return;
        }
        String name = "Dragon Age saves - "
                + new SimpleDateFormat("yyyy-MM-dd HHmm", Locale.ROOT).format(new Date()) + ".zip";
        export(saves, name);
    }

    private void chooseOne() {
        List<File> saves = saves();
        if (saves.isEmpty()) {
            message("There are no saves yet.", this::show);
            return;
        }
        String[] labels = new String[saves.size()];
        for (int i = 0; i < labels.length; i++) {
            labels[i] = label(saves.get(i));
        }
        builder().setTitle("Export which save?")
                .setItems(labels, (d, which) -> {
                    File save = saves.get(which);
                    String name = "Dragon Age save - " + safe(label(save)) + ".zip";
                    export(List.of(save), name);
                })
                .setNegativeButton("Back", (d, w) -> show())
                .setOnCancelListener(d -> show())
                .show();
    }

    private static String safe(String text) {
        return text.replaceAll("[\\\\/:*?\"<>|]", "").replaceAll("\\s+", " ").trim();
    }

    private void export(List<File> saves, String name) {
        AlertDialog working = builder().setMessage("Exporting...").setCancelable(false).show();
        new Thread(() -> {
            boolean ok = write(saves, name);
            ui.post(() -> {
                working.dismiss();
                message(ok ? "Saved to Downloads/Dragon Age Origins:\n\n" + name
                                + "\n\n" + (saves.size() == 1 ? "1 save." : saves.size() + " saves.")
                                + " Import it on another device with this page."
                        : "The export could not be written (is the storage full?).", this::show);
            });
        }, "saves-export").start();
    }

    private boolean write(List<File> saves, String name) {
        ContentResolver resolver = activity.getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.Downloads.DISPLAY_NAME, name);
        values.put(MediaStore.Downloads.MIME_TYPE, "application/zip");
        values.put(MediaStore.Downloads.RELATIVE_PATH, FOLDER);
        values.put(MediaStore.Downloads.IS_PENDING, 1);
        Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        if (uri == null) {
            return false;
        }
        boolean ok = false;
        try (OutputStream raw = resolver.openOutputStream(uri);
             ZipOutputStream zip = new ZipOutputStream(raw)) {
            for (File save : saves) {
                addTree(zip, save.getName() + "/", save);
                File display = new File(save.getParentFile(), save.getName() + ".name");
                if (display.isFile()) {
                    addFile(zip, save.getName() + ".name", display);
                }
            }
            zip.finish();
            ok = true;
        } catch (Exception e) {
            Log.w(TAG, "saves: not exported", e);
        }
        ContentValues done = new ContentValues();
        done.put(MediaStore.Downloads.IS_PENDING, 0);
        resolver.update(uri, done, null, null);
        if (!ok) {
            resolver.delete(uri, null, null);
        }
        Log.i(TAG, "saves: " + saves.size() + " exported to " + FOLDER + "/" + name + (ok ? "" : " - failed"));
        return ok;
    }

    private static void addTree(ZipOutputStream zip, String prefix, File dir) throws Exception {
        File[] entries = dir.listFiles();
        if (entries == null) {
            return;
        }
        Arrays.sort(entries);
        for (File entry : entries) {
            if (entry.isDirectory()) {
                addTree(zip, prefix + entry.getName() + "/", entry);
            } else {
                addFile(zip, prefix + entry.getName(), entry);
            }
        }
    }

    private static void addFile(ZipOutputStream zip, String path, File file) throws Exception {
        // Each file's time kept: the game's lists show when a save was written.
        ZipEntry entry = new ZipEntry(path);
        entry.setTime(file.lastModified());
        zip.putNextEntry(entry);
        try (InputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[1 << 16];
            int n;
            while ((n = in.read(buffer)) > 0) {
                zip.write(buffer, 0, n);
            }
        }
        zip.closeEntry();
    }

    // ---- import ----

    private void pick() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        try {
            activity.startActivityForResult(intent, REQUEST_SAVE_ZIP);
        } catch (Exception e) {
            message("This device has no file picker.", this::show);
        }
    }

    /** One save found in an import: its files' folder, the name it had, its label. */
    private static final class Found {
        File folder;
        String name;
        File display;
        String label;
    }

    /** A pick's result (GameActivity.onActivityResult). */
    void onPicked(Uri uri) {
        if (uri == null) {
            show();
            return;
        }
        AlertDialog working = builder().setMessage("Reading the saves...").setCancelable(false).show();
        new Thread(() -> {
            File work = new File(filesDir, ".importing");
            deleteTree(work);
            List<Found> found = new ArrayList<>();
            String error = null;
            try {
                unzip(uri, work);
                find(work, work, found);
            } catch (Exception e) {
                Log.w(TAG, "saves: the import could not be read", e);
                error = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            final String failure = error;
            ui.post(() -> {
                working.dismiss();
                if (failure != null) {
                    deleteTree(work);
                    message("Not imported: " + failure, this::show);
                } else if (found.isEmpty()) {
                    deleteTree(work);
                    message("No Dragon Age: Origins save for the Xbox 360 was found in it. (A"
                            + " save's folder holds Saves/savegame.das; PC saves cannot be"
                            + " used.)", this::show);
                } else {
                    importNext(found, 0, 0, work);
                }
            });
        }, "saves-import").start();
    }

    private void unzip(Uri uri, File to) throws Exception {
        to.mkdirs();
        String root = to.getCanonicalPath() + File.separator;
        long total = 0;
        try (InputStream in = activity.getContentResolver().openInputStream(uri);
             ZipInputStream zip = new ZipInputStream(in)) {
            ZipEntry entry;
            byte[] buffer = new byte[1 << 16];
            while ((entry = zip.getNextEntry()) != null) {
                String path = entry.getName().replace('\\', '/');
                File target = new File(to, path);
                if (path.startsWith("/") || !target.getCanonicalPath().startsWith(root)) {
                    continue;
                }
                if (entry.isDirectory()) {
                    target.mkdirs();
                    continue;
                }
                target.getParentFile().mkdirs();
                try (OutputStream out = new FileOutputStream(target)) {
                    int n;
                    while ((n = zip.read(buffer)) > 0) {
                        total += n;
                        if (total > MAX_IMPORT) {
                            throw new Exception("it is larger than saves could be.");
                        }
                        out.write(buffer, 0, n);
                    }
                }
                if (entry.getTime() > 0) {
                    target.setLastModified(entry.getTime());
                }
            }
        }
    }

    /** Every folder in the import holding a save of this game (Saves/savegame.das so headed). */
    private static void find(File root, File dir, List<Found> found) {
        File das = new File(dir, "Saves/savegame.das");
        if (das.isFile() && isThisGamesSave(das)) {
            Found f = new Found();
            f.folder = dir;
            f.name = dir.equals(root) ? null : dir.getName();
            File display = new File(dir.getParentFile(), dir.getName() + ".name");
            f.display = display.isFile() ? display : null;
            File met = new File(dir, "Saves/savegame.met");
            f.label = met.isFile() ? SaveRestore.label(met, das.lastModified())
                    : (f.name != null ? f.name : "A save");
            found.add(f);
            return;
        }
        File[] children = dir.listFiles();
        if (children != null) {
            Arrays.sort(children);
            for (File child : children) {
                if (child.isDirectory()) {
                    find(root, child, found);
                }
            }
        }
    }

    private static boolean isThisGamesSave(File das) {
        byte[] head = new byte[SAVE_MAGIC.length];
        try (InputStream in = new FileInputStream(das)) {
            return in.read(head) == head.length && Arrays.equals(head, SAVE_MAGIC);
        } catch (Exception e) {
            return false;
        }
    }

    /** Each found save in turn: in if its slot is free, else the player's choice. */
    private void importNext(List<Found> found, int index, int imported, File work) {
        if (index >= found.size()) {
            deleteTree(work);
            message(imported == 0 ? "Nothing was imported."
                    : (imported == 1 ? "1 save imported. It is" : imported + " saves imported."
                            + " They are") + " in the game's Load Game list.", this::show);
            return;
        }
        Found f = found.get(index);
        String name = f.name != null && SLOT.matcher(f.name).matches() ? f.name : null;
        File existing = name != null ? new File(savesDir(), name) : null;
        if (existing == null || !existing.exists()) {
            boolean ok = put(f, name != null ? name : freeSlot(null), false);
            importNext(found, index + 1, imported + (ok ? 1 : 0), work);
            return;
        }
        builder().setTitle("Already on this device")
                .setMessage(f.label + "\n\nwould take the place of\n\n" + label(existing))
                .setPositiveButton("Keep both", (d, w) -> {
                    boolean ok = put(f, freeSlot(name), false);
                    importNext(found, index + 1, imported + (ok ? 1 : 0), work);
                })
                .setNeutralButton("Replace", (d, w) -> {
                    boolean ok = put(f, name, true);
                    importNext(found, index + 1, imported + (ok ? 1 : 0), work);
                })
                .setNegativeButton("Skip", (d, w) -> importNext(found, index + 1, imported, work))
                .setCancelable(false)
                .show();
    }

    /**
     * A free slot: the same character's next manual slot (`_<n>_u!0` one past their
     * highest), or a new one when the import names none.
     */
    private String freeSlot(String like) {
        String character = "0x10000000";
        if (like != null) {
            Matcher m = SLOT.matcher(like);
            if (m.matches()) {
                character = m.group(1);
            }
        }
        int highest = 0;
        File[] folders = savesDir().listFiles();
        if (folders != null) {
            for (File folder : folders) {
                Matcher m = SLOT.matcher(folder.getName());
                if (m.matches() && m.group(1).equalsIgnoreCase(character)) {
                    highest = Math.max(highest, Integer.parseInt(m.group(2)));
                }
            }
        }
        return character + "_" + (highest + 1) + "_u!0";
    }

    /** The found save into its slot; a save there kept in .backup/ when replaced. */
    private boolean put(Found f, String name, boolean replace) {
        File target = new File(savesDir(), name);
        File targetName = new File(savesDir(), name + ".name");
        try {
            savesDir().mkdirs();
            if (target.exists()) {
                if (!replace) {
                    return false;
                }
                File backups = new File(savesDir(), ".backup");
                backups.mkdirs();
                File backup = new File(backups, name);
                deleteTree(backup);
                if (!target.renameTo(backup)) {
                    return false;
                }
            }
            copyTree(f.folder, target);
            byte[] display = new byte[256];
            if (f.display != null) {
                try (InputStream in = new FileInputStream(f.display)) {
                    int n = in.read(display);
                    if (n <= 0) {
                        display = nameBytes(f.label);
                    }
                }
            } else {
                display = nameBytes(f.label);
            }
            try (OutputStream out = new FileOutputStream(targetName)) {
                out.write(display);
            }
            Log.i(TAG, "saves: imported " + f.label + " as " + name + (replace ? " (replacing)" : ""));
            return true;
        } catch (Exception e) {
            Log.w(TAG, "saves: " + f.label + " not imported", e);
            return false;
        }
    }

    /** A display name as the game keeps them: UTF-16 big-endian in 256 bytes. */
    private static byte[] nameBytes(String text) {
        byte[] out = new byte[256];
        byte[] encoded = text.getBytes(StandardCharsets.UTF_16BE);
        System.arraycopy(encoded, 0, out, 0, Math.min(encoded.length, 254));
        return out;
    }

    private static void copyTree(File from, File to) throws Exception {
        to.mkdirs();
        File[] entries = from.listFiles();
        if (entries == null) {
            return;
        }
        for (File entry : entries) {
            File target = new File(to, entry.getName());
            if (entry.isDirectory()) {
                copyTree(entry, target);
            } else {
                try (InputStream in = new FileInputStream(entry);
                     OutputStream out = new FileOutputStream(target)) {
                    byte[] buffer = new byte[1 << 16];
                    int n;
                    while ((n = in.read(buffer)) > 0) {
                        out.write(buffer, 0, n);
                    }
                }
                target.setLastModified(entry.lastModified());
            }
        }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        file.delete();
    }

    private void message(String text, Runnable then) {
        builder().setMessage(text)
                .setPositiveButton("OK", (d, w) -> then.run())
                .setOnCancelListener(d -> then.run())
                .show();
    }
}
