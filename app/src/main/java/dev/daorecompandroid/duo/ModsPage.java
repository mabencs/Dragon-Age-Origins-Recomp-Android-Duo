package dev.daorecompandroid.duo;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;

import java.util.ArrayList;
import java.util.List;

/**
 * PLAN 10.12, the launcher's Mods page (upstream's, on Android): the mods in their load
 * order - on or off, made for the PC version or not - with adding one (a .zip or a
 * folder, through Android's file picker), and for each: what it is, on or off, moving it
 * up or down the load order, deleting it. Changes take effect from the next start;
 * before the game only (Mods.save writes the game's list).
 */
final class ModsPage {

    static final int REQUEST_MOD_ZIP = 41;
    static final int REQUEST_MOD_FOLDER = 42;

    private final Activity activity;
    private final Runnable done;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private List<Mods.Mod> mods = new ArrayList<>();

    ModsPage(Activity activity, Runnable done) {
        this.activity = activity;
        this.done = done;
    }

    private AlertDialog.Builder builder() {
        return new AlertDialog.Builder(activity, android.R.style.Theme_DeviceDefault_Dialog_Alert);
    }

    /** The list. */
    void show() {
        mods = Mods.scan(activity);
        Mods.save(activity, mods);
        List<String> items = new ArrayList<>();
        items.add("+ Add a mod from a file (.zip, .7z, .rar, .dazip)");
        items.add("+ Add a mod from a folder");
        for (Mods.Mod mod : mods) {
            items.add((mod.isForPc() ? "[PC version]  " : mod.enabled ? "[on]  " : "[off]  ")
                    + mod.name + (mod.version.isEmpty() ? "" : "  " + mod.version));
        }
        builder()
                .setTitle(mods.isEmpty() ? "Mods - none yet" : "Mods - lower in the list wins")
                .setItems(items.toArray(new String[0]), (d, which) -> {
                    if (which == 0) {
                        pick(false);
                    } else if (which == 1) {
                        pick(true);
                    } else {
                        actions(which - 2);
                    }
                })
                .setNegativeButton("Close", (d, w) -> done.run())
                .setOnCancelListener(d -> done.run())
                .show();
    }

    private void pick(boolean folder) {
        Intent intent;
        if (folder) {
            intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        } else {
            intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            // Any file: Android names .rar and .dazip files' types variously (a .rar was
            // greyed out under a list of them), and Mods.unpack tells them apart by their
            // first bytes.
            intent.setType("*/*");
        }
        try {
            activity.startActivityForResult(intent, folder ? REQUEST_MOD_FOLDER : REQUEST_MOD_ZIP);
        } catch (Exception e) {
            message("This device has no file picker.", this::show);
        }
    }

    /** A pick's result (GameActivity.onActivityResult): the mod added off the UI thread. */
    void onPicked(Uri uri, boolean folder) {
        if (uri == null) {
            show();
            return;
        }
        AlertDialog working = builder().setMessage("Adding the mod...").setCancelable(false).show();
        new Thread(() -> {
            Mods.Added added = Mods.add(activity, uri, folder);
            ui.post(() -> {
                working.dismiss();
                StringBuilder text = new StringBuilder();
                if (added.error != null) {
                    text.append("Not added: ").append(added.error);
                } else {
                    Mods.Mod mod = Mods.describe(new java.io.File(Mods.root(activity),
                            added.folder));
                    text.append("Added: ").append(mod.name).append(" (on, at the bottom of the"
                            + " list - it wins over the others).");
                    for (String note : added.notes) {
                        text.append("\n\n").append(note);
                    }
                    for (String warning : mod.warnings) {
                        text.append("\n\n").append(warning);
                    }
                    text.append("\n\nFrom the next start of the game.");
                }
                message(text.toString(), this::show);
            });
        }, "mods-add").start();
    }

    private void actions(int index) {
        Mods.Mod mod = mods.get(index);
        List<String> items = new ArrayList<>();
        items.add("About this mod");
        if (mod.isForPc()) {
            items.add("Convert for this version");
        } else {
            items.add(mod.enabled ? "Turn off" : "Turn on");
        }
        if (index > 0) {
            items.add("Move up (loses to more)");
        }
        if (index < mods.size() - 1) {
            items.add("Move down (wins over more)");
        }
        items.add("Delete");
        builder().setTitle(mod.name)
                .setItems(items.toArray(new String[0]), (d, which) -> {
                    String item = items.get(which);
                    if (item.equals("About this mod")) {
                        about(mod);
                    } else if (item.equals("Convert for this version")) {
                        convert(mod);
                    } else if (item.startsWith("Turn")) {
                        mod.enabled = !mod.enabled;
                        Mods.save(activity, mods);
                        if (!mod.enabled && !mod.addins.isEmpty()) {
                            message("Its add-in is off from the next start. Saves made while it"
                                    + " was on will not load until it is on again.", this::show);
                        } else {
                            show();
                        }
                    } else if (item.startsWith("Move up")) {
                        mods.add(index - 1, mods.remove(index));
                        Mods.save(activity, mods);
                        show();
                    } else if (item.startsWith("Move down")) {
                        mods.add(index + 1, mods.remove(index));
                        Mods.save(activity, mods);
                        show();
                    } else if (item.equals("Delete")) {
                        confirmDelete(mod);
                    }
                })
                .setNegativeButton("Back", (d, w) -> show())
                .setOnCancelListener(d -> show())
                .show();
    }

    private void about(Mods.Mod mod) {
        StringBuilder text = new StringBuilder();
        text.append(mod.name);
        if (!mod.version.isEmpty()) {
            text.append("  ").append(mod.version);
        }
        if (!mod.author.isEmpty()) {
            text.append("\nby ").append(mod.author);
        }
        if (!mod.description.isEmpty()) {
            text.append("\n\n").append(mod.description);
        }
        text.append("\n\n").append(mod.files).append(mod.files == 1 ? " file" : " files")
                .append(" for the game. ")
                .append(mod.isForPc() ? "Not loaded." : mod.enabled ? "On." : "Off.");
        for (String[] addin : mod.addins) {
            text.append("\n\nInstalled as downloadable content: ").append(addin[1]);
        }
        for (String warning : mod.warnings) {
            text.append("\n\n").append(warning);
        }
        String overlap = Mods.overlaps(activity, mods, mod);
        if (!overlap.isEmpty()) {
            text.append("\n\n").append(overlap);
        }
        message(text.toString(), this::show);
    }

    /**
     * PLAN 10.13: the mod's PC files converted to the 360's formats (DAORecomp's
     * converter, native), off the UI thread; what it did said after.
     */
    private void convert(Mods.Mod mod) {
        AlertDialog working = builder().setMessage("Converting " + mod.name
                + " for this version... this can take a few minutes.").setCancelable(false).show();
        String folder = new java.io.File(Mods.root(activity), mod.folder).getAbsolutePath();
        String game = GameData.destination(activity).getAbsolutePath();
        String content = GameData.dlcFolder(activity).getAbsolutePath();
        new Thread(() -> {
            String report;
            try {
                report = NativeBridge.convertMod(folder, game, content);
            } catch (Throwable e) {
                report = "problem\t" + e;
            }
            final String text = convertReport(report);
            ui.post(() -> {
                working.dismiss();
                message(text, this::show);
            });
        }, "mods-convert").start();
    }

    private static String convertReport(String report) {
        StringBuilder text = new StringBuilder();
        List<String> problems = new ArrayList<>();
        List<String> leftOut = new ArrayList<>();
        List<String> notes = new ArrayList<>();
        List<String> addins = new ArrayList<>();
        boolean converted = false;
        int files = 0;
        for (String line : report.split("\n")) {
            String[] parts = line.split("\t", 3);
            if (parts[0].equals("addin") && parts.length >= 2) {
                addins.add(parts[1]);
            } else if (parts[0].equals("result") && parts.length >= 3) {
                converted = parts[1].equals("1");
                try {
                    files = Integer.parseInt(parts[2].trim());
                } catch (NumberFormatException e) {
                    files = 0;
                }
            } else if (parts.length >= 2) {
                (parts[0].equals("problem") ? problems : parts[0].equals("left-out") ? leftOut
                        : notes).add(parts[1]);
            }
        }
        if (converted) {
            text.append("Converted: ").append(files).append(files == 1 ? " file" : " files")
                    .append(" written for this version. Its PC files are kept in"
                            + " mods/.originals/, to undo it by hand. From the next start.");
        } else {
            text.append("Not converted.");
        }
        for (String title : addins) {
            text.append("\n\nInstalled as downloadable content: ").append(title)
                    .append(". Saves made with it need it installed and on.");
        }
        for (String p : problems) {
            text.append("\n\n").append(p);
        }
        if (!leftOut.isEmpty()) {
            text.append("\n\nLeft out (").append(leftOut.size()).append("):");
            for (int i = 0; i < Math.min(leftOut.size(), 8); i++) {
                text.append("\n- ").append(leftOut.get(i));
            }
            if (leftOut.size() > 8) {
                text.append("\n- and ").append(leftOut.size() - 8).append(" more");
            }
        }
        for (String n : notes) {
            text.append("\n\n").append(n);
        }
        return text.toString();
    }

    private void confirmDelete(Mods.Mod mod) {
        builder().setTitle("Delete " + mod.name + "?")
                .setMessage("Its folder is removed from the app; this cannot be undone."
                        + (mod.addins.isEmpty() ? "" : " Its add-in is uninstalled too: saves"
                                + " made with it will not load until it is added again."))
                .setPositiveButton("Delete", (d, w) -> {
                    Mods.delete(activity, mod);
                    show();
                })
                .setNegativeButton("Cancel", (d, w) -> show())
                .setOnCancelListener(d -> show())
                .show();
    }

    private void message(String text, Runnable then) {
        builder().setMessage(text)
                .setPositiveButton("OK", (d, w) -> then.run())
                .setOnCancelListener(d -> then.run())
                .show();
    }
}
