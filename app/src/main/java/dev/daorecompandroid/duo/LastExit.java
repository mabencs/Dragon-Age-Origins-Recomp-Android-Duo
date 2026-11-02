package dev.daorecompandroid.duo;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * PLAN 10.7: why the app's last session ended, from Android's own record of its processes'
 * exits ({@link ActivityManager#getHistoricalProcessExitReasons}) - a crash in the game's
 * native code or in the app's Java, an ANR, Android closing it in the background for
 * memory, or the app's own end.
 *
 * <p>The app tags its process with what it is doing ({@link #mark}: {@code pre-launch},
 * {@code playing}, {@code held} while put away, {@code ending} just before it ends its own
 * process), and Android hands the tag back with the exit, so the app's own end - a kill
 * signal, as Android records it - is told apart from anyone else's. Each exit not yet
 * written that was neither the app's own end nor a stop from outside (a force stop,
 * swiping it away) goes to {@code files/exits/}: what Android says, and its trace when it
 * keeps one (an ANR's thread dump; a native crash's tombstone, Android's protobuf). The
 * launch screen tells how the last session ended when it was not as asked. Nothing leaves
 * the device.
 */
final class LastExit {

    private static final String TAG = "WardenExit";

    private LastExit() {
    }

    /** Tags the process with what the app is doing (Android keeps it with the exit). */
    static void mark(Context context, String state) {
        try {
            ActivityManager manager = context.getSystemService(ActivityManager.class);
            manager.setProcessStateSummary(state.getBytes(StandardCharsets.UTF_8));
        } catch (RuntimeException e) {
            Log.w(TAG, "cannot tag the process: " + e);
        }
    }

    /**
     * Writes the reports of exits not yet written and returns how the last session ended,
     * in words for the launch screen - "" when it ended as asked (or there is no record).
     */
    static String check(Context context) {
        List<ApplicationExitInfo> exits;
        try {
            ActivityManager manager = context.getSystemService(ActivityManager.class);
            exits = manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 0);
        } catch (RuntimeException e) {
            Log.w(TAG, "no exit records: " + e);
            return "";
        }
        File dir = new File(context.getFilesDir(), "exits");
        File seenFile = new File(dir, "seen");
        long seen = readLong(seenFile);
        long newest = seen;
        String last = null;
        // Newest first; the main process's newest record is the last session's.
        for (ApplicationExitInfo exit : exits) {
            boolean main = exit.getProcessName().equals(context.getPackageName());
            boolean unexpected = isUnexpected(exit);
            if (main && last == null) {
                last = unexpected ? describe(exit) : "";
            }
            if (exit.getTimestamp() > seen && unexpected) {
                write(dir, exit);
            }
            newest = Math.max(newest, exit.getTimestamp());
        }
        if (newest > seen) {
            dir.mkdirs();
            writeText(seenFile, Long.toString(newest));
        }
        if (last != null && !last.isEmpty()) {
            Log.i(TAG, "the last session: " + last);
        }
        return last == null ? "" : last;
    }

    private static String state(ApplicationExitInfo exit) {
        byte[] summary = exit.getProcessStateSummary();
        return summary == null ? "" : new String(summary, StandardCharsets.UTF_8);
    }

    private static boolean isUnexpected(ApplicationExitInfo exit) {
        switch (exit.getReason()) {
            case ApplicationExitInfo.REASON_EXIT_SELF:
            case ApplicationExitInfo.REASON_USER_REQUESTED:
            case ApplicationExitInfo.REASON_USER_STOPPED:
            case ApplicationExitInfo.REASON_DEPENDENCY_DIED:
            case ApplicationExitInfo.REASON_PERMISSION_CHANGE:
            case ApplicationExitInfo.REASON_PACKAGE_STATE_CHANGE:
            case ApplicationExitInfo.REASON_PACKAGE_UPDATED:
                return false;
            case ApplicationExitInfo.REASON_SIGNALED:
                return !state(exit).equals("ending");
            default:
                return true;
        }
    }

    private static String reasonName(int reason) {
        switch (reason) {
            case ApplicationExitInfo.REASON_SIGNALED: return "signaled";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "low-memory";
            case ApplicationExitInfo.REASON_CRASH: return "crash";
            case ApplicationExitInfo.REASON_CRASH_NATIVE: return "native-crash";
            case ApplicationExitInfo.REASON_ANR: return "anr";
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE: return "start-failure";
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE: return "resources";
            case ApplicationExitInfo.REASON_FREEZER: return "freezer";
            case ApplicationExitInfo.REASON_OTHER: return "other";
            default: return "reason-" + reason;
        }
    }

    /** The launch screen's words for an unexpected exit. */
    private static String describe(ApplicationExitInfo exit) {
        String when = new SimpleDateFormat("d MMM HH:mm", Locale.UK)
                .format(new Date(exit.getTimestamp()));
        String what;
        switch (exit.getReason()) {
            case ApplicationExitInfo.REASON_CRASH_NATIVE:
                what = "the game crashed (signal " + exit.getStatus() + ")";
                break;
            case ApplicationExitInfo.REASON_CRASH:
                what = "the app crashed";
                break;
            case ApplicationExitInfo.REASON_ANR:
                what = "the game stopped responding and Android closed it";
                break;
            case ApplicationExitInfo.REASON_LOW_MEMORY:
                what = "Android closed it to free memory";
                break;
            case ApplicationExitInfo.REASON_SIGNALED:
                what = "it was ended by signal " + exit.getStatus();
                break;
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE:
                what = "it failed to start";
                break;
            default:
                what = "Android ended it (" + reasonName(exit.getReason()) + ")";
                break;
        }
        String doing = state(exit);
        return "The last session ended " + when + ": " + what
                + (doing.equals("held") ? ", while put away" : doing.equals("playing")
                        ? ", in play" : "")
                + ". The report is in the app's files (exits).";
    }

    private static void write(File dir, ApplicationExitInfo exit) {
        dir.mkdirs();
        String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT)
                .format(new Date(exit.getTimestamp()));
        String process = exit.getProcessName().endsWith(":deck") ? "deck" : "game";
        String base = stamp + "-" + process + "-" + reasonName(exit.getReason());
        StringBuilder text = new StringBuilder();
        text.append("time        ").append(new Date(exit.getTimestamp())).append('\n')
                .append("process     ").append(exit.getProcessName())
                .append(" (pid ").append(exit.getPid()).append(")\n")
                .append("reason      ").append(reasonName(exit.getReason()))
                .append(" (").append(exit.getReason()).append(")\n")
                .append("status      ").append(exit.getStatus()).append('\n')
                .append("importance  ").append(exit.getImportance()).append('\n')
                .append("memory      pss ").append(exit.getPss() / 1024).append(" MB, rss ")
                .append(exit.getRss() / 1024).append(" MB\n")
                .append("app state   ").append(state(exit)).append('\n')
                .append("description ").append(exit.getDescription()).append('\n');
        String traceName = null;
        try (InputStream trace = exit.getTraceInputStream()) {
            if (trace != null) {
                traceName = base + (exit.getReason() == ApplicationExitInfo.REASON_ANR
                        ? ".anr.txt" : ".tombstone.pb");
                try (OutputStream out = new FileOutputStream(new File(dir, traceName))) {
                    byte[] buffer = new byte[65536];
                    int n;
                    while ((n = trace.read(buffer)) > 0) {
                        out.write(buffer, 0, n);
                    }
                }
            }
        } catch (IOException e) {
            text.append("trace       not saved: ").append(e).append('\n');
        }
        text.append("trace       ").append(traceName == null ? "none kept" : traceName)
                .append('\n');
        writeText(new File(dir, base + ".txt"), text.toString());
        Log.i(TAG, "exit report " + base + ".txt" + (traceName == null ? "" : " + " + traceName));
    }

    private static long readLong(File file) {
        try (InputStream in = new java.io.FileInputStream(file)) {
            byte[] bytes = new byte[32];
            int n = in.read(bytes);
            return n > 0 ? Long.parseLong(new String(bytes, 0, n, StandardCharsets.UTF_8).trim())
                    : 0;
        } catch (IOException | NumberFormatException e) {
            return 0;
        }
    }

    private static void writeText(File file, String text) {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            Log.w(TAG, "cannot write " + file + ": " + e);
        }
    }
}
