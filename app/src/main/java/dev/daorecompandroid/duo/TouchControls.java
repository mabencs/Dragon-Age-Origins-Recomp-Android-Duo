package dev.daorecompandroid.duo;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.Arrays;
import java.util.Locale;
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
 *
 * <p>PLAN 10.21 (the owner, 2026-11-08): the player's own size and layout. Options ->
 * On-screen controller size ({@code files/touch-scale}, a percentage) scales every control;
 * Options -> Adjust on-screen controls layout opens {@link #startEditing}, where each control
 * is dragged where the player wants it and a small Done saves the places to
 * {@code files/touch-layout} (a line "id x y" a control, fractions of the screen). A stick
 * still floats: the area a thumb starts it in moves with its resting place.
 */
final class TouchControls extends View {

    private static final String TAG = "Warden";
    private static final long CHECK_MS = 500;
    /** The places, as the player left them (startEditing). */
    static final String LAYOUT_FILE = "touch-layout";

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
        final String id;  // in the layout file
        final int bit;
        final String label;
        final int colour;
        final float defaultCx;
        final float defaultCy;
        float cx;  // fraction of the width
        float cy;  // fraction of the height
        final float radius;  // fraction of the height, at 100%
        final boolean pill;
        float x;
        float y;
        float r;

        Button(String id, int bit, String label, int colour, float cx, float cy, float radius,
               boolean pill) {
            this.id = id;
            this.bit = bit;
            this.label = label;
            this.colour = colour;
            this.defaultCx = cx;
            this.defaultCy = cy;
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
        new Button("A", A, "A", 0xFF5DBB4C, 0.885f, 0.66f, 0.068f, false),
        new Button("B", B, "B", 0xFFD9443A, 0.945f, 0.54f, 0.068f, false),
        new Button("X", X, "X", 0xFF3C7FD9, 0.825f, 0.54f, 0.068f, false),
        new Button("Y", Y, "Y", 0xFFE8C33A, 0.885f, 0.42f, 0.068f, false),
        new Button("LT", LEFT_TRIGGER, "LT", GOLD, 0.035f, 0.30f, 0.055f, false),
        new Button("LB", LEFT_SHOULDER, "LB", GOLD, 0.035f, 0.44f, 0.055f, false),
        new Button("RT", RIGHT_TRIGGER, "RT", GOLD, 0.965f, 0.18f, 0.055f, false),
        new Button("RB", RIGHT_SHOULDER, "RB", GOLD, 0.965f, 0.31f, 0.055f, false),
        new Button("Back", BACK, "Back", GOLD, 0.43f, 0.94f, 0.04f, true),
        new Button("Start", START, "Start", GOLD, 0.57f, 0.94f, 0.04f, true),
        new Button("L3", LEFT_THUMB, "L3", GOLD, 0.035f, 0.94f, 0.04f, false),
        new Button("R3", RIGHT_THUMB, "R3", GOLD, 0.60f, 0.70f, 0.04f, false),
        new Button("Menu", MENU, "≡", GOLD, 0.035f, 0.08f, 0.045f, false),
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

    // Where the D-pad and the sticks are now (the player's layout, else the defaults).
    private float dpadCx = DPAD_CX;
    private float dpadCy = DPAD_CY;
    private float leftCx = LEFT_CX;
    private float leftCy = LEFT_CY;
    private float rightCx = RIGHT_CX;
    private float rightCy = RIGHT_CY;
    /** Options -> On-screen controller size, as a factor. */
    private float scale = 1f;
    // Where they are drawn and hit, in pixels: the places above, each control kept whole on
    // the screen (bigger controls near an edge move in rather than off it).
    private float dpadX;
    private float dpadY;
    private float leftRestX;
    private float leftRestY;
    private float rightRestX;
    private float rightRestY;

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

    // ---- the layout editor (startEditing) ----
    private static final int DRAG_NONE = -1;
    private static final int DRAG_DPAD = 100;
    private static final int DRAG_LEFT = 101;
    private static final int DRAG_RIGHT = 102;
    private boolean editing;
    private Runnable editDone;
    /** What each finger drags: a button's index, or DRAG_DPAD / DRAG_LEFT / DRAG_RIGHT. */
    private final int[] dragging = new int[10];
    private final float[] grabX = new float[10];
    private final float[] grabY = new float[10];
    private final RectF doneBox = new RectF();
    private final RectF resetBox = new RectF();

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
        Arrays.fill(dragging, DRAG_NONE);
        scale = readScale();
        loadLayout();
    }

    /** Starts checking the option (twice a second, so it is taken at once). */
    void start() {
        if (!running) {
            running = true;
            if (inFront) {
                ui.post(check);
            }
        }
    }

    /** Whether the game is in front: put away (the lid, the lock, Home), no checks (BUG-119). */
    private boolean inFront = true;

    void setInFront(boolean front) {
        inFront = front;
        ui.removeCallbacks(check);
        if (front && running) {
            ui.post(check);
        }
    }

    void stop() {
        running = false;
        ui.removeCallbacks(check);
        if (editing) {
            finishEditing();
        }
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
            if (!editing) {
                takeScale();
                show(enabled(filesDir) && playing.getAsBoolean());
            }
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
        Arrays.fill(owner, NONE);
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

    // ---- size and places ----

    /** Options -> On-screen controller size: 100 is the controls as first drawn. */
    private float readScale() {
        try {
            return Integer.parseInt(Options.read(filesDir, Options.byFile("touch-scale")))
                    / 100f;
        } catch (NumberFormatException e) {
            return 1f;
        }
    }

    /** The size chosen in the panel, taken while shown (the check, twice a second). */
    private void takeScale() {
        float wanted = readScale();
        if (wanted != scale) {
            scale = wanted;
            layout();
            invalidate();
        }
    }

    private void layout() {
        float w = getWidth();
        float h = getHeight();
        for (Button button : buttons) {
            button.r = button.radius * h * scale;
            button.x = inside(button.cx * w, button.r * (button.pill ? 1.9f : 1f), w);
            button.y = inside(button.cy * h, button.r, h);
        }
        float arm = DPAD_SIZE * h * scale * 0.98f;  // to the arms' outer edges
        dpadX = inside(dpadCx * w, arm, w);
        dpadY = inside(dpadCy * h, arm, h);
        float ring = STICK_REACH * h * scale * 1.15f;
        leftRestX = inside(leftCx * w, ring, w);
        leftRestY = inside(leftCy * h, ring, h);
        rightRestX = inside(rightCx * w, ring, w);
        rightRestY = inside(rightCy * h, ring, h);
    }

    /** A centre moved in so that `extent` either side of it stays within 0..size. */
    private static float inside(float centre, float extent, float size) {
        if (extent * 2f >= size) {
            return size / 2f;
        }
        return Math.max(extent, Math.min(size - extent, centre));
    }

    private void loadLayout() {
        File file = new File(filesDir, LAYOUT_FILE);
        if (!file.exists()) {
            return;
        }
        try (BufferedReader in = new BufferedReader(new FileReader(file))) {
            String line;
            while ((line = in.readLine()) != null) {
                String[] part = line.trim().split("\\s+");
                if (part.length != 3) {
                    continue;
                }
                float x = clamp(Float.parseFloat(part[1]));
                float y = clamp(Float.parseFloat(part[2]));
                place(part[0], x, y);
            }
        } catch (IOException | NumberFormatException e) {
            Log.w(TAG, "touch controls: the layout unread, the defaults kept", e);
        }
    }

    private void place(String id, float x, float y) {
        switch (id) {
            case "DPad":
                dpadCx = x;
                dpadCy = y;
                return;
            case "LeftStick":
                leftCx = x;
                leftCy = y;
                return;
            case "RightStick":
                rightCx = x;
                rightCy = y;
                return;
            default:
                for (Button button : buttons) {
                    if (button.id.equals(id)) {
                        button.cx = x;
                        button.cy = y;
                    }
                }
        }
    }

    private void saveLayout() {
        StringBuilder out = new StringBuilder();
        for (Button button : buttons) {
            line(out, button.id, button.cx, button.cy);
        }
        line(out, "DPad", dpadCx, dpadCy);
        line(out, "LeftStick", leftCx, leftCy);
        line(out, "RightStick", rightCx, rightCy);
        File file = new File(filesDir, LAYOUT_FILE);
        File part = new File(filesDir, LAYOUT_FILE + ".part");
        try (FileWriter writer = new FileWriter(part)) {
            writer.write(out.toString());
        } catch (IOException e) {
            Log.e(TAG, "touch controls: cannot write the layout", e);
            return;
        }
        if (!part.renameTo(file)) {
            Log.e(TAG, "touch controls: cannot put the layout in place");
            return;
        }
        Log.i(TAG, "touch controls: the layout saved");
    }

    private static void line(StringBuilder out, String id, float x, float y) {
        out.append(String.format(Locale.ROOT, "%s %.4f %.4f%n", id, x, y));
    }

    private void resetLayout() {
        for (Button button : buttons) {
            button.cx = button.defaultCx;
            button.cy = button.defaultCy;
        }
        dpadCx = DPAD_CX;
        dpadCy = DPAD_CY;
        leftCx = LEFT_CX;
        leftCy = LEFT_CY;
        rightCx = RIGHT_CX;
        rightCy = RIGHT_CY;
        layout();
        Log.i(TAG, "touch controls: the layout back to the defaults");
    }

    private static float clamp(float value) {
        return Math.max(0f, Math.min(1f, value));
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        layout();
    }

    // ---- the layout editor ----

    /**
     * Options -> Adjust on-screen controls layout: every control shown where it is, to be
     * dragged by touch, whether the on-screen controller is on or not; nothing reaches the
     * game. Done (or the controller's A, B or Start) saves the places and runs
     * {@code done}; Reset puts the defaults back until then.
     */
    void startEditing(Runnable done) {
        editDone = done;
        if (shown) {
            show(false);  // nothing held reaches the game while editing
        }
        editing = true;
        scale = readScale();
        Arrays.fill(dragging, DRAG_NONE);
        setVisibility(VISIBLE);
        bringToFront();
        layout();
        invalidate();
        Log.i(TAG, "touch controls: adjusting the layout");
    }

    boolean isEditing() {
        return editing;
    }

    private void finishEditing() {
        saveLayout();
        editing = false;
        Arrays.fill(dragging, DRAG_NONE);
        shown = false;
        setVisibility(GONE);
        Runnable done = editDone;
        editDone = null;
        if (done != null) {
            done.run();
        }
    }

    /**
     * The controller while editing: A, B, Start, Select or Back is Done; the pad's other
     * buttons do nothing. The volume keys stay the system's.
     */
    boolean onEditKey(KeyEvent event) {
        if (!editing) {
            return false;
        }
        int code = event.getKeyCode();
        boolean done = code == KeyEvent.KEYCODE_BUTTON_A || code == KeyEvent.KEYCODE_BUTTON_B
                || code == KeyEvent.KEYCODE_BUTTON_START || code == KeyEvent.KEYCODE_BUTTON_SELECT
                || code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_ESCAPE
                || code == KeyEvent.KEYCODE_ENTER || code == KeyEvent.KEYCODE_DPAD_CENTER;
        if (!done && !KeyEvent.isGamepadButton(code)) {
            return false;
        }
        if (done && event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
            finishEditing();
        }
        return true;
    }

    private void editTouch(MotionEvent event) {
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                editDown(event.getPointerId(index), event.getX(index), event.getY(index));
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++) {
                    editMove(event.getPointerId(i), event.getX(i), event.getY(i));
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                int id = event.getPointerId(index);
                if (id >= 0 && id < dragging.length) {
                    dragging[id] = DRAG_NONE;
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                Arrays.fill(dragging, DRAG_NONE);
                break;
            default:
                break;
        }
        invalidate();
    }

    private void editDown(int id, float x, float y) {
        if (id < 0 || id >= dragging.length) {
            return;
        }
        if (doneBox.contains(x, y)) {
            finishEditing();
            return;
        }
        if (resetBox.contains(x, y)) {
            resetLayout();
            return;
        }
        float w = getWidth();
        float h = getHeight();
        // The buttons first, the last drawn on top.
        for (int i = buttons.length - 1; i >= 0; i--) {
            if (buttons[i].hit(x, y)) {
                grab(id, i, x - buttons[i].x, y - buttons[i].y);
                return;
            }
        }
        float arm = DPAD_SIZE * h * scale;
        if (Math.abs(x - dpadX) <= arm && Math.abs(y - dpadY) <= arm) {
            grab(id, DRAG_DPAD, x - dpadX, y - dpadY);
            return;
        }
        float reach = STICK_REACH * h * scale * 1.15f;
        if (Math.hypot(x - leftRestX, y - leftRestY) <= reach) {
            grab(id, DRAG_LEFT, x - leftRestX, y - leftRestY);
        } else if (Math.hypot(x - rightRestX, y - rightRestY) <= reach) {
            grab(id, DRAG_RIGHT, x - rightRestX, y - rightRestY);
        }
    }

    private void grab(int id, int what, float offsetX, float offsetY) {
        dragging[id] = what;
        grabX[id] = offsetX;
        grabY[id] = offsetY;
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
    }

    private void editMove(int id, float x, float y) {
        if (id < 0 || id >= dragging.length || dragging[id] == DRAG_NONE) {
            return;
        }
        float fx = clamp((x - grabX[id]) / getWidth());
        float fy = clamp((y - grabY[id]) / getHeight());
        int what = dragging[id];
        if (what == DRAG_DPAD) {
            dpadCx = fx;
            dpadCy = fy;
        } else if (what == DRAG_LEFT) {
            leftCx = fx;
            leftCy = fy;
        } else if (what == DRAG_RIGHT) {
            rightCx = fx;
            rightCy = fy;
        } else {
            buttons[what].cx = fx;
            buttons[what].cy = fy;
        }
        layout();
    }

    // ---- play ----

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (editing) {
            editTouch(event);
            return true;
        }
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
        float arm = DPAD_SIZE * h * scale;
        if (Math.abs(x - dpadX) <= arm * 1.2f && Math.abs(y - dpadY) <= arm * 1.2f) {
            owner[id] = DPAD;
            dpad = dpadBits(x - dpadX, y - dpadY, arm);
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            return;
        }
        // The sticks float: the thumb's landing is the stick's centre, in an area that
        // moves with the stick's resting place (by default the lower left; the lower
        // middle-right).
        float lx = leftCx - LEFT_CX;
        float ly = leftCy - LEFT_CY;
        if (x >= lx * w && x < (0.40f + lx) * w && y > (0.50f + ly) * h && y <= (1f + ly) * h
                && (owner[id] = freeStick(STICK_LEFT)) != NONE) {
            originX[id] = x;
            originY[id] = y;
            leftBaseX = x;
            leftBaseY = y;
            return;
        }
        float rx = rightCx - RIGHT_CX;
        float ry = rightCy - RIGHT_CY;
        if (x > (0.52f + rx) * w && x < (0.78f + rx) * w && y > (0.55f + ry) * h
                && y <= (1f + ry) * h && (owner[id] = freeStick(STICK_RIGHT)) != NONE) {
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
        float reach = STICK_REACH * getHeight() * scale;
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
            float arm = DPAD_SIZE * getHeight() * scale;
            int bits = dpadBits(x - dpadX, y - dpadY, arm);
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
        if (editing) {
            canvas.drawColor(0x66000000);  // the controls stand out from what is behind
        }
        ring.setStrokeWidth(h * 0.004f);
        boolean leftAtRest = editing || leftBaseX < 0;
        boolean rightAtRest = editing || rightBaseX < 0;
        drawStick(canvas, leftAtRest ? leftRestX : leftBaseX,
                leftAtRest ? leftRestY : leftBaseY, leftX, leftY, h, dragged(DRAG_LEFT));
        drawStick(canvas, rightAtRest ? rightRestX : rightBaseX,
                rightAtRest ? rightRestY : rightBaseY, rightX, rightY, h, dragged(DRAG_RIGHT));
        drawDpad(canvas, dpadX, dpadY, DPAD_SIZE * h * scale, dragged(DRAG_DPAD));
        for (int i = 0; i < buttons.length; i++) {
            Button button = buttons[i];
            boolean down = editing ? dragged(i) : (held & button.bit) != 0;
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
        if (editing) {
            drawEditBar(canvas, w, h);
        }
    }

    private boolean dragged(int what) {
        if (!editing) {
            return false;
        }
        for (int d : dragging) {
            if (d == what) {
                return true;
            }
        }
        return false;
    }

    /** Small and at the top centre, between the HUD's portraits and its map: Reset, Done. */
    private void drawEditBar(Canvas canvas, float w, float h) {
        float pillH = h * 0.055f;
        float top = h * 0.025f;
        float doneW = h * 0.17f;
        float resetW = h * 0.17f;
        float gap = h * 0.02f;
        doneBox.set(w / 2f + gap / 2f, top, w / 2f + gap / 2f + doneW, top + pillH);
        resetBox.set(w / 2f - gap / 2f - resetW, top, w / 2f - gap / 2f, top + pillH);
        ring.setStrokeWidth(h * 0.003f);
        fill.setColor(0xC0C9A227);
        canvas.drawRoundRect(doneBox, pillH / 2f, pillH / 2f, fill);
        fill.setColor(0x80100C08);
        ring.setColor(0xB0C9A227);
        canvas.drawRoundRect(resetBox, pillH / 2f, pillH / 2f, fill);
        canvas.drawRoundRect(resetBox, pillH / 2f, pillH / 2f, ring);
        text.setTextSize(pillH * 0.5f);
        text.setColor(0xFF161210);
        canvas.drawText("Done", doneBox.centerX(), doneBox.centerY() + pillH * 0.17f, text);
        text.setColor(0xE0C9A227);
        canvas.drawText("Reset", resetBox.centerX(), resetBox.centerY() + pillH * 0.17f, text);
        text.setTextSize(pillH * 0.36f);
        text.setColor(0xB0D8CFC0);
        canvas.drawText("Drag the controls where you want them", w / 2f,
                top + pillH + pillH * 0.55f, text);
    }

    private void drawStick(Canvas canvas, float cx, float cy, float sx, float sy, float h,
                           boolean picked) {
        float reach = STICK_REACH * h * scale;
        fill.setColor(picked ? 0x80C9A227 : 0x40100C08);
        ring.setColor(0x90C9A227);
        canvas.drawCircle(cx, cy, reach * 1.15f, fill);
        canvas.drawCircle(cx, cy, reach * 1.15f, ring);
        boolean moved = sx != 0 || sy != 0;
        fill.setColor(moved ? 0xC0C9A227 : 0x80C9A227);
        canvas.drawCircle(cx + sx * reach, cy - sy * reach, reach * 0.45f, fill);
    }

    private void drawDpad(Canvas canvas, float cx, float cy, float arm, boolean picked) {
        float half = arm * 0.36f;
        int[] bits = {DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT};
        float[][] offsets = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (int i = 0; i < 4; i++) {
            float ox = cx + offsets[i][0] * arm * 0.62f;
            float oy = cy + offsets[i][1] * arm * 0.62f;
            boolean down = picked || (dpad & bits[i]) != 0;
            box.set(ox - half, oy - half, ox + half, oy + half);
            fill.setColor(down ? 0xB0C9A227 : 0x55100C08);
            ring.setColor(down ? 0xFFC9A227 : 0xB0C9A227);
            canvas.drawRoundRect(box, half * 0.3f, half * 0.3f, fill);
            canvas.drawRoundRect(box, half * 0.3f, half * 0.3f, ring);
        }
    }
}
