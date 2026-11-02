package dev.daorecompandroid.duo;

import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;

import java.io.BufferedReader;
import java.io.FileReader;
import java.util.Arrays;
import java.util.Locale;
import java.util.Map;

/**
 * The performance numbers, the same for the deck's Perf tab and the overlay on the main
 * screen (the "Performance tab/overlay" option): the game's frames a second and the
 * screen's, the frame time, the 1% low, the power drawn, the battery and the GPU.
 *
 * <p>Fed the frame counts the native side keeps ({now ns, the game's swaps, the panel's
 * presents, then the game's last frame intervals in ns, oldest first - up to 1,000):
 * the rates over at least half a second; the frame time the average of the last second's
 * intervals; the 1% low the rate of the 99th-percentile interval over all the intervals
 * kept (about 16 s at 60). The power is the battery's: its current times its voltage
 * (BatteryManager), read at most once a second - on the charger the charger feeds the
 * device and the battery's own draw says nothing, so it says so.
 */
final class PerfStats {

    /** Not yet known. */
    static final double NONE = -1;

    double gameFps = NONE;
    double screenFps = NONE;
    /** The average of the last second's frame times, ms. */
    double frameMs = NONE;
    /** The slowest frame kept, ms. */
    double slowestMs = NONE;
    /** The rate of the 99th-percentile frame time over the frames kept. */
    double onePercentLow = NONE;
    /** Watts drawn from the battery, or NONE (on the charger, or unknown). */
    double watts = NONE;
    boolean charging;
    int batteryPercent = -1;
    double batteryCelsius = NONE;
    /** The GPU's busy percentage and clock, where the system lets an app read them. */
    String gpuBusy;
    long gpuMhz = -1;
    /** The intervals kept, ns, oldest first. */
    long[] intervals = new long[0];
    /** What the frames go through as the game runs (NativeBridge.deckRender). */
    String frameGeneration;
    String upscaler;
    String renderSize;
    String outputSize;

    private long lastNow;
    private long lastSwaps;
    private long lastPresents;
    private long lastSlowRead;

    /** New frame counts (DeckSource.frames / NativeBridge.deckFrames). */
    void update(Context context, long[] frames) {
        if (frames == null || frames.length < 3) {
            return;
        }
        long now = frames[0];
        // A first look, or the first after a while unseen (the tab hidden, the overlay
        // off): the rates start again rather than average over the time away.
        if (lastNow == 0 || now - lastNow > 3_000_000_000L) {
            gameFps = NONE;
            screenFps = NONE;
            lastNow = now;
            lastSwaps = frames[1];
            lastPresents = frames[2];
        } else if (now - lastNow >= 500_000_000L) {
            double seconds = (now - lastNow) / 1e9;
            gameFps = (frames[1] - lastSwaps) / seconds;
            screenFps = (frames[2] - lastPresents) / seconds;
            lastNow = now;
            lastSwaps = frames[1];
            lastPresents = frames[2];
        }
        intervals = Arrays.copyOfRange(frames, 3, frames.length);
        if (intervals.length > 0) {
            // The last second's frames, newest first.
            double sum = 0;
            int count = 0;
            for (int i = intervals.length - 1; i >= 0 && sum < 1e9; i--) {
                sum += intervals[i];
                count++;
            }
            frameMs = sum / count / 1e6;
            long[] sorted = intervals.clone();
            Arrays.sort(sorted);
            slowestMs = sorted[sorted.length - 1] / 1e6;
            // The 1% low wants at least a hundred frames to have a 1%.
            if (sorted.length >= 100) {
                long p99 = sorted[(int) Math.floor(sorted.length * 0.99) - 1];
                onePercentLow = p99 > 0 ? 1e9 / p99 : NONE;
            }
        }
        if (now - lastSlowRead > 1_000_000_000L) {
            lastSlowRead = now;
            readDevice(context);
        }
    }

    private void readDevice(Context context) {
        Intent battery = context.registerReceiver(null,
                new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        int millivolts = 0;
        if (battery != null) {
            int level = battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
            int scale = battery.getIntExtra(BatteryManager.EXTRA_SCALE, 100);
            batteryPercent = level < 0 ? -1 : level * 100 / Math.max(scale, 1);
            batteryCelsius = battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) / 10.0;
            charging = battery.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0;
            millivolts = battery.getIntExtra(BatteryManager.EXTRA_VOLTAGE, 0);
        }
        watts = NONE;
        BatteryManager manager = context.getSystemService(BatteryManager.class);
        if (manager != null && !charging && millivolts > 0) {
            int microamps = manager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW);
            if (microamps != Integer.MIN_VALUE && microamps != 0) {
                watts = Math.abs(microamps) / 1e6 * millivolts / 1e3;
            }
        }
        String busy = readLine("/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage");
        gpuBusy = busy != null ? busy.replace(" ", "") : null;
        String clock = readLine("/sys/class/kgsl/kgsl-3d0/gpuclk");
        gpuMhz = -1;
        if (clock != null) {
            try {
                gpuMhz = Long.parseLong(clock.trim()) / 1_000_000;
            } catch (NumberFormatException e) {
                gpuMhz = -1;
            }
        }
    }

    /** The "Render.*" lines (NativeBridge.deckRender, in the deck's snapshot too). */
    void render(Map<String, String> values) {
        frameGeneration = values.get("Render.FrameGeneration");
        upscaler = values.get("Render.Upscaler");
        renderSize = values.get("Render.Size");
        outputSize = values.get("Render.Output");
    }

    /** Whether frames are being generated: the screen's rate is then the total. */
    boolean generating() {
        return frameGeneration != null && !frameGeneration.startsWith("Off");
    }

    // ---- the words, the same on the deck and the overlay ----

    /** Frame generation, the upscaler and its quality, one line. */
    String pipeline() {
        return "Frame generation " + (frameGeneration != null ? frameGeneration : "–")
                + "   ·   Upscaling " + (upscaler != null ? upscaler : "–");
    }

    /** The rendered size and the size shown, one line. */
    String sizes() {
        return "Rendered at " + (renderSize != null ? renderSize : "–")
                + ", upscaled to " + (outputSize != null ? outputSize : "–");
    }

    static String rate(double fps) {
        return fps == NONE ? "–" : String.format(Locale.ROOT, "%.1f", fps);
    }

    String frameTime() {
        return frameMs == NONE ? "–" : String.format(Locale.ROOT, "%.1f ms", frameMs);
    }

    String low() {
        return onePercentLow == NONE ? "–" : String.format(Locale.ROOT, "%.1f", onePercentLow);
    }

    String power() {
        if (charging) {
            return "on the charger";
        }
        return watts == NONE ? "–" : String.format(Locale.ROOT, "%.1f W", watts);
    }

    /** The battery and the GPU, one line. */
    String device() {
        StringBuilder out = new StringBuilder();
        if (batteryPercent >= 0) {
            out.append(String.format(Locale.ROOT, "Battery %d%%, %.1f °C", batteryPercent,
                    batteryCelsius));
        }
        if (gpuBusy != null) {
            if (out.length() > 0) {
                out.append("   ·   ");
            }
            out.append("GPU ").append(gpuBusy).append(" busy");
            if (gpuMhz > 0) {
                out.append(" at ").append(gpuMhz).append(" MHz");
            }
        }
        return out.toString();
    }

    private static String readLine(String path) {
        try (BufferedReader reader = new BufferedReader(new FileReader(path))) {
            return reader.readLine();
        } catch (Exception e) {
            return null;
        }
    }
}
