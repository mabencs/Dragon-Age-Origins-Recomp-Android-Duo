package dev.daorecompandroid.duo;

import android.app.Activity;
import android.app.AlertDialog;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * PLAN 10.15, the save restore page (the owner picked it from upstream, 2026-11-01). A
 * save the game writes over is first moved aside, whole, to
 * {@code files/content/<type>/.backup/<its folder>} (warden_xam.cc's XamContentCreateEx,
 * session 32). This lists those kept saves - the character, the chapter, when it was
 * written - and puts one back: the kept save and the one that replaced it change places,
 * so a restore is itself undone by restoring again (upstream sets the save it replaces
 * aside as {@code ~current} while it works; here it is the swap's middle step).
 */
final class SaveRestore {

    private static final String TAG = "Warden";

    /** A kept save: where it is, where it goes back to, and its label. */
    static final class Kept {
        File backup;
        File live;
        String label;
        long time;
    }

    private SaveRestore() {
    }

    /** The kept saves, newest first. */
    static List<Kept> list(File filesDir) {
        List<Kept> kept = new ArrayList<>();
        File[] types = new File(filesDir, "content").listFiles();
        if (types == null) {
            return kept;
        }
        for (File type : types) {
            File[] backups = new File(type, ".backup").listFiles();
            if (backups == null) {
                continue;
            }
            for (File backup : backups) {
                if (!backup.isDirectory() || backup.getName().endsWith("~current")) {
                    continue;
                }
                File met = new File(backup, "Saves/savegame.met");
                if (!met.isFile()) {
                    continue;  // not a game save (the profile's, the options')
                }
                Kept k = new Kept();
                k.backup = backup;
                k.live = new File(type, backup.getName());
                k.time = new File(backup, "Saves/savegame.das").lastModified();
                k.label = label(met, k.time);
                kept.add(k);
            }
        }
        kept.sort((a, b) -> Long.compare(b.time, a.time));
        return kept;
    }

    /**
     * The character and the chapter from the save's metadata (a GFF file: after its
     * header, its strings - the character's name, a code, the chapter's title, its
     * description), and when it was written.
     */
    static String label(File met, long time) {
        String character = "A save";
        String chapter = "";
        try (FileInputStream in = new FileInputStream(met)) {
            byte[] data = in.readAllBytes();
            if (data.length > 0x20) {
                int start = ((data[0x1C] & 0xFF) << 24) | ((data[0x1D] & 0xFF) << 16)
                        | ((data[0x1E] & 0xFF) << 8) | (data[0x1F] & 0xFF);
                List<String> strings = new ArrayList<>();
                int at = Math.max(0, Math.min(start, data.length));
                StringBuilder current = new StringBuilder();
                for (int i = at; i < data.length; i++) {
                    int c = data[i] & 0xFF;
                    if (c == 0) {
                        if (current.length() > 0) {
                            strings.add(current.toString());
                            current.setLength(0);
                        }
                    } else if (c >= 0x20 && c < 0x7F) {
                        current.append((char) c);
                    } else {
                        current.setLength(0);
                    }
                }
                if (!strings.isEmpty()) {
                    character = strings.get(0);
                }
                // The chapter: the first short string with a space in it (the description
                // is long).
                for (int i = 1; i < strings.size(); i++) {
                    String s = strings.get(i);
                    if (s.contains(" ") && s.length() <= 60) {
                        chapter = s;
                        break;
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "saves: " + met + " not read", e);
        }
        String when = new SimpleDateFormat("d MMM, HH:mm", Locale.getDefault())
                .format(new Date(time));
        return character + (chapter.isEmpty() ? "" : " - " + chapter) + "  (" + when + ")";
    }

    /** The kept save back in its place, the one that replaced it kept instead. */
    static boolean restore(Kept k) {
        File middle = new File(k.backup.getParentFile(), k.backup.getName() + "~current");
        deleteTree(middle);
        boolean hadLive = k.live.exists();
        if (hadLive && !k.live.renameTo(middle)) {
            Log.w(TAG, "saves: " + k.live + " could not be set aside");
            return false;
        }
        if (!k.backup.renameTo(k.live)) {
            Log.w(TAG, "saves: " + k.backup + " could not be put back");
            if (hadLive) {
                middle.renameTo(k.live);
            }
            return false;
        }
        if (hadLive && !middle.renameTo(k.backup)) {
            Log.w(TAG, "saves: the replaced save stays as " + middle);
        }
        Log.i(TAG, "saves: " + k.live.getName() + " restored (" + k.label + ")");
        return true;
    }

    /** The page: the kept saves to choose from, then a confirmation. */
    static void show(Activity activity, File filesDir, Runnable done) {
        List<Kept> kept = list(filesDir);
        AlertDialog.Builder builder = new AlertDialog.Builder(activity,
                android.R.style.Theme_DeviceDefault_Dialog_Alert);
        builder.setTitle("Restore an earlier save");
        if (kept.isEmpty()) {
            builder.setMessage("No earlier saves are kept yet. When the game writes over a"
                    + " save, the one it replaces is kept here.");
            builder.setPositiveButton("Close", (d, w) -> done.run());
            builder.setOnCancelListener(d -> done.run());
            builder.show();
            return;
        }
        String[] labels = new String[kept.size()];
        for (int i = 0; i < labels.length; i++) {
            labels[i] = kept.get(i).label;
        }
        builder.setItems(labels, (dialog, which) -> {
            Kept k = kept.get(which);
            new AlertDialog.Builder(activity, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("Put this save back?")
                    .setMessage(k.label + "\n\nThe save in its slot now is kept instead, so"
                            + " this can be undone the same way.")
                    .setPositiveButton("Restore", (d, w) -> {
                        boolean ok = restore(k);
                        new AlertDialog.Builder(activity,
                                android.R.style.Theme_DeviceDefault_Dialog_Alert)
                                .setMessage(ok ? "Restored. It is in the game's Load Game list."
                                        : "It could not be restored; nothing was changed.")
                                .setPositiveButton("OK", (d2, w2) -> done.run())
                                .setOnCancelListener(d2 -> done.run())
                                .show();
                    })
                    .setNegativeButton("Cancel", (d, w) -> done.run())
                    .setOnCancelListener(d -> done.run())
                    .show();
        });
        builder.setNegativeButton("Close", (d, w) -> done.run());
        builder.setOnCancelListener(d -> done.run());
        builder.show();
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
}
