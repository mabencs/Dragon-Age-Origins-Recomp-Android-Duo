package dev.daorecompandroid.duo;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.pm.PackageInfo;
import android.util.Log;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * PLAN 11.5, About and licences: what the app is, the plain sentence that the game is the
 * player's own and nothing of it ships with the app, and every project whose code is built
 * into it with its licence - each one's full text a tap away. The texts are in
 * assets/licences/, gathered from the pinned sources by tools/gather_licences.py.
 */
final class AboutPage {

    private static final String TAG = "Warden";

    private AboutPage() {
    }

    /** One project: its file in assets/licences/, its name, licence and use. */
    private static final class Entry {
        String file;
        String name;
        String licence;
        String used;
    }

    private static AlertDialog.Builder builder(Activity activity) {
        return new AlertDialog.Builder(activity, android.R.style.Theme_DeviceDefault_Dialog_Alert);
    }

    static void show(Activity activity, Runnable done) {
        List<Entry> entries = index(activity);
        String version = "";
        try {
            PackageInfo info = activity.getPackageManager().getPackageInfo(
                    activity.getPackageName(), 0);
            version = info.versionName;
        } catch (Exception e) {
            // The version is a nicety.
        }
        String[] items = new String[entries.size() + 1];
        items[0] = "About this app";
        for (int i = 0; i < entries.size(); i++) {
            items[i + 1] = entries.get(i).name + "  -  " + entries.get(i).licence;
        }
        final String appVersion = version;
        builder(activity).setTitle("About and licences")
                .setItems(items, (d, which) -> {
                    if (which == 0) {
                        text(activity, "Dragon Age Origins Recomp Android Duo", about(appVersion),
                                () -> show(activity, done));
                    } else {
                        Entry entry = entries.get(which - 1);
                        text(activity, entry.name, read(activity, "licences/" + entry.file),
                                () -> show(activity, done));
                    }
                })
                .setNegativeButton("Close", (d, w) -> done.run())
                .setOnCancelListener(d -> done.run())
                .show();
    }

    private static String about(String version) {
        return "Version " + version
                + (BuildConfig.RELEASE_NAME.isEmpty() ? "" : " - " + BuildConfig.RELEASE_NAME)
                + ", commit " + BuildConfig.GIT_COMMIT + ".\n\n"
                + "Dragon Age: Origins is BioWare's and Electronic Arts' game, and this app"
                + " holds none of it. It plays the copy you own: the game's program is"
                + " translated from your disc's executable and title update on this device,"
                + " once, by the compiler the app carries, and the game's pictures, sounds"
                + " and words are read from your disc image here. It is not made, endorsed"
                + " or supported by BioWare or Electronic Arts.\n\n"
                + "The app has no online functions: it never connects to the internet.\n\n"
                + "The app is free software under the GNU General Public License, version 3"
                + " or any later version (the first entry on the previous page has its text),"
                + " and comes with no warranty. "
                + (BuildConfig.SOURCE_URL.isEmpty()
                        ? "Its complete source is the repository it was built from."
                        : "Its complete source: " + BuildConfig.SOURCE_URL + ".")
                + " It is built mainly on DAORecomp (github.com/96dot/Dragon-Age-Origins-Recomp)"
                + " and Xenia.\n\n"
                + "It is built on the work of the projects listed on the previous page, each"
                + " under its own licence - tap one for its full text, its version and where"
                + " its source is. lsfg-vk is under the GNU GPL 3.0, and FFmpeg and libmspack"
                + " under the GNU LGPL 2.1: their sources are at the addresses given there."
                + " Lossless Scaling's shaders, which frame generation uses, are read from"
                + " your own copy of Lossless.dll; the app does not include them.";
    }

    private static List<Entry> index(Activity activity) {
        List<Entry> entries = new ArrayList<>();
        for (String line : read(activity, "licences/index.txt").split("\n")) {
            String[] parts = line.split("\t");
            if (parts.length >= 4) {
                Entry entry = new Entry();
                entry.file = parts[0];
                entry.name = parts[1];
                entry.licence = parts[2];
                entry.used = parts[3];
                entries.add(entry);
            }
        }
        return entries;
    }

    private static String read(Activity activity, String asset) {
        try (InputStream in = activity.getAssets().open(asset)) {
            return new String(in.readAllBytes(), StandardCharsets.UTF_8);
        } catch (Exception e) {
            Log.w(TAG, "about: " + asset + " not read", e);
            return "(not found)";
        }
    }

    /** A long text, scrolled, small enough to read a licence. */
    private static void text(Activity activity, String title, String body, Runnable back) {
        TextView view = new TextView(activity);
        view.setText(body);
        view.setTextIsSelectable(true);
        view.setTextSize(13);
        int pad = Math.round(20 * activity.getResources().getDisplayMetrics().density);
        view.setPadding(pad, pad / 2, pad, pad / 2);
        ScrollView scroll = new ScrollView(activity);
        scroll.addView(view);
        builder(activity).setTitle(title)
                .setView(scroll)
                .setPositiveButton("Back", (d, w) -> back.run())
                .setOnCancelListener(d -> back.run())
                .show();
    }
}
