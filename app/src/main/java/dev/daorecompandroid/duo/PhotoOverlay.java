package dev.daorecompandroid.duo;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Environment;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.provider.MediaStore;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.PixelCopy;
import android.view.SurfaceView;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.io.File;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.Map;

/**
 * PLAN 9.20, photo mode's overlay on the main screen (warden_photo_mode.cc runs the
 * camera): while photo mode is on, a bar of hints at the bottom - the buttons named as
 * the Button prompts option draws them - and, when the game's A is pressed, the game's
 * picture saved as a PNG in Downloads/Dragon Age Origins (MediaStore: no permission),
 * named by the area and the time. The picture is the game's surface alone (PixelCopy of
 * the SurfaceView): the app's own overlays - these hints, the on-screen controller, the
 * performance numbers - are not in it.
 */
final class PhotoOverlay {

    private static final String TAG = "Warden";
    private static final long TICK_MS = 100;
    private static final long SAVED_MS = 3000;

    private final Context context;
    private final File filesDir;
    private final SurfaceView surface;
    private final TextView bar;
    private final TextView saved;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private HandlerThread saveThread;
    private Handler save;
    private boolean running;
    private int photosSeen = -1;
    private long savedUntil;
    private String[] names;

    PhotoOverlay(Context context, File filesDir, SurfaceView surface, FrameLayout root) {
        this.context = context;
        this.filesDir = filesDir;
        this.surface = surface;
        bar = new TextView(context);
        bar.setTypeface(Typeface.SERIF);
        bar.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        bar.setTextColor(0xFFE8DCC0);
        bar.setGravity(Gravity.CENTER);
        int pad = dp(8);
        bar.setPadding(pad * 2, pad / 2, pad * 2, pad / 2);
        GradientDrawable back = new GradientDrawable();
        back.setColor(0xB80E0B09);
        back.setStroke(dp(1), 0xFF8C6D1F);
        back.setCornerRadius(dp(4));
        bar.setBackground(back);
        bar.setVisibility(View.GONE);
        FrameLayout.LayoutParams barParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        barParams.bottomMargin = dp(10);
        root.addView(bar, barParams);
        saved = new TextView(context);
        saved.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        saved.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        saved.setTextColor(0xFFC9A227);
        saved.setPadding(pad * 2, pad / 2, pad * 2, pad / 2);
        GradientDrawable savedBack = new GradientDrawable();
        savedBack.setColor(0xB80E0B09);
        savedBack.setCornerRadius(dp(4));
        saved.setBackground(savedBack);
        saved.setVisibility(View.GONE);
        FrameLayout.LayoutParams savedParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        savedParams.topMargin = dp(60);
        root.addView(saved, savedParams);
    }

    void start() {
        if (running) {
            return;
        }
        running = true;
        saveThread = new HandlerThread("photo-save");
        saveThread.start();
        save = new Handler(saveThread.getLooper());
        // The buttons by their places, as the game's prompts name them (bottom, right,
        // left, top).
        String glyphs = Options.read(filesDir, Options.byFile("glyphs"));
        // With the on-screen controller, its buttons are Xbox's whatever the prompts say;
        // the hints then go to the top, clear of its Back and Start.
        boolean touch = TouchControls.enabled(filesDir);
        FrameLayout.LayoutParams barParams = (FrameLayout.LayoutParams) bar.getLayoutParams();
        barParams.gravity = (touch ? Gravity.TOP : Gravity.BOTTOM) | Gravity.CENTER_HORIZONTAL;
        barParams.topMargin = touch ? dp(10) : 0;
        barParams.bottomMargin = touch ? 0 : dp(10);
        bar.setLayoutParams(barParams);
        if (touch) {
            names = new String[] {"A", "B", "X", "Y", "Back", "LT", "RT"};
        } else if ("nintendo".equals(glyphs)) {
            names = new String[] {"B", "A", "Y", "X", "−", "ZL", "ZR"};
        } else if ("playstation".equals(glyphs)) {
            names = new String[] {"✕", "○", "□", "△", "Select", "L2", "R2"};
        } else {
            names = new String[] {"A", "B", "X", "Y", "Back", "LT", "RT"};
        }
        ui.post(tick);
    }

    void stop() {
        running = false;
        ui.removeCallbacks(tick);
        bar.setVisibility(View.GONE);
        saved.setVisibility(View.GONE);
        if (saveThread != null) {
            saveThread.quitSafely();
            saveThread = null;
        }
    }

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (!running) {
                return;
            }
            float[] state = NativeBridge.photoState();
            boolean on = state != null && state[0] > 0.5f;
            int photos = state != null ? Math.round(state[5]) : 0;
            if (photosSeen < 0) {
                photosSeen = photos;
            }
            if (on && photos > photosSeen) {
                takePhoto();
            }
            photosSeen = photos;
            if (on && state[4] > 0.5f) {
                bar.setText(String.format(Locale.ROOT,
                        "%s photo   ·   %s leave   ·   %s speed ×%s   ·   %s hide these   ·   "
                                + "%s reset   ·   D-pad ↑↓ zoom %d°   ←→ roll %d°   ·   %s %s down, up",
                        names[0], names[1], names[2], trim(state[3]), names[3], names[4],
                        Math.round(state[1]), Math.round(state[2]), names[5], names[6]));
                bar.setVisibility(View.VISIBLE);
            } else {
                bar.setVisibility(View.GONE);
            }
            saved.setVisibility(System.currentTimeMillis() < savedUntil ? View.VISIBLE : View.GONE);
            ui.postDelayed(this, TICK_MS);
        }
    };

    private static String trim(float speed) {
        return speed == Math.round(speed) ? String.valueOf(Math.round(speed))
                : String.valueOf(speed);
    }

    /** The game's surface as it is now, saved on the save thread. */
    private void takePhoto() {
        int width = surface.getWidth();
        int height = surface.getHeight();
        if (width <= 0 || height <= 0 || !surface.getHolder().getSurface().isValid()) {
            return;
        }
        Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        String area = areaName();
        PixelCopy.request(surface, bitmap, result -> {
            if (result != PixelCopy.SUCCESS) {
                Log.w(TAG, "photo: the copy failed (" + result + ")");
                bitmap.recycle();
                return;
            }
            Handler handler = save;
            if (handler != null) {
                handler.post(() -> savePhoto(bitmap, area));
            }
        }, ui);
    }

    private void savePhoto(Bitmap bitmap, String area) {
        String name = "Dragon Age - " + area + " - "
                + new SimpleDateFormat("yyyy-MM-dd HH.mm.ss", Locale.ROOT).format(new Date())
                + ".png";
        String folder = Environment.DIRECTORY_DOWNLOADS + "/Dragon Age Origins";
        ContentValues values = new ContentValues();
        values.put(MediaStore.Downloads.DISPLAY_NAME, name);
        values.put(MediaStore.Downloads.MIME_TYPE, "image/png");
        values.put(MediaStore.Downloads.RELATIVE_PATH, folder);
        values.put(MediaStore.Downloads.IS_PENDING, 1);
        ContentResolver resolver = context.getContentResolver();
        Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        boolean ok = false;
        if (uri != null) {
            try (OutputStream out = resolver.openOutputStream(uri)) {
                ok = out != null && bitmap.compress(Bitmap.CompressFormat.PNG, 100, out);
            } catch (Exception e) {
                Log.w(TAG, "photo: not saved", e);
            }
            ContentValues done = new ContentValues();
            done.put(MediaStore.Downloads.IS_PENDING, 0);
            resolver.update(uri, done, null, null);
            if (!ok) {
                resolver.delete(uri, null, null);
            }
        }
        bitmap.recycle();
        final boolean savedOk = ok;
        Log.i(TAG, "photo: " + (ok ? "saved to " + folder + "/" + name : "not saved"));
        ui.post(() -> {
            saved.setText(savedOk ? "Photo saved to Downloads/Dragon Age Origins"
                    : "The photo could not be saved");
            savedUntil = System.currentTimeMillis() + SAVED_MS;
            saved.setVisibility(View.VISIBLE);
        });
    }

    /** The area's name from the state mirror, for the file's: its map's, else none. */
    private static String areaName() {
        Map<String, String> values = DeckSource.parse(NativeBridge.deckSnapshot());
        String area = values.get("Area.Name");
        if (area == null || area.isEmpty()) {
            area = values.get("Area.Minimap");
        }
        if (area == null || area.isEmpty()) {
            return "Thedas";
        }
        String clean = area.replaceAll("[^A-Za-z0-9 _'-]", " ").trim();
        return clean.isEmpty() ? "Thedas" : clean.length() > 48 ? clean.substring(0, 48) : clean;
    }

    private int dp(float value) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
                context.getResources().getDisplayMetrics()));
    }
}
