package dev.daorecompandroid.duo;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.io.File;
import java.util.function.BooleanSupplier;

/**
 * PLAN 10.17, the on-screen controller (the owner, 2026-11-01): an Xbox controller drawn
 * over the game and played by touch, for the "On-screen controller" option. Its state
 * goes to the native side in the 360's terms (NativeBridge.touchPad) and is merged into
 * the first pad as the game polls it, so a held controller and the touch work together.
 *
 * <p>Laid out by the screen's height, around the game's HUD: the left stick floats where
 * the left thumb lands in the lower left, the D-pad beside it, the right stick floats in
 * the lower middle-right, A B X Y on the right above the HUD's quick slots, the bumpers
 * and triggers on the edges between the portraits and the map, Back and Start at the
 * bottom centre, and the options panel's button in the top left corner. Every finger is
 * its own control (multi-touch): a button stays held until its finger lifts; the D-pad
 * follows its finger across its directions.
 */
final class TouchControls extends View {

    private static final long CHECK_MS = 500;

    /** The 360's button bits (warden_input.h). */
    private static final int DPAD_UP = 0x0001;
    private static final int DPAD_DOWN = 0x0002;
    private static final int DPAD_LEFT = 0x0004;
    private static final int DPAD_RIGHT = 0x0008;
    private static final int START = 0x0010;
    private static final int BACK = 0x0020;
    private static final int LEFT_THUMB = 0x0040;
    private static final int RIGHT_THUMB = 0x0080;
    private static final int LEFT_SHOULDER = 0x0100;
    private static final int RIGHT_SHOULDER = 0x0200;
    private static final int A = 0x1000;
    private static final int B = 0x2000;
    private static final int X = 0x4000;
    private static final int Y = 0x8000;
    /** Not buttons: the triggers and the options panel, handled apart. */
    private static final int LEFT_TRIGGER = 0x10000;
    private static final int RIGHT_TRIGGER = 0x20000;
    private static final int MENU = 0x40000;

    /** A round or pill-shaped button: its centre and size as fractions of the height. */
    private static final class Button {
        final int bit;
        final String label;
        final int colour;
        final float cx;  // fraction of the width
        final float cy;  // fraction of the height
        final float radius;  // fraction of the height
        final boolean pill;
        float x;
        float y;
        float r;

        Button(int bit, String label, int colour, float cx, float cy, float radius,
               boolean pill) {
            this.bit = bit;
            this.label = label;
            this.colour = colour;
            this.cx = cx;
            this.cy = cy;
            this.radius = radius;
            this.pill = pill;
        }

        boolean hit(float px, float py) {
            float dx = Math.abs(px - x) / (pill ? 1.9f : 1f);
            float dy = py - y;
            // A little larger than drawn: thumbs are wide.
            return dx * dx + dy * dy <= (r * 1.25f) * (r * 1.25f);
        }
    }

    private static final int GOLD = 0xFFC9A227;
    private final Button[] buttons = {
        // Xbox's colours, Xbox's places.
        new Button(A, "A", 0xFF5DBB4C, 0.885f, 0.66f, 0.068f, false),
        new Button(B, "B", 0xFFD9443A, 0.945f, 0.54f, 0.068f, false),
        new Button(X, "X", 0xFF3C7FD9, 0.825f, 0.54f, 0.068f, false),
        new Button(Y, "Y", 0xFFE8C33A, 0.885f, 0.42f, 0.068f, false),
        new Button(LEFT_TRIGGER, "LT", GOLD, 0.035f, 0.30f, 0.055f, false),
        new Button(LEFT_SHOULDER, "LB", GOLD, 0.035f, 0.44f, 0.055f, false),
        new Button(RIGHT_TRIGGER, "RT", GOLD, 0.965f, 0.18f, 0.055f, false),
        new Button(RIGHT_SHOULDER, "RB", GOLD, 0.965f, 0.31f, 0.055f, false),
        new Button(BACK, "Back", GOLD, 0.43f, 0.94f, 0.04f, true),
        new Button(START, "Start", GOLD, 0.57f, 0.94f, 0.04f, true),
        new Button(LEFT_THUMB, "L3", GOLD, 0.035f, 0.94f, 0.04f, false),
        new Button(RIGHT_THUMB, "R3", GOLD, 0.60f, 0.70f, 0.04f, false),
        new Button(MENU, "≡", GOLD, 0.035f, 0.08f, 0.045f, false),
    };

    // The D-pad: a cross centred here, its arm's length a fraction of the height.
    private static final float DPAD_CX = 0.30f;
    private static final float DPAD_CY = 0.80f;
    private static final float DPAD_SIZE = 0.13f;
    // The sticks: where they rest, their reach, and the area a thumb starts one in.
    private static final float LEFT_CX = 0.13f;
    private static final float LEFT_CY = 0.74f;
    private static final float RIGHT_CX = 0.70f;
    private static final float RIGHT_CY = 0.82f;
    private static final float STICK_REACH = 0.11f;

    private final File filesDir;
    private final BooleanSupplier playing;
    private final Runnable openOptions;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF box = new RectF();

    /** What each finger (by pointer id, up to 10) holds: a button bit, or a control. */
    private static final int NONE = 0;
    private static final int STICK_LEFT = -1;
    private static final int STICK_RIGHT = -2;
    private static final int DPAD = -3;
    private final int[] owner = new int[10];
    private final float[] originX = new float[10];
    private final float[] originY = new float[10];

    private int held;  // button bits, triggers and the menu included
    private int dpad;
    private float leftX, leftY, rightX, rightY;
    // The sticks' drawn bases: where the thumb landed, else their rest.
    private float leftBaseX = -1, leftBaseY, rightBaseX = -1, rightBaseY;
    private boolean shown;
    private boolean running;

    TouchControls(Context context, File filesDir, BooleanSupplier playing,
                  Runnable openOptions) {
        super(context);
        this.filesDir = filesDir;
        this.playing = playing;
        this.openOptions = openOptions;
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        ring.setStyle(Paint.Style.STROKE);
        setVisibility(GONE);
        setHapticFeedbackEnabled(true);
    }

    /** Starts checking the option (twice a second, so it is taken at once). */
    void start() {
        if (!running) {
            running = true;
            ui.post(check);
        }
    }

    void stop() {
        running = false;
        ui.removeCallbacks(check);
        show(false);
    }

    /** Whether the option is on - the game's prompts follow it (OptionsPanel). */
    static boolean enabled(File filesDir) {
        return "on".equals(Options.read(filesDir, Options.byFile("touch-controls")));
    }

    private final Runnable check = new Runnable() {
        @Override
        public void run() {
            if (!running) {
                return;
            }
            show(enabled(filesDir) && playing.getAsBoolean());
            ui.postDelayed(this, CHECK_MS);
        }
    };

    private void show(boolean wanted) {
        if (wanted == shown) {
            return;
        }
        shown = wanted;
        releaseAll();
        setVisibility(wanted ? VISIBLE : GONE);
        send();
    }

    private void releaseAll() {
        java.util.Arrays.fill(owner, NONE);
        held = 0;
        dpad = 0;
        leftX = leftY = rightX = rightY = 0;
        leftBaseX = rightBaseX = -1;
    }

    /** The state to the native side: active while shown. */
    private void send() {
        int bits = (held & 0xFFFF) | dpad;
        NativeBridge.touchPad(shown, bits, leftX, leftY, rightX, rightY,
                (held & LEFT_TRIGGER) != 0 ? 1f : 0f, (held & RIGHT_TRIGGER) != 0 ? 1f : 0f);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        for (Button button : buttons) {
            button.x = button.cx * w;
            button.y = button.cy * h;
            button.r = button.radius * h;
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!shown) {
            return false;
        }
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                down(event.getPointerId(index), event.getX(index), event.getY(index));
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++) {
                    move(event.getPointerId(i), event.getX(i), event.getY(i));
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                up(event.getPointerId(index));
                break;
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                break;
            default:
                return true;
        }
        send();
        invalidate();
        return true;
    }

    private void down(int id, float x, float y) {
        if (id < 0 || id >= owner.length) {
            return;
        }
        float w = getWidth();
        float h = getHeight();
        for (Button button : buttons) {
            if (button.hit(x, y)) {
                owner[id] = button.bit;
                held |= button.bit;
                performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                if (button.bit == MENU) {
                    // Over the game, which holds; the controls hide while it is open.
                    owner[id] = NONE;
                    held &= ~MENU;
                    openOptions.run();
                }
                return;
            }
        }
        float dpadX = DPAD_CX * w;
        float dpadY = DPAD_CY * h;
        float arm = DPAD_SIZE * h;
        if (Math.abs(x - dpadX) <= arm * 1.2f && Math.abs(y - dpadY) <= arm * 1.2f) {
            owner[id] = DPAD;
            dpad = dpadBits(x - dpadX, y - dpadY, arm);
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            return;
        }
        // The sticks float: the thumb's landing is the stick's centre.
        if (x < 0.40f * w && y > 0.50f * h && (owner[id] = freeStick(STICK_LEFT)) != NONE) {
            originX[id] = x;
            originY[id] = y;
            leftBaseX = x;
            leftBaseY = y;
            return;
        }
        if (x > 0.52f * w && x < 0.78f * w && y > 0.55f * h
                && (owner[id] = freeStick(STICK_RIGHT)) != NONE) {
            originX[id] = x;
            originY[id] = y;
            rightBaseX = x;
            rightBaseY = y;
        }
    }

    /** The stick, if no other finger holds it; else NONE. */
    private int freeStick(int stick) {
        for (int o : owner) {
            if (o == stick) {
                return NONE;
            }
        }
        return stick;
    }

    private void move(int id, float x, float y) {
        if (id < 0 || id >= owner.length) {
            return;
        }
        float reach = STICK_REACH * getHeight();
        if (owner[id] == STICK_LEFT || owner[id] == STICK_RIGHT) {
            float dx = (x - originX[id]) / reach;
            float dy = (y - originY[id]) / reach;
            float length = (float) Math.hypot(dx, dy);
            if (length > 1f) {
                dx /= length;
                dy /= length;
            }
            if (owner[id] == STICK_LEFT) {
                leftX = dx;
                leftY = -dy;  // the 360's up is positive
            } else {
                rightX = dx;
                rightY = -dy;
            }
        } else if (owner[id] == DPAD) {
            float arm = DPAD_SIZE * getHeight();
            int bits = dpadBits(x - DPAD_CX * getWidth(), y - DPAD_CY * getHeight(), arm);
            if (bits != dpad && bits != 0) {
                performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            }
            dpad = bits;
        }
    }

    private void up(int id) {
        if (id < 0 || id >= owner.length) {
            return;
        }
        int o = owner[id];
        owner[id] = NONE;
        if (o == STICK_LEFT) {
            leftX = leftY = 0;
            leftBaseX = -1;
        } else if (o == STICK_RIGHT) {
            rightX = rightY = 0;
            rightBaseX = -1;
        } else if (o == DPAD) {
            dpad = 0;
        } else if (o != NONE) {
            // Another finger may hold the same button: only the last one lets it go.
            boolean still = false;
            for (int other : owner) {
                still |= other == o;
            }
            if (!still) {
                held &= ~o;
            }
        }
    }

    /** The D-pad's directions from the finger's offset: eight ways, a dead centre. */
    private static int dpadBits(float dx, float dy, float arm) {
        if (Math.hypot(dx, dy) < arm * 0.2f) {
            return 0;
        }
        double angle = Math.toDegrees(Math.atan2(-dy, dx));  // 0 right, 90 up
        if (angle < 0) {
            angle += 360;
        }
        int bits = 0;
        if (angle < 67.5 || angle > 292.5) {
            bits |= DPAD_RIGHT;
        }
        if (angle > 22.5 && angle < 157.5) {
            bits |= DPAD_UP;
        }
        if (angle > 112.5 && angle < 247.5) {
            bits |= DPAD_LEFT;
        }
        if (angle > 202.5 && angle < 337.5) {
            bits |= DPAD_DOWN;
        }
        return bits;
    }

    // ---- drawing: translucent, the game showing through ----

    @Override
    protected void onDraw(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        ring.setStrokeWidth(h * 0.004f);
        drawStick(canvas, leftBaseX >= 0 ? leftBaseX : LEFT_CX * w,
                leftBaseX >= 0 ? leftBaseY : LEFT_CY * h, leftX, leftY, h);
        drawStick(canvas, rightBaseX >= 0 ? rightBaseX : RIGHT_CX * w,
                rightBaseX >= 0 ? rightBaseY : RIGHT_CY * h, rightX, rightY, h);
        drawDpad(canvas, DPAD_CX * w, DPAD_CY * h, DPAD_SIZE * h);
        for (Button button : buttons) {
            boolean down = (held & button.bit) != 0;
            fill.setColor(down ? (button.colour & 0x00FFFFFF) | 0xB0000000 : 0x55100C08);
            ring.setColor((button.colour & 0x00FFFFFF) | (down ? 0xFF000000 : 0xB0000000));
            if (button.pill) {
                box.set(button.x - button.r * 1.9f, button.y - button.r,
                        button.x + button.r * 1.9f, button.y + button.r);
                canvas.drawRoundRect(box, button.r, button.r, fill);
                canvas.drawRoundRect(box, button.r, button.r, ring);
            } else {
                canvas.drawCircle(button.x, button.y, button.r, fill);
                canvas.drawCircle(button.x, button.y, button.r, ring);
            }
            text.setColor(down ? 0xFFFFFFFF : (button.colour & 0x00FFFFFF) | 0xE0000000);
            text.setTextSize(button.r * (button.label.length() > 2 ? 0.75f : 0.95f));
            canvas.drawText(button.label, button.x, button.y + text.getTextSize() * 0.35f, text);
        }
    }

    private void drawStick(Canvas canvas, float cx, float cy, float sx, float sy, float h) {
        float reach = STICK_REACH * h;
        fill.setColor(0x40100C08);
        ring.setColor(0x90C9A227);
        canvas.drawCircle(cx, cy, reach * 1.15f, fill);
        canvas.drawCircle(cx, cy, reach * 1.15f, ring);
        boolean moved = sx != 0 || sy != 0;
        fill.setColor(moved ? 0xC0C9A227 : 0x80C9A227);
        canvas.drawCircle(cx + sx * reach, cy - sy * reach, reach * 0.45f, fill);
    }

    private void drawDpad(Canvas canvas, float cx, float cy, float arm) {
        float half = arm * 0.36f;
        int[] bits = {DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT};
        float[][] offsets = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (int i = 0; i < 4; i++) {
            float ox = cx + offsets[i][0] * arm * 0.62f;
            float oy = cy + offsets[i][1] * arm * 0.62f;
            boolean down = (dpad & bits[i]) != 0;
            box.set(ox - half, oy - half, ox + half, oy + half);
            fill.setColor(down ? 0xB0C9A227 : 0x55100C08);
            ring.setColor(down ? 0xFFC9A227 : 0xB0C9A227);
            canvas.drawRoundRect(box, half * 0.3f, half * 0.3f, fill);
            canvas.drawRoundRect(box, half * 0.3f, half * 0.3f, ring);
        }
    }
}
