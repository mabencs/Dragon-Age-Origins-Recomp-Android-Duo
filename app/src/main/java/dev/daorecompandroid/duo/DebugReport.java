package dev.daorecompandroid.duo;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.List;
import java.util.ArrayList;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * PLAN 11.1, Debugging Mode's report. With Options -> Debugging Mode on (files/debug-mode,
 * from the next start) the native side writes the run's whole log to files/debug/
 * (warden_debug_mode.cc) and keeps the save it loads; the options panel's "Exit and export
 * save/logs" then makes a fresh save and packs everything a report needs into one .zip in
 * Downloads/Dragon Age Origins (MediaStore: no permission), for the player to send from any
 * file manager or messenger: this run's log and the last run's, the save loaded and the
 * fresh one, the exit reports and tombstones (files/exits/), a screenshot, the options and
 * markers in effect, and a readme naming the app, its commit, the device and the driver.
 */
final class DebugReport {

    private static final String TAG = "Warden";
    static final String OPTION = "debug-mode";
    private static final String FOLDER = Environment.DIRECTORY_DOWNLOADS + "/Dragon Age Origins";
    /** Files in the files folder that are not markers: the game's data and the app's own. */
    private static final long MARKER_MAX_BYTES = 4096;

    private DebugReport() {
    }

    /** Whether Debugging Mode is on for a run starting now. */
    static boolean onAtStart(File filesDir) {
        File file = new File(filesDir, OPTION);
        if (!file.isFile()) {
            return false;
        }
        try {
            return new String(readAll(file, 64), StandardCharsets.UTF_8).trim().equals("on");
        } catch (Exception e) {
            return false;
        }
    }

    /** The app's version and commit, the device, Android and the options in effect. */
    static String describe(Context context, File filesDir) {
        StringBuilder text = new StringBuilder();
        String version = "?";
        long code = 0;
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(
                    context.getPackageName(), 0);
            version = info.versionName;
            code = info.getLongVersionCode();
        } catch (Exception e) {
            // The version is a nicety here.
        }
        text.append("App: Dragon Age Origins Recomp Android Duo ").append(version)
                .append(" (").append(code).append("), commit ").append(BuildConfig.GIT_COMMIT)
                .append(", ").append(BuildConfig.DEBUG ? "debug" : "release").append(" build\n");
        text.append("Device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append(" (").append(Build.DEVICE).append("), SoC ").append(Build.SOC_MODEL)
                .append('\n');
        text.append("Android: ").append(Build.VERSION.RELEASE).append(" (API ")
                .append(Build.VERSION.SDK_INT).append("), build ").append(Build.DISPLAY)
                .append('\n');
        // The owner (2026-11-02): which title update the game's code was made from - a
        // report is looked into only with Title Update 7 (README).
        text.append("Game's code: ").append(GameCode.madeFrom(context)).append('\n');
        // The owner (2026-11-02): plainly, whether mods are loaded, and which.
        text.append(modsLoaded(context, filesDir));
        text.append("Options:");
        for (Options.Option option : Options.ALL) {
            text.append(' ').append(option.file).append('=').append(Options.read(filesDir, option));
        }
        text.append('\n');
        text.append("Markers:");
        File[] files = filesDir.listFiles();
        if (files != null) {
            java.util.Arrays.sort(files);
            for (File file : files) {
                if (file.isFile() && file.length() <= MARKER_MAX_BYTES && !isOption(file.getName())) {
                    text.append(' ').append(file.getName());
                }
            }
        }
        text.append('\n');
        return text.toString();
    }

    /** The mods on, in the Mods page's order (the last wins), and their add-ins, or "none". */
    static String modsLoaded(Context context, File filesDir) {
        // What the game loads: files/mods-enabled.txt, the one that wins first (Mods.save).
        List<String> loaded = new ArrayList<>();
        File enabled = new File(filesDir, "mods-enabled.txt");
        if (enabled.isFile()) {
            try {
                for (String line : new String(java.nio.file.Files.readAllBytes(enabled.toPath()),
                        StandardCharsets.UTF_8).split("\n")) {
                    if (!line.trim().isEmpty()) {
                        loaded.add(line.trim());
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "debug mode: mods-enabled.txt not read", e);
            }
        }
        List<Mods.Mod> all = Mods.scan(context);
        List<String> addins = new ArrayList<>();
        List<String> notLoaded = new ArrayList<>();
        for (Mods.Mod mod : all) {
            for (String[] addin : mod.addins) {
                addins.add(addin[1] + " (" + addin[0] + ", from " + mod.name + ") - "
                        + (mod.enabled ? "on" : "off, not loaded"));
            }
            if (!mod.isLoaded()) {
                notLoaded.add(mod.name + (mod.enabled ? " (the PC version's files, not"
                        + " converted)" : " (off)"));
            }
        }
        StringBuilder text = new StringBuilder();
        if (loaded.isEmpty() && addins.isEmpty()) {
            text.append("Mods: NONE LOADED - the game as on the disc\n");
        } else {
            text.append("Mods: ").append(loaded.size()).append(" LOADED, ")
                    .append(addins.size()).append(" add-in").append(addins.size() == 1 ? "" : "s")
                    .append(" from mods - a modded game\n");
            for (int k = 0; k < loaded.size(); k++) {
                Mods.Mod mod = Mods.describe(new File(loaded.get(k)));
                text.append("  ").append(k + 1).append(". ").append(mod.name)
                        .append(mod.version.isEmpty() || mod.name.contains(mod.version) ? ""
                                : " " + mod.version)
                        .append(mod.author.isEmpty() ? "" : ", by " + mod.author)
                        .append(k == 0 && loaded.size() > 1 ? " (wins over those below)" : "")
                        .append(" - ").append(new File(loaded.get(k)).getName()).append('\n');
            }
            for (String addin : addins) {
                text.append("  add-in: ").append(addin).append('\n');
            }
        }
        if (!notLoaded.isEmpty()) {
            text.append("  in the Mods list, not loaded: ").append(String.join("; ", notLoaded))
                    .append('\n');
        }
        return text.toString();
    }

    private static boolean isOption(String name) {
        for (Options.Option option : Options.ALL) {
            if (option.file.equals(name)) {
                return true;
            }
        }
        return false;
    }

    /** Before the runtime starts: the header the native log begins with. */
    static void writeHeader(Context context, File filesDir) {
        File dir = new File(filesDir, "debug");
        dir.mkdirs();
        String header = describe(context, filesDir)
                + "Started: " + new SimpleDateFormat("yyyy-MM-dd HH:mm:ss Z", Locale.ROOT)
                        .format(new Date()) + "\n";
        try (OutputStream out = new FileOutputStream(new File(dir, "run-header.txt"))) {
            out.write(header.getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            Log.w(TAG, "debug mode: no header", e);
        }
    }

    /** The newest save folder written since a time - the report's fresh save. */
    static File newestSaveSince(File filesDir, long sinceMillis) {
        File[] folders = new File(filesDir, "content/00000001").listFiles();
        File newest = null;
        if (folders != null) {
            for (File folder : folders) {
                if (!folder.isDirectory() || folder.getName().startsWith(".")) {
                    continue;
                }
                long modified = lastModifiedIn(folder);
                if (modified >= sinceMillis
                        && (newest == null || modified > lastModifiedIn(newest))) {
                    newest = folder;
                }
            }
        }
        return newest;
    }

    private static long lastModifiedIn(File folder) {
        long newest = folder.lastModified();
        File[] files = folder.listFiles();
        if (files != null) {
            for (File file : files) {
                newest = Math.max(newest, file.isDirectory() ? lastModifiedIn(file)
                        : file.lastModified());
            }
        }
        return newest;
    }

    /**
     * The report packed into Downloads/Dragon Age Origins; its file name, or null.
     * saveResult: 1 a fresh save made, 0 none possible, -1 not in time. Not on the UI thread.
     */
    static String export(Context context, File filesDir, Bitmap screenshot, int saveResult,
                         long saveAskedMillis) {
        String name = "warden-report-"
                + new SimpleDateFormat("yyyyMMdd-HHmm", Locale.ROOT).format(new Date()) + ".zip";
        ContentResolver resolver = context.getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.Downloads.DISPLAY_NAME, name);
        values.put(MediaStore.Downloads.MIME_TYPE, "application/zip");
        values.put(MediaStore.Downloads.RELATIVE_PATH, FOLDER);
        values.put(MediaStore.Downloads.IS_PENDING, 1);
        Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        if (uri == null) {
            return null;
        }
        boolean ok = false;
        File debug = new File(filesDir, "debug");
        File fresh = saveResult == 1 ? newestSaveSince(filesDir, saveAskedMillis - 2000) : null;
        try (OutputStream raw = resolver.openOutputStream(uri);
             ZipOutputStream zip = new ZipOutputStream(raw)) {
            StringBuilder readme = new StringBuilder();
            readme.append("Dragon Age Origins Recomp Android Duo - a bug report\n\n")
                    .append(describe(context, filesDir))
                    .append("Made: ").append(new SimpleDateFormat("yyyy-MM-dd HH:mm:ss Z",
                            Locale.ROOT).format(new Date())).append("\n\n")
                    .append("What is in it:\n")
                    .append("  logs/log-current.txt   this run's whole log (from the start of the run)\n")
                    .append("  logs/log-previous.txt  the run before's, when there was one\n")
                    .append("  logs/run-header.txt    the device and options as this run began\n")
                    .append("  saves/loaded/          the save loaded in this run (as it was then)\n")
                    .append("  saves/fresh/           a save made just now, where the game allowed one\n")
                    .append("  exits/                 how earlier runs ended (Android's reports, tombstones)\n")
                    .append("  screenshot.png         the screen as the report was made\n\n");
            readme.append("The fresh save: ").append(saveResult == 1
                    ? (fresh != null ? fresh.getName() : "written, but not found")
                    : saveResult == 0 ? "none - the game allowed no save there (not exploring)"
                            : "asked for, not written in time").append('\n');
            File loaded = new File(debug, "loaded-save");
            readme.append("The save loaded: ").append(loaded.isDirectory()
                    && loaded.list() != null && loaded.list().length > 0
                    ? String.join(", ", loaded.list()) : "none recorded (no save loaded)")
                    .append('\n');
            addText(zip, "readme.txt", readme.toString());
            addFile(zip, "logs/log-current.txt", new File(debug, "log-current.txt"));
            addFile(zip, "logs/log-previous.txt", new File(debug, "log-previous.txt"));
            addFile(zip, "logs/run-header.txt", new File(debug, "run-header.txt"));
            addTree(zip, "saves/loaded/", loaded);
            if (fresh != null) {
                addTree(zip, "saves/fresh/" + fresh.getName() + "/", fresh);
                File display = new File(fresh.getParentFile(), fresh.getName() + ".name");
                addFile(zip, "saves/fresh/" + fresh.getName() + ".name", display);
            }
            addTree(zip, "exits/", new File(filesDir, "exits"));
            if (screenshot != null) {
                ByteArrayOutputStream png = new ByteArrayOutputStream();
                screenshot.compress(Bitmap.CompressFormat.PNG, 100, png);
                zip.putNextEntry(new ZipEntry("screenshot.png"));
                zip.write(png.toByteArray());
                zip.closeEntry();
            }
            zip.finish();
            ok = true;
        } catch (Exception e) {
            Log.w(TAG, "debug mode: the report was not written", e);
        }
        ContentValues done = new ContentValues();
        done.put(MediaStore.Downloads.IS_PENDING, 0);
        resolver.update(uri, done, null, null);
        if (!ok) {
            resolver.delete(uri, null, null);
            return null;
        }
        Log.i(TAG, "debug mode: the report written to " + FOLDER + "/" + name);
        return name;
    }

    private static void addText(ZipOutputStream zip, String path, String text) throws Exception {
        zip.putNextEntry(new ZipEntry(path));
        zip.write(text.getBytes(StandardCharsets.UTF_8));
        zip.closeEntry();
    }

    private static void addFile(ZipOutputStream zip, String path, File file) throws Exception {
        if (!file.isFile()) {
            return;
        }
        zip.putNextEntry(new ZipEntry(path));
        byte[] buffer = new byte[1 << 16];
        // The current log is still being written: what is there now.
        try (InputStream in = new FileInputStream(file)) {
            int n;
            while ((n = in.read(buffer)) > 0) {
                zip.write(buffer, 0, n);
            }
        }
        zip.closeEntry();
    }

    private static void addTree(ZipOutputStream zip, String prefix, File dir) throws Exception {
        File[] entries = dir.listFiles();
        if (entries == null) {
            return;
        }
        for (File entry : entries) {
            if (entry.isDirectory()) {
                addTree(zip, prefix + entry.getName() + "/", entry);
            } else {
                addFile(zip, prefix + entry.getName(), entry);
            }
        }
    }

    private static byte[] readAll(File file, int max) throws Exception {
        try (InputStream in = new FileInputStream(file)) {
            byte[] data = new byte[max];
            int n = in.read(data);
            return n > 0 ? java.util.Arrays.copyOf(data, n) : new byte[0];
        }
    }
}
