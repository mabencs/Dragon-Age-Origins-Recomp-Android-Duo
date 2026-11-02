package dev.daorecompandroid.duo;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.util.HashSet;
import java.util.Set;

/**
 * The options panel (PLAN 10.1): every option, over the game, from the controller.
 *
 * <p>Select and Start pressed together open it during play (GameActivity), and so does
 * Back, then Settings. While it is open the game holds, as when the app is put away:
 * its threads, its clocks and its sound. Up and down choose an option, left and right
 * (or A) change it, B, Start or Select close the panel. The live options - the frame
 * rate, the upscaling, the colour, the shadows and post-processing, the headphone mix,
 * the autosave - are taken at once ({@link NativeBridge#applyOption}); the others from
 * the next start, and the panel says so, with a row to quit the game.
 *
 * <p>Before the game, the same panel is what the app opens on (the owner, 2026-10-30):
 * {@link #openPreLaunch} adds three rows at the top - Launch Dragon Age, and the disc
 * image's and the title update's files to choose - and every option is simply taken as
 * the game launches. Start launches too. {@link #beginGame} turns it into the in-game
 * panel as the game starts.
 *
 * <p>Plain Android views over the game's SurfaceView rather than ImGui, which the plan
 * named: nothing in the presenter changes, the panel cannot cost the game a frame (the
 * game holds), and touch works too. The views never take focus - the activity hands
 * them the controller's keys and motion itself, so a focused ScrollView cannot eat the
 * D-pad.
 */
final class OptionsPanel {

    private static final String TAG = "Warden";

    /** What the panel asks of the activity. */
    interface Host {
        void onOptionsClosed();

        void onQuitChosen();

        /** Before the game: Launch Dragon Age chosen, with the game able to start. */
        void onLaunchChosen();

        /** Before the game: Build chosen, with the game's code able to be made (PLAN 12.8). */
        void onBuildChosen();

        /** PLAN 10.15: before the game, Restore an earlier save chosen. */
        void onRestoreSaves();

        /** PLAN 10.12: before the game, Mods chosen. */
        void onMods();

        /** PLAN 11.1: whether Debugging Mode is on for this run. */
        boolean debugMode();

        /** PLAN 11.1: in the game, with Debugging Mode on, Exit and export save/logs chosen. */
        void onExportReport();

        /** PLAN 11.5: About and licences chosen. */
        void onAbout();

        /** Before the game: a file to choose - {@link #PICK_IMAGE}, {@link #PICK_ULTIMATE},
         * {@link #PICK_UPDATE} or {@link #PICK_LOSSLESS}. */
        void onPickFile(int what);

        /** PLAN 9.20: in the game, Photo mode chosen - the panel closes first. */
        void onPhotoMode();
    }

    /** What the pre-launch rows show, from the activity. */
    static final class LaunchState {
        /** The game data extracted and a title update in place: Launch is offered. */
        boolean ready;
        /** PLAN 12.5: Launch's value while not ready ("not yet"). */
        String launchValue = "not yet";
        String launchDetail = "";
        /** PLAN 12.8: the files in place and the game's code not made, or made for others: Build is offered. */
        boolean canBuild;
        /** Build's value: "not yet", "start", the making's progress, "done" or "failed". */
        String buildValue = "not yet";
        String buildDetail = "";
        String imageValue = "";
        String imageDetail = "";
        String updateValue = "";
        String updateDetail = "";
        String losslessValue = "";
        String losslessDetail = "";
        String ultimateValue = "";
        String ultimateDetail = "";
        /** The header's words on the right. */
        String status = "";
    }

    private static final int GOLD = 0xFFE0B84A;
    private static final int GOLD_DIM = 0xFF8A6D1F;
    private static final int PARCHMENT = 0xFFD8CFC0;
    private static final int GREY = 0xFFA89E8C;
    private static final int FAINT = 0xFF7D7465;
    private static final int CARD = 0xF2161210;
    private static final int SELECTED = 0x3DC9A227;
    private static final int DIM = 0xB3000000;

    // Held on the stick or the hat, the selection moves again after this, then this often.
    private static final long REPEAT_FIRST_MS = 380;
    private static final long REPEAT_NEXT_MS = 110;

    /** The pre-launch rows, then the options from FIRST_OPTION, then quitting. */
    private static final int LAUNCH_ROW = 0;
    // PLAN 12.8 (the owner, 2026-11-02): the game's code made when the player says so,
    // once their files are in - not as soon as a title update is.
    private static final int BUILD_ROW = 1;
    private static final int IMAGE_ROW = 2;
    // PLAN 10.10: the Ultimate Edition's disc 2 - Awakening and every DLC on it.
    private static final int ULTIMATE_ROW = 3;
    private static final int UPDATE_ROW = 4;
    private static final int LOSSLESS_ROW = 5;
    // PLAN 10.15, 11.2: the saves - export, import, and the ones the game wrote over.
    private static final int RESTORE_ROW = 6;
    // PLAN 10.12: the mods (ModsPage).
    private static final int MODS_ROW = 7;
    private static final int FIRST_OPTION = 8;
    /** What an option's asterisk means (the owner, 2026-11-02). */
    private static final String RESTART_NOTE =
            "* Needs a restart of the game: changed while playing, it applies at the next start";

    /** What a pre-launch file row asks for (Host.onPickFile). */
    static final int PICK_IMAGE = 0;
    static final int PICK_UPDATE = 1;
    static final int PICK_LOSSLESS = 2;
    static final int PICK_ULTIMATE = 3;

    private final Context context;
    private final File dir;
    /** The options offered here (HDR's only where the display takes it). */
    private final Options.Option[] options = Options.available();
    private final Host host;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private final FrameLayout overlay;
    private final ScrollView scroll;
    private final LinearLayout list;
    private final TextView title;
    private final TextView status;
    /** In the game: a tap closes the panel (the touch's B). */
    private final TextView resume;
    private final View launchRule;
    private final TextView detail;
    private final TextView hints;
    private final TextView[] names;
    private final TextView[] values;
    private final LinearLayout[] rows;

    /** Each option's value as the game started: an option that is not live differs from it until the next start. */
    private final String[] startValues;
    /** A live option the game could not take while running: from the next start too. */
    private final boolean[] notTaken;
    private final int rowCount;
    private final int quitRow;
    /** PLAN 9.20: in the game, the row after the options opens photo mode. */
    private final int photoRow;
    /** PLAN 11.1: in the game with Debugging Mode on, the row after Photo mode exports a report. */
    private final int exportRow;
    /** PLAN 11.5: the row before Quit, About and licences. */
    private final int aboutRow;

    private boolean open;
    /** Before the game: the launch rows shown, every option taken as the game launches. */
    private boolean preLaunch;
    private LaunchState launch = new LaunchState();
    private int selected = FIRST_OPTION;
    private boolean quitArmed;
    /** Keys pressed while the panel was open: their releases are the panel's too, even after it closes. */
    private final Set<Long> ownedKeys = new HashSet<>();
    /** The direction held on the stick or the hat, for edges and the repeat. */
    private int heldX;
    private int heldY;
    private final Runnable repeat = new Runnable() {
        @Override
        public void run() {
            if (open && heldY != 0) {
                move(heldY);
                handler.postDelayed(this, REPEAT_NEXT_MS);
            }
        }
    };
    /** The last motion of each pad while open, handed to the game as the panel closes. */
    private final android.util.SparseArray<MotionEvent> lastMotion = new android.util.SparseArray<>();

    /** The button labels the hints use, as the game's prompts draw them. */
    private String confirmLabel = "A";
    private String backLabel = "B";

    OptionsPanel(Context context, File dir, FrameLayout root, Host host) {
        this.context = context;
        this.dir = dir;
        this.host = host;

        Options.Option[] all = options;
        rowCount = FIRST_OPTION + all.length + 4;
        photoRow = FIRST_OPTION + all.length;
        exportRow = photoRow + 1;
        aboutRow = exportRow + 1;
        quitRow = aboutRow + 1;
        startValues = new String[all.length];
        notTaken = new boolean[all.length];
        takeStartValues();

        overlay = new FrameLayout(context);
        overlay.setBackgroundColor(DIM);
        overlay.setVisibility(View.GONE);
        overlay.setClickable(true);
        overlay.setOnClickListener(v -> close());
        noFocus(overlay);

        LinearLayout card = new LinearLayout(context);
        card.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable cardBackground = new GradientDrawable();
        cardBackground.setColor(CARD);
        cardBackground.setCornerRadius(dp(6));
        cardBackground.setStroke(dp(1.5f), GOLD_DIM);
        card.setBackground(cardBackground);
        card.setPadding(dp(20), dp(14), dp(20), dp(12));
        card.setClickable(true);  // a tap on the card is not a tap outside it
        noFocus(card);
        FrameLayout.LayoutParams cardParams = new FrameLayout.LayoutParams(
                dp(560), ViewGroup.LayoutParams.MATCH_PARENT);
        cardParams.gravity = Gravity.CENTER_HORIZONTAL;
        cardParams.topMargin = dp(12);
        cardParams.bottomMargin = dp(12);
        overlay.addView(card, cardParams);

        LinearLayout header = new LinearLayout(context);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.BOTTOM);
        title = text(22, GOLD);
        title.setText("Options");
        title.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        header.addView(title, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        status = text(12, FAINT);
        status.setText("The game is paused");
        header.addView(status);
        // Touch (the owner, 2026-11-01): back to the game without a controller.
        resume = text(15, GOLD);
        resume.setText("Resume  ✕");
        resume.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        resume.setPadding(dp(18), dp(6), dp(4), dp(6));
        resume.setClickable(true);
        resume.setOnClickListener(v -> close());
        noFocus(resume);
        header.addView(resume);
        card.addView(header);
        card.addView(rule(), ruleParams());

        scroll = new ScrollView(context);
        scroll.setVerticalScrollBarEnabled(false);
        noFocus(scroll);
        list = new LinearLayout(context);
        list.setOrientation(LinearLayout.VERTICAL);
        noFocus(list);
        scroll.addView(list, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        card.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        names = new TextView[rowCount];
        values = new TextView[rowCount];
        rows = new LinearLayout[rowCount];
        View rule = null;
        for (int i = 0; i < rowCount; i++) {
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(10), dp(i == LAUNCH_ROW ? 9 : 5), dp(10),
                    dp(i == LAUNCH_ROW ? 9 : 5));
            row.setClickable(true);
            noFocus(row);
            final int index = i;
            // Touch: where the finger lifted, for the value's halves (tapRow).
            final float[] tapX = {-1f};
            row.setOnTouchListener((v, event) -> {
                if (event.getActionMasked() == MotionEvent.ACTION_UP) {
                    tapX[0] = event.getX();
                }
                return false;
            });
            row.setOnClickListener(v -> tapRow(index, tapX[0]));
            TextView name = text(i == LAUNCH_ROW ? 19 : 15, PARCHMENT);
            if (i == LAUNCH_ROW) {
                name.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
            }
            TextView value = text(15, GOLD);
            value.setGravity(Gravity.END);
            // Wide enough to take a thumb on either half (‹ back, › on).
            value.setMinWidth(dp(190));
            value.setSingleLine(true);
            value.setEllipsize(TextUtils.TruncateAt.MIDDLE);
            row.addView(name, new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            row.addView(value, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            list.addView(row, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            names[i] = name;
            values[i] = value;
            rows[i] = row;
            if (i == MODS_ROW) {
                // Between the files and the options, shown with them.
                rule = rule();
                list.addView(rule, ruleParams());
            }
        }
        launchRule = rule;
        names[LAUNCH_ROW].setText("Launch Dragon Age");
        names[BUILD_ROW].setText("Build");
        names[IMAGE_ROW].setText("Disc image (.iso)");
        names[ULTIMATE_ROW].setText("Ultimate Edition disc 2 (.iso)");
        // PLAN 12.3 (the owner, 2026-11-02): disc 2 carries Title Update 6.
        names[UPDATE_ROW].setText("Title update 7 (optional, recommended)");
        names[LOSSLESS_ROW].setText("Lossless Scaling (Lossless.dll)");
        names[RESTORE_ROW].setText("Saves - export, import, restore");
        names[MODS_ROW].setText("Mods");
        showLaunchRows(false);

        card.addView(rule(), ruleParams());
        detail = text(13, GREY);
        detail.setMinLines(2);
        detail.setMaxLines(3);
        detail.setEllipsize(TextUtils.TruncateAt.END);
        card.addView(detail);
        hints = text(12, FAINT);
        hints.setPadding(0, dp(6), 0, 0);
        card.addView(hints);

        root.addView(overlay, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        render();
    }

    boolean isOpen() {
        return open;
    }

    void open() {
        if (open) {
            return;
        }
        open = true;
        quitArmed = false;
        heldX = 0;
        heldY = 0;
        overlay.setVisibility(View.VISIBLE);
        overlay.bringToFront();
        render();
        Log.i(TAG, "options: panel open");
    }

    /**
     * Before the game: the panel as the app's first screen - Launch Dragon Age chosen, the
     * files' rows and the options below it. It stays open until the game is launched.
     */
    void openPreLaunch(LaunchState state) {
        preLaunch = true;
        launch = state;
        title.setText("Dragon Age: Origins");
        showLaunchRows(true);
        selected = LAUNCH_ROW;
        open();
        Log.i(TAG, "options: the pre-launch settings open");
    }

    /** The pre-launch rows' state changed (a file chosen, the extraction's progress). */
    void setLaunchState(LaunchState state) {
        launch = state;
        if (preLaunch) {
            render();
        }
    }

    /**
     * The game is launching: the panel becomes the in-game one, closed - the launch rows
     * gone, and what an option holds now is what the game started with.
     */
    void beginGame() {
        preLaunch = false;
        open = false;
        handler.removeCallbacks(repeat);
        overlay.setVisibility(View.GONE);
        showLaunchRows(false);
        title.setText("Options");
        takeStartValues();
        selected = FIRST_OPTION;
        quitArmed = false;
        render();
        Log.i(TAG, "options: the game launches with the settings as they are");
    }

    void close() {
        if (!open || preLaunch) {
            return;
        }
        open = false;
        handler.removeCallbacks(repeat);
        overlay.setVisibility(View.GONE);
        Log.i(TAG, "options: panel closed");
        host.onOptionsClosed();
    }

    /**
     * The pads' last motion while the panel was open, each handed to {@code sink} and then
     * forgotten - so the game sees the sticks, triggers and hat as they are now.
     */
    void takeMotions(java.util.function.Consumer<MotionEvent> sink) {
        for (int i = 0; i < lastMotion.size(); i++) {
            MotionEvent event = lastMotion.valueAt(i);
            sink.accept(event);
            event.recycle();
        }
        lastMotion.clear();
    }

    /**
     * A key, open or not. Open, the navigation keys are the panel's; closed, only the
     * releases of keys pressed while it was open (the B that closed it) are. Before the
     * game, Start launches, B does nothing (there is nothing to go back to) and the
     * system's Back is the activity's, which closes the app.
     *
     * @return true if the key was the panel's.
     */
    boolean onKey(KeyEvent event) {
        long key = ((long) event.getDeviceId() << 32) | (event.getKeyCode() & 0xFFFFFFFFL);
        int action = event.getAction();
        if (action == KeyEvent.ACTION_UP) {
            return ownedKeys.remove(key);
        }
        if (!open || action != KeyEvent.ACTION_DOWN) {
            return false;
        }
        boolean first = event.getRepeatCount() == 0;
        switch (event.getKeyCode()) {
            case KeyEvent.KEYCODE_DPAD_UP:
                move(-1);
                break;
            case KeyEvent.KEYCODE_DPAD_DOWN:
                move(1);
                break;
            case KeyEvent.KEYCODE_DPAD_LEFT:
                if (first) {
                    change(-1, false);
                }
                break;
            case KeyEvent.KEYCODE_DPAD_RIGHT:
                if (first) {
                    change(1, false);
                }
                break;
            case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_DPAD_CENTER:
            case KeyEvent.KEYCODE_ENTER:
                if (first) {
                    activate();
                }
                break;
            case KeyEvent.KEYCODE_BACK:
                if (preLaunch) {
                    return false;
                }
                if (first) {
                    ownedKeys.add(key);
                    close();
                }
                return true;
            case KeyEvent.KEYCODE_BUTTON_START:
                if (preLaunch) {
                    if (first) {
                        launchIfReady();
                    }
                    break;
                }
                if (first) {
                    ownedKeys.add(key);
                    close();
                }
                return true;
            case KeyEvent.KEYCODE_BUTTON_B:
            case KeyEvent.KEYCODE_ESCAPE:
            case KeyEvent.KEYCODE_BUTTON_SELECT:
            case KeyEvent.KEYCODE_MENU:
                if (preLaunch) {
                    break;
                }
                if (first) {
                    ownedKeys.add(key);
                    close();
                }
                return true;
            default:
                // The volume keys and the like stay the system's; the pad's other buttons
                // do nothing here, and do not reach the game either.
                if (!KeyEvent.isGamepadButton(event.getKeyCode())) {
                    return false;
                }
                break;
        }
        ownedKeys.add(key);
        return true;
    }

    /** A pad's sticks and hat while open: the left stick and the hat move like the D-pad. */
    boolean onMotion(MotionEvent event) {
        if (!open || event.getAction() != MotionEvent.ACTION_MOVE
                || (event.getSource() & android.view.InputDevice.SOURCE_JOYSTICK)
                        != android.view.InputDevice.SOURCE_JOYSTICK) {
            return false;
        }
        MotionEvent previous = lastMotion.get(event.getDeviceId());
        if (previous != null) {
            previous.recycle();
        }
        lastMotion.put(event.getDeviceId(), MotionEvent.obtain(event));

        float hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X);
        float hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
        float stickX = event.getAxisValue(MotionEvent.AXIS_X);
        float stickY = event.getAxisValue(MotionEvent.AXIS_Y);
        int x = direction(hatX, stickX, heldX);
        int y = direction(hatY, stickY, heldY);
        if (y != heldY) {
            heldY = y;
            handler.removeCallbacks(repeat);
            if (y != 0) {
                move(y);
                handler.postDelayed(repeat, REPEAT_FIRST_MS);
            }
        }
        if (x != heldX) {
            heldX = x;
            if (x != 0) {
                change(x, false);
            }
        }
        return true;
    }

    /**
     * -1, 0 or 1 from the hat, else the stick: pushed past 0.6, let go below 0.35 (so a
     * stick resting near the edge does not flicker between the two).
     */
    private static int direction(float hat, float stick, int held) {
        if (hat < -0.5f) {
            return -1;
        }
        if (hat > 0.5f) {
            return 1;
        }
        if (held != 0 && stick * held > 0.35f) {
            return held;
        }
        if (stick < -0.6f) {
            return -1;
        }
        if (stick > 0.6f) {
            return 1;
        }
        return 0;
    }

    /** The first row offered: the launch rows only before the game. */
    private int firstRow() {
        return preLaunch ? LAUNCH_ROW : FIRST_OPTION;
    }

    private void move(int delta) {
        int first = firstRow();
        int row = first + Math.floorMod(selected - first + delta, rowCount - first);
        // The photo mode row is the game's only; the report's, the game's with Debugging Mode.
        while (hidden(row)) {
            row = first + Math.floorMod(row - first + delta, rowCount - first);
        }
        select(row);
    }

    private boolean hidden(int row) {
        return (row == photoRow && preLaunch)
                || (row == exportRow && (preLaunch || !host.debugMode()));
    }

    private void select(int row) {
        if (row != selected) {
            quitArmed = false;
            cheatDone = null;
        }
        selected = row;
        render();
        final View target = rows[row];
        scroll.post(() -> {
            int top = target.getTop();
            int bottom = target.getBottom();
            int visibleTop = scroll.getScrollY();
            int visibleBottom = visibleTop + scroll.getHeight();
            if (top < visibleTop) {
                scroll.smoothScrollTo(0, top);
            } else if (bottom > visibleBottom) {
                scroll.smoothScrollTo(0, bottom - scroll.getHeight());
            }
        });
    }

    /**
     * A tap on a row (the owner, 2026-11-01: the panel by touch). Launch, the files and
     * Quit act at once (Quit in the game asks for a second tap, as for a second A). An
     * option is chosen by the first tap - its description shown, nothing changed - and
     * then changed by taps on its value: the left half back (‹), the right half on (›),
     * a tap on its name on, round to the first, as A does.
     */
    private void tapRow(int index, float x) {
        boolean option = index >= FIRST_OPTION && index < photoRow;
        if (!option) {
            select(index);
            activate();
            return;
        }
        if (index != selected) {
            select(index);
            return;
        }
        View value = values[index];
        if (x >= value.getLeft() && x < value.getLeft() + value.getWidth() / 2f) {
            change(-1, false);
        } else if (x >= value.getLeft()) {
            change(1, false);
        } else if (Options.isCheatAction(options[index - FIRST_OPTION].file)) {
            activate();  // PLAN 10.14: a tap on the name does it
        } else {
            change(1, true);
        }
    }

    /**
     * A, or a tap: the next value, round to the first; Launch, a file to choose; on the
     * last row, quitting (before the game, closing the app at once - nothing to lose).
     */
    private void activate() {
        if (selected == LAUNCH_ROW) {
            launchIfReady();
            return;
        }
        if (selected == BUILD_ROW) {
            if (!preLaunch) {
                return;
            }
            if (!launch.canBuild) {
                Log.i(TAG, "options: Build chosen, but there is nothing to build now ("
                        + launch.buildValue + ")");
                return;
            }
            Log.i(TAG, "options: Build");
            host.onBuildChosen();
            return;
        }
        if (selected == IMAGE_ROW || selected == ULTIMATE_ROW || selected == UPDATE_ROW
                || selected == LOSSLESS_ROW) {
            Log.i(TAG, "options: choose the " + (selected == IMAGE_ROW ? "disc image"
                    : selected == ULTIMATE_ROW ? "Ultimate Edition's disc 2"
                    : selected == UPDATE_ROW ? "title update" : "Lossless.dll"));
            host.onPickFile(selected == IMAGE_ROW ? PICK_IMAGE
                    : selected == ULTIMATE_ROW ? PICK_ULTIMATE
                    : selected == UPDATE_ROW ? PICK_UPDATE : PICK_LOSSLESS);
            return;
        }
        if (selected == RESTORE_ROW) {
            Log.i(TAG, "options: the save restore page");
            host.onRestoreSaves();
            return;
        }
        if (selected == MODS_ROW) {
            Log.i(TAG, "options: the Mods page");
            host.onMods();
            return;
        }
        if (selected == photoRow) {
            Log.i(TAG, "options: photo mode chosen");
            host.onPhotoMode();
            return;
        }
        if (selected == aboutRow) {
            Log.i(TAG, "options: About and licences");
            host.onAbout();
            return;
        }
        if (selected == exportRow) {
            if (!hidden(exportRow)) {
                Log.i(TAG, "options: Exit and export save/logs chosen");
                host.onExportReport();
            }
            return;
        }
        if (selected == quitRow) {
            if (quitArmed || preLaunch) {
                Log.i(TAG, "options: quit chosen");
                host.onQuitChosen();
                return;
            }
            quitArmed = true;
            render();
            return;
        }
        // PLAN 10.14: a cheat done on demand - the chosen amount, at the next frame of play.
        int actionIndex = selected - FIRST_OPTION;
        if (actionIndex >= 0 && actionIndex < options.length
                && Options.isCheatAction(options[actionIndex].file)) {
            if (!preLaunch) {
                Options.Option option = options[actionIndex];
                String value = Options.read(dir, option);
                NativeBridge.applyOption(option.file, value);
                cheatDone = option.labels[option.indexOf(value)];
                Log.i(TAG, "options: " + option.file + " " + value + " asked for");
                render();
            }
            return;
        }
        change(1, true);
    }

    /** PLAN 10.14: the last cheat asked for, shown under the row until the selection moves. */
    private String cheatDone;

    private void launchIfReady() {
        if (!preLaunch) {
            return;
        }
        if (!launch.ready) {
            Log.i(TAG, "options: Launch chosen, but the game cannot start yet");
            return;
        }
        Log.i(TAG, "options: Launch Dragon Age");
        host.onLaunchChosen();
    }

    private void change(int delta, boolean wrap) {
        int optionIndex = selected - FIRST_OPTION;
        if (optionIndex < 0 || optionIndex >= options.length || setAside(optionIndex)) {
            return;
        }
        Options.Option option = options[optionIndex];
        int index = option.indexOf(Options.read(dir, option)) + delta;
        if (wrap) {
            index = Math.floorMod(index, option.values.length);
        } else if (index < 0 || index >= option.values.length) {
            return;
        }
        String value = option.values[index];
        if (!Options.write(dir, option, value)) {
            return;
        }
        notTaken[optionIndex] = false;
        // Before the game every option is taken as it launches; nothing to apply.
        // The performance tab/overlay and the on-screen controller are the app's own
        // (GameActivity and the deck read their files); nothing for the game to take.
        boolean appOwn = option.file.equals("perf") || option.file.equals("touch-controls")
                || Options.isCheatAction(option.file);  // an amount chosen, not yet done
        if (!preLaunch && option.live && !appOwn
                && !NativeBridge.applyOption(option.file, value)) {
            notTaken[optionIndex] = true;
        }
        // PLAN 10.17: the on-screen controller is an Xbox one, so turning it on makes the
        // game's prompts Xbox's too (the Button prompts row, from the next start in the
        // game); they can still be changed after.
        if (option.file.equals("touch-controls") && value.equals("on")) {
            Options.Option glyphs = Options.byFile("glyphs");
            if (!"xbox".equals(Options.read(dir, glyphs))) {
                Options.write(dir, glyphs, "xbox");
                Log.i(TAG, "options: the on-screen controller on - the button prompts Xbox's");
            }
        }
        render();
    }

    private void render() {
        takeLabels();
        boolean restart = false;
        for (int i = 0; i < options.length; i++) {
            int row = FIRST_OPTION + i;
            Options.Option option = options[i];
            String value = Options.read(dir, option);
            boolean nextStart = pendsRestart(i, value);
            restart |= nextStart;
            // The owner (2026-11-02): an option the game takes only as it starts is marked
            // with an asterisk, the hints below saying what it means.
            names[row].setText(option.live ? option.title : option.title + " *");
            String label = option.labels[option.indexOf(value)] + (nextStart ? " •" : "");
            values[row].setText(row == selected && !setAside(i) ? "‹  " + label + "  ›" : label);
        }
        names[quitRow].setText(preLaunch ? "Close" : "Quit the game");
        values[quitRow].setText("");
        names[photoRow].setText("Photo mode");
        values[photoRow].setText("");
        rows[photoRow].setVisibility(preLaunch ? View.GONE : View.VISIBLE);
        names[exportRow].setText("Exit and export save/logs");
        values[exportRow].setText("");
        rows[exportRow].setVisibility(hidden(exportRow) ? View.GONE : View.VISIBLE);
        names[aboutRow].setText("About and licences");
        values[aboutRow].setText("");
        values[LAUNCH_ROW].setText(launch.ready ? "" : launch.launchValue);
        values[BUILD_ROW].setText(launch.buildValue);
        values[IMAGE_ROW].setText(launch.imageValue);
        values[UPDATE_ROW].setText(launch.updateValue);
        values[LOSSLESS_ROW].setText(launch.losslessValue);
        values[ULTIMATE_ROW].setText(launch.ultimateValue);
        values[RESTORE_ROW].setText("");
        values[MODS_ROW].setText("");
        for (int i = 0; i < rowCount; i++) {
            rows[i].setBackgroundColor(i == selected ? SELECTED : 0);
            boolean aside = (i >= FIRST_OPTION && i < photoRow && setAside(i - FIRST_OPTION))
                    || (i == LAUNCH_ROW && !launch.ready)
                    || (i == BUILD_ROW && !launch.canBuild);
            names[i].setTextColor(aside ? GREY : i == selected ? GOLD : PARCHMENT);
            values[i].setTextColor(aside ? GREY : GOLD);
        }
        resume.setVisibility(preLaunch ? View.GONE : View.VISIBLE);
        if (preLaunch) {
            status.setText(launch.status);
            hints.setText("↑↓ choose    ←→ change    " + confirmLabel + " select    Start launch"
                    + "\nBy touch: tap a row to choose it, then the left or right of its value"
                    + "\n" + RESTART_NOTE);
        } else {
            status.setText("The game is paused");
            hints.setText("↑↓ choose    ←→ change    " + confirmLabel
                    + " change    " + backLabel + " close"
                    + "\nBy touch: tap a row to choose it, then the left or right of its value"
                    + "\n" + RESTART_NOTE);
        }

        String text;
        if (selected == LAUNCH_ROW) {
            text = launch.launchDetail;
        } else if (selected == BUILD_ROW) {
            text = launch.buildDetail;
        } else if (selected == IMAGE_ROW) {
            text = launch.imageDetail;
        } else if (selected == UPDATE_ROW) {
            text = launch.updateDetail;
        } else if (selected == LOSSLESS_ROW) {
            text = launch.losslessDetail;
        } else if (selected == ULTIMATE_ROW) {
            text = launch.ultimateDetail;
        } else if (selected == MODS_ROW) {
            text = "PC mods: add one from a .zip, .7z, .rar, .dazip or a folder; convert PC"
                    + " ones (add-ins too); on or off, which wins. From the next start.";
        } else if (selected == RESTORE_ROW) {
            text = "Your saves to a .zip in Downloads, or in from one (another device's, a"
                    + " backup), and the saves the game wrote over - kept, and put back here.";
        } else if (selected == photoRow) {
            text = "Pauses the world and frees the camera, the HUD hidden: the sticks move and"
                    + " turn it, the triggers lower and raise it, the D-pad zooms and rolls, "
                    + confirmLabel + " saves a photo to Downloads. L3 and R3 together also open it.";
        } else if (selected == aboutRow) {
            text = "The app's version, the plain word that the game is yours and nothing of it"
                    + " ships with the app, and the licences of the code built into it.";
        } else if (selected == exportRow) {
            text = "Debugging Mode: makes a fresh save where the game allows one, ends the game"
                    + " and puts one .zip in Downloads - this run's log and the last one's, the"
                    + " save loaded and the fresh one, a screenshot, how earlier runs ended -"
                    + " to send with a bug report.";
        } else if (selected == quitRow) {
            if (preLaunch) {
                text = "Closes the app.";
            } else {
                text = quitArmed
                        ? "Press " + confirmLabel + " or tap again to quit. Progress since the"
                                + " last save is lost."
                        : "Ends the game." + (restart ? " The options marked • take effect"
                                + " when it starts again." : "");
            }
        } else {
            int optionIndex = selected - FIRST_OPTION;
            Options.Option option = options[optionIndex];
            String value = Options.read(dir, option);
            text = option.details[option.indexOf(value)];
            if (setAside(optionIndex) && option.file.startsWith("cheat-")) {
                text = "In the game only: the cheats run the developers' own scripts on the"
                        + " game in play.";
            } else if (cheatDone != null && Options.isCheatAction(option.file)) {
                text = cheatDone + " - done as the game goes on (close this panel).";
            } else if (setAside(optionIndex) && option.file.equals("motion-blur")) {
                text = "Needs FSR 3.1 on: the blur follows the camera's motion that FSR 3.1"
                        + " is given.";
            } else if (setAside(optionIndex) && option.file.equals("dof")) {
                text = "Needs FSR 3.1 on: the blur is drawn on its upscaled scene.";
            } else if (setAside(optionIndex) && option.file.equals("resolution")) {
                text = "Set aside while FSR 3.1 is on: its profile picks the size the game"
                        + " renders at. Turn FSR 3.1 off to choose it here.";
            } else if (setAside(optionIndex)) {
                text = "Set aside while FSR 3.1 is on: it upscales the game to the panel"
                        + " itself. Screens without a 3D scene, the menus, keep this choice.";
            } else if (pendsRestart(optionIndex, value)) {
                text += " From the next start.";
            } else if (!option.live && !preLaunch) {
                text += " Needs a restart of the game (*).";
            }
        }
        detail.setText(text);
    }

    /** The game's prompts: the bottom button is the game's A whatever it is called. */
    private void takeLabels() {
        String glyphs = Options.read(dir, options[indexOfFile("glyphs")]);
        if ("nintendo".equals(glyphs)) {
            confirmLabel = "B";
            backLabel = "A";
        } else if ("playstation".equals(glyphs)) {
            confirmLabel = "✕";
            backLabel = "○";
        } else {
            confirmLabel = "A";
            backLabel = "B";
        }
    }

    /** What the options hold now, as the game starts with them. */
    private void takeStartValues() {
        for (int i = 0; i < options.length; i++) {
            startValues[i] = Options.read(dir, options[i]);
            notTaken[i] = false;
        }
    }

    private void showLaunchRows(boolean show) {
        int visibility = show ? View.VISIBLE : View.GONE;
        rows[LAUNCH_ROW].setVisibility(visibility);
        rows[BUILD_ROW].setVisibility(visibility);
        rows[IMAGE_ROW].setVisibility(visibility);
        rows[UPDATE_ROW].setVisibility(visibility);
        rows[LOSSLESS_ROW].setVisibility(visibility);
        rows[ULTIMATE_ROW].setVisibility(visibility);
        rows[RESTORE_ROW].setVisibility(visibility);
        rows[MODS_ROW].setVisibility(visibility);
        launchRule.setVisibility(visibility);
    }

    /**
     * An option that cannot be changed now, greyed: Upscaling while FSR 3.1 is chosen - FSR
     * 3.1 hands the presenter its frames at the panel's size, and the presenter's own
     * effects are not run over them (warden_upscale.cc, FrameUpscaled).
     */
    private boolean setAside(int i) {
        boolean fsr3 = !Options.read(dir, options[indexOfFile("fsr3")]).equals("off");
        String file = options[i].file;
        // Motion blur needs FSR 3.1's motion vectors; the others give way to FSR 3.1.
        // PLAN 10.14: the cheats are the game's, in the game only.
        if (file.startsWith("cheat-")) {
            return preLaunch;
        }
        return file.equals("motion-blur") || file.equals("dof") ? !fsr3
                : (file.equals("upscaling") || file.equals("resolution")) && fsr3;
    }

    /**
     * The value differs from what the game took as it started, and the game cannot take it
     * now. Never before the game: everything is taken as it launches.
     */
    private boolean pendsRestart(int i, String value) {
        Options.Option option = options[i];
        return !preLaunch && (!option.live || notTaken[i]) && !value.equals(startValues[i]);
    }

    private int indexOfFile(String file) {
        for (int i = 0; i < options.length; i++) {
            if (options[i].file.equals(file)) {
                return i;
            }
        }
        throw new IllegalArgumentException(file);
    }

    private TextView text(float sp, int colour) {
        TextView view = new TextView(context);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setTextColor(colour);
        view.setTypeface(Typeface.SERIF);
        noFocus(view);
        return view;
    }

    private View rule() {
        View view = new View(context);
        view.setBackgroundColor(GOLD_DIM);
        return view;
    }

    private LinearLayout.LayoutParams ruleParams() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, Math.max(1, dp(1)));
        params.topMargin = dp(8);
        params.bottomMargin = dp(8);
        return params;
    }

    private static void noFocus(View view) {
        view.setFocusable(false);
        view.setFocusableInTouchMode(false);
    }

    private int dp(float value) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
                context.getResources().getDisplayMetrics()));
    }
}
