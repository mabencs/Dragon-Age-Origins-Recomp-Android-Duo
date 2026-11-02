package dev.daorecompandroid.duo;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.util.Locale;

/**
 * The performance overlay on the main screen (the "Performance tab/overlay" option): the
 * same numbers as the second screen's Perf tab (PerfStats) - the game's own frames a
 * second and the screen's total with generated frames, frame time, 1% low, power drawn,
 * the frame generation, the upscaler and the rendered and shown sizes - in a small box
 * at the top centre, where the game's HUD leaves room
 * (its portraits top left, its map top right). Shown while the option is on and the deck
 * is not showing on the second screen; checked twice a second, so the option is taken at
 * once. The numbers come from the game's own process (NativeBridge.deckFrames), never
 * waiting on the game.
 */
final class PerfOverlay {

    private static final long TICK_MS = 500;

    private final Context context;
    private final File filesDir;
    private final LinearLayout box;
    private final TextView numbers;
    private final TextView pipeline;
    private final TextView device;
    private final PerfStats stats = new PerfStats();
    private final Handler ui = new Handler(Looper.getMainLooper());
    /** The second screen's deck is showing: the numbers are its Perf tab's then. */
    private volatile boolean deckShowing;
    private boolean running;

    PerfOverlay(Context context, File filesDir, FrameLayout root) {
        this.context = context;
        this.filesDir = filesDir;
        box = new LinearLayout(context);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER_HORIZONTAL);
        int pad = dp(5);
        box.setPadding(pad + dp(4), pad / 2, pad + dp(4), pad / 2);
        GradientDrawable back = new GradientDrawable();
        back.setColor(0xB80E0B09);
        back.setStroke(dp(1), 0xFF8C6D1F);
        back.setCornerRadius(dp(4));
        box.setBackground(back);
        numbers = new TextView(context);
        numbers.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        numbers.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        numbers.setTextColor(0xFFC9A227);
        numbers.setFontFeatureSettings("tnum");
        box.addView(numbers);
        pipeline = new TextView(context);
        pipeline.setTypeface(Typeface.SERIF);
        pipeline.setTextSize(TypedValue.COMPLEX_UNIT_SP, 8.5f);
        pipeline.setTextColor(0xFFE8DCC0);
        pipeline.setGravity(Gravity.CENTER_HORIZONTAL);
        box.addView(pipeline);
        device = new TextView(context);
        device.setTypeface(Typeface.SERIF);
        device.setTextSize(TypedValue.COMPLEX_UNIT_SP, 8);
        device.setGravity(Gravity.CENTER_HORIZONTAL);
        device.setTextColor(0xFFD8CDB5);
        device.setFontFeatureSettings("tnum");
        box.addView(device);
        box.setVisibility(View.GONE);
        // Never takes touches or focus: the game's surface keeps them.
        box.setFocusable(false);
        box.setClickable(false);
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        params.topMargin = dp(6);
        root.addView(box, params);
    }

    /** Whether the deck shows on the second screen (it then carries the numbers). */
    void setDeckShowing(boolean showing) {
        deckShowing = showing;
    }

    /** Starts checking the option and, while wanted, updating the numbers. */
    void start() {
        if (running) {
            return;
        }
        running = true;
        ui.post(tick);
    }

    void stop() {
        running = false;
        ui.removeCallbacks(tick);
        box.setVisibility(View.GONE);
    }

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (!running) {
                return;
            }
            boolean wanted = "on".equals(Options.read(filesDir, Options.byFile("perf")))
                    && !deckShowing;
            if (wanted) {
                stats.update(context, NativeBridge.deckFrames());
                stats.render(DeckSource.parse(NativeBridge.deckRender()));
                numbers.setText(String.format(Locale.ROOT,
                        "Native %s fps   ·   Total %s fps   ·   %s   ·   1%% low %s   ·   %s",
                        PerfStats.rate(stats.gameFps), PerfStats.rate(stats.screenFps),
                        stats.frameTime(), stats.low(), stats.power()));
                pipeline.setText(stats.pipeline() + "\n" + stats.sizes());
                device.setText(stats.device());
            }
            box.setVisibility(wanted ? View.VISIBLE : View.GONE);
            ui.postDelayed(this, TICK_MS);
        }
    };

    private int dp(float value) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
                context.getResources().getDisplayMetrics()));
    }
}
