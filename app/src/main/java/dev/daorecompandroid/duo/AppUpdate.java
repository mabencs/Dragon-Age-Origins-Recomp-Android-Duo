package dev.daorecompandroid.duo;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageInstaller;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.RelativeSizeSpan;
import android.text.style.StyleSpan;
import android.graphics.Typeface;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * PLAN 13.1: the launcher's update check, the app's only network use (the game itself stays
 * offline). At each launch, off the main thread, it reads the repository's releases - the list,
 * as GitHub's "latest" leaves pre-releases out - and offers the newest above this version.
 *
 * <p>A release can install itself here, or be manual: one whose notes hold {@link #MANUAL_MARK}
 * (an update that asks more of the player than an install) or that has no APK. Those, and every
 * build but the published one, which cannot update over itself, only open the release's page.
 * The APK must match the size and SHA-256 GitHub gives, and Android's installer asks the player
 * and refuses another signing key or a lower version.
 */
final class AppUpdate {

    private static final String TAG = "Warden";

    /** In a release's notes: not to be installed from the launcher. Not shown on GitHub. */
    static final String MANUAL_MARK = "<!-- manual-update -->";
    /** The published build: the one the releases' APKs update in place. */
    private static final String PUBLISHED_PACKAGE = "prod.daorecompandroid.duo";
    private static final int CONNECT_TIMEOUT_MS = 8000;
    private static final int READ_TIMEOUT_MS = 20000;
    private static final Pattern VERSION = Pattern.compile("(\\d+)\\.(\\d+)\\.(\\d+)");

    static final int NOTHING = 0;     // no news: no row
    static final int AVAILABLE = 1;   // Download & install
    static final int MANUAL = 2;      // its page only
    static final int DOWNLOADING = 3;
    static final int INSTALLING = 4;  // with Android's installer
    static final int FAILED = 5;      // Download & install again

    /** Told on the main thread when anything here changes. */
    interface Listener {
        void updateChanged();
    }

    private final Context context;
    private final Listener listener;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final String action;

    private volatile int state = NOTHING;
    private volatile int percent;
    private volatile boolean cancelled;
    private String failure = "";
    private String version = "";
    private String page = "";
    private String apkUrl = "";
    private String sha256 = "";
    private long size;
    private boolean selfInstall;
    private String whyManual = "";
    /** The release notes of every version above this one, newest first (the owner, 2026-11-03). */
    private CharSequence notes = "";
    private BroadcastReceiver installed;
    /** Android's dialog shown, and the session it is about. */
    private volatile boolean asked;
    private volatile int sessionId = -1;

    AppUpdate(Context context, Listener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
        action = context.getPackageName() + ".APP_UPDATE";
    }

    int state() {
        return state;
    }

    /** The launcher's row, while there is one. */
    String rowName() {
        return "Update available: " + version;
    }

    String rowValue() {
        switch (state) {
            case AVAILABLE:
                return "Download & install";
            case MANUAL:
                return "Manual update";
            case DOWNLOADING:
                return "Downloading " + percent + "%";
            case INSTALLING:
                return "Installing…";
            case FAILED:
                return "Try again";
            default:
                return "";
        }
    }

    String rowDetail() {
        long megabytes = size / (1024 * 1024);
        switch (state) {
            case AVAILABLE:
                return "Downloads " + megabytes + " MB and installs " + version + " over this"
                        + " version. Your saves, game data and settings stay. Android asks you to"
                        + " confirm; open the app again after it installs.";
            case MANUAL:
                return whyManual + " Select to open the release's page.";
            case DOWNLOADING:
                return "Downloading " + version + ". Launching the game cancels it.";
            case INSTALLING:
                return "Confirm the install in Android's dialog. Open the app again after it"
                        + " installs.";
            case FAILED:
                return "The update failed: " + failure + " Nothing was changed.";
            default:
                return "";
        }
    }

    /** The launcher opened: the check, once, off this thread. */
    void check() {
        if (BuildConfig.SOURCE_URL.isEmpty()) {
            return;
        }
        Thread thread = new Thread(() -> {
            try {
                read(feed());
            } catch (Exception e) {
                // No network is ordinary: the launcher simply has no row.
                Log.i(TAG, "update check: none (" + e + ")");
            }
        }, "update-check");
        thread.setDaemon(true);
        thread.start();
    }

    /** What the update brings: the notes of each release above this version, newest first. */
    CharSequence notes() {
        return notes;
    }

    /** The version offered ("0.1.3-alpha"). */
    String version() {
        return version;
    }

    /** The row chosen. */
    void choose() {
        if (state == MANUAL) {
            openPage();
        } else if (state == AVAILABLE || state == FAILED) {
            download();
        }
    }

    /**
     * The launcher in front again. Android's first dialog, the one that sends the player to
     * allow installs from this app, says nothing to the session when it is cancelled: with
     * that dialog gone and no answer, the row would wait for ever (BUG-111).
     *
     * BUG-114: but the launcher is also in front again the moment the player taps Install,
     * while Android installs - abandoning the session here then cancelled every real
     * install (0.1.3 to 0.1.4, three times). So nothing is abandoned on the way back: if no
     * answer has come after a while and Android is not installing, the row offers Try again,
     * and only that tap gives the old session up (download).
     */
    void resumed() {
        if (state == INSTALLING && asked) {
            asked = false;
            final int id = sessionId;
            Log.i(TAG, "update: back from Android's installer - waiting for its answer");
            main.postDelayed(() -> {
                if (state != INSTALLING || sessionId != id || id == -1) {
                    return;  // answered, or another try since
                }
                PackageInstaller.SessionInfo info =
                        context.getPackageManager().getPackageInstaller().getSessionInfo(id);
                // The APK written in is 0.8 of a session's progress; Android's own work, once
                // the player confirms, the rest (run 681: a cancelled dialog left 0.8).
                if (info != null && info.getProgress() > 0.81f) {
                    Log.i(TAG, "update: Android is installing (" + info.getProgress() + ")");
                    return;  // it ends with this process, or with an answer
                }
                Log.i(TAG, "update: no answer from Android's installer - Try again offered");
                failure = "Android's installer was closed without installing.";
                set(FAILED);
            }, UNANSWERED_MS);
        }
    }

    /** How long after the installer's dialog an answer may take before Try again (BUG-114). */
    private static final long UNANSWERED_MS = 30_000;

    private void abandon() {
        int id = sessionId;
        sessionId = -1;
        if (id != -1) {
            try {
                context.getPackageManager().getPackageInstaller().abandonSession(id);
            } catch (RuntimeException e) {
                Log.i(TAG, "update: the session is gone (" + e + ")");
            }
        }
    }

    /** The game is launching: nothing may install under it. */
    void cancel() {
        cancelled = true;
        asked = false;
        abandon();
        if (state == DOWNLOADING || state == INSTALLING) {
            set(selfInstall ? AVAILABLE : MANUAL);
        }
    }

    private String feed() throws IOException {
        // A development build can be given a release list to try the row with (TESTING.md).
        if (BuildConfig.DEBUG) {
            File test = new File(context.getFilesDir(), "update-feed");
            if (test.isFile()) {
                return new String(Files.readAllBytes(test.toPath()), StandardCharsets.UTF_8);
            }
        }
        Uri source = Uri.parse(BuildConfig.SOURCE_URL);
        URL url = new URL("https://api.github.com/repos" + source.getPath()
                + "/releases?per_page=30");
        HttpURLConnection connection = connect(url);
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        try (InputStream in = connection.getInputStream()) {
            return new String(in.readAllBytes(), StandardCharsets.UTF_8);
        } finally {
            connection.disconnect();
        }
    }

    private static HttpURLConnection connect(URL url) throws IOException {
        if (!"https".equals(url.getProtocol())) {
            throw new IOException("not https: " + url);
        }
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
        connection.setReadTimeout(READ_TIMEOUT_MS);
        connection.setRequestProperty("User-Agent", "dao-recomp-android");
        return connection;
    }

    private void read(String json) throws Exception {
        JSONArray releases = new JSONArray(json);
        JSONObject newest = null;
        int[] newestVersion = parse(BuildConfig.VERSION_NAME);
        if (newestVersion == null) {
            return;
        }
        for (int i = 0; i < releases.length(); i++) {
            JSONObject release = releases.getJSONObject(i);
            int[] number = parse(release.optString("tag_name"));
            if (!release.optBoolean("draft") && number != null
                    && compare(number, newestVersion) > 0) {
                newest = release;
                newestVersion = number;
            }
        }
        if (newest == null) {
            Log.i(TAG, "update check: " + BuildConfig.VERSION_NAME + " is the newest");
            return;
        }
        // The notes of every release between this version and the newest, so a player who
        // skipped one still reads what it changed.
        int[] current = parse(BuildConfig.VERSION_NAME);
        List<JSONObject> newer = new ArrayList<>();
        for (int i = 0; i < releases.length(); i++) {
            JSONObject release = releases.getJSONObject(i);
            int[] number = parse(release.optString("tag_name"));
            if (!release.optBoolean("draft") && number != null && compare(number, current) > 0
                    && compare(number, newestVersion) <= 0) {
                newer.add(release);
            }
        }
        newer.sort((a, b) -> compare(parse(b.optString("tag_name")),
                parse(a.optString("tag_name"))));
        SpannableStringBuilder text = new SpannableStringBuilder();
        for (JSONObject release : newer) {
            String title = release.optString("name");
            if (title.isEmpty()) {
                title = release.optString("tag_name");
            }
            if (text.length() > 0) {
                text.append("\n\n");
            }
            int start = text.length();
            text.append(title);
            text.setSpan(new StyleSpan(Typeface.BOLD), start, text.length(),
                    Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            text.setSpan(new RelativeSizeSpan(1.25f), start, text.length(),
                    Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            text.append("\n");
            text.append(formatNotes(release.optString("body")));
        }
        final CharSequence allNotes = text;
        JSONObject apk = null;
        JSONArray assets = newest.optJSONArray("assets");
        for (int i = 0; assets != null && i < assets.length(); i++) {
            JSONObject asset = assets.getJSONObject(i);
            if (asset.optString("name").endsWith(".apk")) {
                apk = asset;
                break;
            }
        }
        boolean marked = newest.optString("body").toLowerCase(Locale.ROOT).contains(MANUAL_MARK);
        boolean published = PUBLISHED_PACKAGE.equals(context.getPackageName());
        String digest = apk == null ? "" : apk.optString("digest");
        String why = marked ? "This update has to be installed by hand."
                : !published ? "A development build cannot update itself."
                : apk == null ? "This release has no APK to install."
                : !digest.startsWith("sha256:") ? "This release has no checksum to verify."
                : "";
        String tag = newest.getString("tag_name");
        String name = tag.startsWith("v") ? tag.substring(1) : tag;
        String html = newest.optString("html_url");
        String url = apk == null ? "" : apk.optString("browser_download_url");
        String sha = digest.startsWith("sha256:") ? digest.substring(7) : "";
        long length = apk == null ? 0 : apk.optLong("size");
        main.post(() -> {
            version = name;
            page = html;
            apkUrl = url;
            sha256 = sha;
            size = length;
            whyManual = why;
            notes = allNotes;
            selfInstall = why.isEmpty();
            Log.i(TAG, "update check: " + name + " available"
                    + (selfInstall ? "" : " - manual: " + why));
            set(selfInstall ? AVAILABLE : MANUAL);
        });
    }

    /**
     * A release's notes as GitHub's markdown has them, made plain for a dialog: headings
     * bold, list items with bullets, emphasis, code marks and links' addresses taken off, the
     * comments (the manual mark) left out, and the Installing section, which is for the
     * release's page (the APK below, its checksum), not for the launcher installing it.
     */
    static CharSequence formatNotes(String markdown) {
        String body = markdown.replace("\r", "").replaceAll("(?s)<!--.*?-->", "").trim();
        SpannableStringBuilder out = new SpannableStringBuilder();
        boolean blank = false;
        boolean afterHeading = false;
        boolean skipping = false;
        for (String line : body.split("\n")) {
            String trimmed = line.trim();
            if (trimmed.isEmpty()) {
                blank = out.length() > 0;
                continue;
            }
            boolean heading = trimmed.startsWith("#");
            if (heading) {
                skipping = trimmed.replaceFirst("^#+\\s*", "").equalsIgnoreCase("Installing");
            }
            if (skipping) {
                continue;
            }
            boolean item = trimmed.startsWith("- ") || trimmed.startsWith("* ");
            boolean continued = !heading && !item && line.startsWith("  ") && !blank;
            String content = heading ? trimmed.replaceFirst("^#+\\s*", "")
                    : item ? trimmed.substring(2) : trimmed;
            content = content.replaceAll("\\[([^\\]]*)\\]\\([^)]*\\)", "$1")
                    .replace("**", "").replace("`", "");
            if (out.length() > 0) {
                // A heading's first line right under it; a blank line between paragraphs.
                out.append(continued ? " " : afterHeading ? "\n" : blank || heading ? "\n\n" : "\n");
            }
            blank = false;
            afterHeading = heading;
            int start = out.length();
            out.append(item ? "•  " + content : content);
            if (heading) {
                out.setSpan(new StyleSpan(Typeface.BOLD), start, out.length(),
                        Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            }
        }
        return out;
    }

    /** The first three numbers in the text: "v0.1.2-alpha", "0.1.1-alpha-debug". */
    static int[] parse(String text) {
        Matcher matcher = VERSION.matcher(text);
        if (!matcher.find()) {
            return null;
        }
        return new int[] {Integer.parseInt(matcher.group(1)), Integer.parseInt(matcher.group(2)),
                Integer.parseInt(matcher.group(3))};
    }

    static int compare(int[] a, int[] b) {
        for (int i = 0; i < 3; i++) {
            if (a[i] != b[i]) {
                return Integer.compare(a[i], b[i]);
            }
        }
        return 0;
    }

    private void openPage() {
        try {
            context.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(page))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        } catch (RuntimeException e) {
            Log.w(TAG, "update: no browser for " + page, e);
        }
    }

    private void set(int newState) {
        state = newState;
        listener.updateChanged();
    }

    private void fail(String why) {
        Log.w(TAG, "update: failed - " + why);
        main.post(() -> {
            if (!cancelled) {
                failure = why;
                set(FAILED);
            }
        });
    }

    private void download() {
        abandon();  // an earlier try's session, if Android never answered it (BUG-114)
        cancelled = false;
        percent = 0;
        set(DOWNLOADING);
        Thread thread = new Thread(() -> {
            File apk = new File(context.getCacheDir(), "update.apk");
            try {
                if (fetch(apk) && !cancelled) {
                    install(apk);
                }
            } catch (Exception e) {
                fail("the download stopped.");
                Log.w(TAG, "update: download", e);
            } finally {
                // PackageInstaller holds its own copy by now.
                apk.delete();
            }
        }, "update-download");
        thread.setDaemon(true);
        thread.start();
    }

    /** The APK to `file`, its size and SHA-256 as GitHub gave them. False when cancelled. */
    private boolean fetch(File file) throws Exception {
        HttpURLConnection connection = connect(new URL(apkUrl));
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        long done = 0;
        try (InputStream in = connection.getInputStream();
                OutputStream out = new FileOutputStream(file)) {
            byte[] buffer = new byte[1 << 16];
            int count;
            while ((count = in.read(buffer)) > 0) {
                if (cancelled) {
                    return false;
                }
                out.write(buffer, 0, count);
                digest.update(buffer, 0, count);
                done += count;
                int now = size > 0 ? (int) (done * 100 / size) : 0;
                if (now != percent) {
                    percent = now;
                    main.post(listener::updateChanged);
                }
            }
        } finally {
            connection.disconnect();
        }
        StringBuilder hex = new StringBuilder();
        for (byte b : digest.digest()) {
            hex.append(String.format(Locale.ROOT, "%02x", b));
        }
        if (done != size || !hex.toString().equalsIgnoreCase(sha256)) {
            throw new IOException("the download does not match: " + done + " bytes, " + hex);
        }
        Log.i(TAG, "update: " + done + " bytes verified");
        return true;
    }

    /** Hands the verified APK to Android's installer, which asks the player. */
    private void install(File apk) throws Exception {
        main.post(() -> {
            if (!cancelled) {
                set(INSTALLING);
            }
        });
        PackageInstaller installer = context.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(
                PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(context.getPackageName());
        params.setSize(apk.length());
        int id = installer.createSession(params);
        sessionId = id;
        try (PackageInstaller.Session session = installer.openSession(id)) {
            try (OutputStream out = session.openWrite("base.apk", 0, apk.length());
                    InputStream in = new FileInputStream(apk)) {
                in.transferTo(out);
                session.fsync(out);
            }
            listen();
            Intent result = new Intent(action).setPackage(context.getPackageName());
            android.app.PendingIntent pending = android.app.PendingIntent.getBroadcast(context, id,
                    result, android.app.PendingIntent.FLAG_UPDATE_CURRENT
                            | android.app.PendingIntent.FLAG_MUTABLE);
            session.commit(pending.getIntentSender());
        }
    }

    /** The installer's answers: its dialog to show, or how it ended. */
    private void listen() {
        if (installed != null) {
            return;
        }
        installed = new BroadcastReceiver() {
            @Override
            public void onReceive(Context ignored, Intent intent) {
                // BUG-114: only the current try's session. Try again gives the last one up,
                // and its "abandoned" answer came in during the new download (run 681).
                int id = intent.getIntExtra(PackageInstaller.EXTRA_SESSION_ID, -1);
                if (id != sessionId) {
                    Log.i(TAG, "update: an earlier session's answer ignored (" + id + ")");
                    return;
                }
                int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS,
                        PackageInstaller.STATUS_FAILURE);
                if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                    Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent.class);
                    if (confirm != null && !cancelled) {
                        asked = true;
                        context.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                    }
                } else if (status != PackageInstaller.STATUS_SUCCESS) {
                    asked = false;
                    String message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
                    Log.w(TAG, "update: the installer says " + status + " " + message);
                    fail(status == PackageInstaller.STATUS_FAILURE_ABORTED
                            ? "the install was cancelled." : "Android refused the install.");
                }
            }
        };
        context.registerReceiver(installed, new IntentFilter(action),
                Context.RECEIVER_NOT_EXPORTED);
    }
}
