package dev.daorecompandroid.duo;

import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.BatteryManager;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * The second screen's deck (PLAN 8.2, 8.5, 8.6): a tab rail, the chosen tab, a footer.
 * Read-only (D6) but for one write path, the Quickbar tab's taps (PLAN 8.8): everything it
 * shows is the state mirror's - what the game's own UI is shown - or the frame counts. Updated about ten times a second by its host, which pulls
 * the data; the view never calls into the game.
 *
 * <p>The look is SECOND-SCREEN.md §4's: dark stone and aged brass, parchment for text,
 * serif type - the game's own fonts and frame art are 8.4's, to come. Tabs answer a tap
 * and cross-fade.
 */
final class DeckView extends LinearLayout {

    // SECOND-SCREEN.md §4's palette.
    static final int DEEP = 0xFF0E0B09;
    static final int STONE = 0xFF1A1613;
    static final int EDGE = 0xFF2E2822;
    static final int BRASS = 0xFFC9A227;
    static final int BRASS_DIM = 0xFF8C6D1F;
    static final int PARCHMENT = 0xFFD9C9A3;
    static final int INK_ON_PARCHMENT = 0xFF2B2318;
    static final int TEXT = 0xFFD8CDB5;
    static final int FAINT = 0xFF8A8070;
    static final int BLOOD = 0xFF7A1518;
    static final int HEALTH = 0xFFA3201F;
    static final int MANA = 0xFF2E5C8A;
    static final int STAMINA = 0xFFB8912E;
    static final int STEEL = 0xFF4A4845;

    /** A tab: its rail label, its view, and what it makes of each update. */
    private abstract static class Tab {
        final String label;
        View view;

        Tab(String label) {
            this.label = label;
        }

        abstract void update(Map<String, String> values, long[] frames);
    }

    private final Context context;
    /** PLAN 8.4: the game's own faces (DeckFonts) - Nogard for headings, Dragon Text for
     * the rest - or the system's serif until they are made. */
    private final Typeface headingFace;
    private final Typeface bodyFace;
    /** PLAN 8.4: the HUD's own art (DeckArt), or null - plain frames until it is read. */
    private final DeckArt art;
    private final List<Tab> tabs = new ArrayList<>();
    private final List<TextView> railLabels = new ArrayList<>();
    private final FrameLayout content;
    private final TextView footerLeft;
    private final TextView footerRight;
    private int current = -1;
    /** The Perf tab's place, and whether the "Performance tab/overlay" option shows it. */
    private int perfIndex = -1;
    private boolean perfVisible = true;
    private Map<String, String> lastValues = Collections.emptyMap();
    private long[] lastFrames;

    DeckView(Context context, boolean showData) {
        this(context, showData, null, null);
    }

    DeckView(Context context, boolean showData, DeckFonts fonts, DeckArt art) {
        super(context);
        this.context = context;
        this.art = art;
        headingFace = fonts != null ? fonts.heading : Typeface.create(Typeface.SERIF, Typeface.BOLD);
        bodyFace = fonts != null ? fonts.body : Typeface.SERIF;
        setOrientation(VERTICAL);
        setBackgroundColor(DEEP);

        partyTab = new PartyTab();
        tabs.add(partyTab);
        tabs.add(new QuickbarTab());
        tabs.add(new QuestsTab());
        tabs.add(new MapTab());
        tabs.add(new TacticsTab());
        tabs.add(new CodexTab());
        tabs.add(new ItemsTab());
        perfIndex = tabs.size();
        tabs.add(new PerformanceTab());
        if (showData) {
            tabs.add(new DataTab());
        }

        LinearLayout rail = new LinearLayout(context);
        rail.setOrientation(HORIZONTAL);
        rail.setBackgroundColor(STONE);
        rail.setPadding(dp(6), dp(6), dp(6), dp(4));
        for (int i = 0; i < tabs.size(); i++) {
            TextView label = text(15, TEXT);
            label.setText(tabs.get(i).label);
            label.setGravity(Gravity.CENTER);
            label.setTypeface(headingFace);
            label.setMinHeight(dp(52));
            final int index = i;
            label.setOnClickListener(v -> select(index));
            rail.addView(label, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            railLabels.add(label);
        }
        addView(rail, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        if (art != null) {
            // The game's ornate divider across the rail's foot.
            FrameLayout divider = new FrameLayout(context);
            divider.setBackgroundColor(STONE);
            View line = rule(BRASS_DIM);
            FrameLayout.LayoutParams lineParams = new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, dp(1), Gravity.CENTER_VERTICAL);
            divider.addView(line, lineParams);
            android.widget.ImageView ornament = new android.widget.ImageView(context);
            ornament.setImageBitmap(art.get(DeckArt.DIVIDER));
            ornament.setScaleType(android.widget.ImageView.ScaleType.FIT_CENTER);
            divider.addView(ornament, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, dp(26), Gravity.CENTER));
            addView(divider, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(26)));
        } else {
            addView(rule(BRASS_DIM), new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1)));
        }

        content = new FrameLayout(context);
        content.setPadding(dp(12), dp(10), dp(12), dp(10));
        addView(content, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        addView(rule(EDGE), new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1)));
        LinearLayout footer = new LinearLayout(context);
        footer.setOrientation(HORIZONTAL);
        footer.setPadding(dp(12), dp(4), dp(12), dp(6));
        footerLeft = text(12, FAINT);
        footerRight = text(12, FAINT);
        footerRight.setGravity(Gravity.END);
        footer.addView(footerLeft, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        footer.addView(footerRight);
        addView(footer, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        for (Tab tab : tabs) {
            tab.view.setVisibility(GONE);
            content.addView(tab.view, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }
        select(0);
    }

    /** New data, on the UI thread: the current tab updates, the footer with it. */
    void update(Map<String, String> values, long[] frames, boolean connected) {
        // Sorted once a pull: the tabs read ranges of keys from it.
        values = new java.util.TreeMap<>(values);
        lastValues = values;
        lastFrames = frames;
        tabs.get(current).update(values, frames);
        footerLeft.setText(connected ? "Dragon Age: Origins" : "Waiting for the game…");
        footerRight.setText(connected ? values.size() + " values" : "");
    }

    /**
     * The "Performance tab/overlay" option: off hides the Perf tab (and leaves it for the
     * Party tab if it was showing), on shows it. Cheap to call every pull.
     */
    void setPerfVisible(boolean visible) {
        if (visible == perfVisible || perfIndex < 0) {
            return;
        }
        perfVisible = visible;
        railLabels.get(perfIndex).setVisibility(visible ? VISIBLE : GONE);
        if (!visible && current == perfIndex) {
            select(0);
        }
    }

    private void select(int index) {
        if (index == current) {
            return;
        }
        if (current >= 0) {
            View old = tabs.get(current).view;
            old.animate().alpha(0f).setDuration(180).withEndAction(() -> old.setVisibility(GONE));
        }
        current = index;
        View now = tabs.get(index).view;
        now.setAlpha(0f);
        now.setVisibility(VISIBLE);
        now.animate().alpha(1f).setDuration(240);
        for (int i = 0; i < railLabels.size(); i++) {
            TextView label = railLabels.get(i);
            label.setTextColor(i == index ? BRASS : FAINT);
            label.setBackground(i != index ? null : art != null
                    ? new ThreeSlice(art.get(DeckArt.TAB_SELECTED), 0.12f)
                    : frame(STONE, BRASS_DIM));
        }
        tabs.get(index).update(lastValues, lastFrames);
    }

    // ---- the tabs ----

    /**
     * Party (8.5's first): a card for each member the HUD's portraits show - the tokens
     * the portraits observe (run 454): Party.Members.N.Portrait, .Attributes.7 (health,
     * the UI's PROP_HEALTH) and .8 (mana or stamina, PROP_ENERGY - .Stats.ClassHasMana
     * says which) as .Current and .Total, .Poisoned, .Paralyzed, .PortraitLevelUp and
     * .UserSelected. The HUD observes only the selected character's name
     * (GUI.SelectedChar.Name), so each name is learnt as its member is selected alone, by
     * portrait; until then the portrait's own name stands in.
     */
    private final class PartyTab extends Tab {
        private static final int MEMBERS = 4;
        private final TextView heading;
        private final TextView empty;
        private final MemberCard[] cards = new MemberCard[MEMBERS];
        /** Names learnt by portrait, while the deck runs. */
        private final Map<String, String> names = new java.util.HashMap<>();

        PartyTab() {
            super("Party");
            LinearLayout column = new LinearLayout(context);
            column.setOrientation(VERTICAL);
            heading = text(13, FAINT);
            column.addView(heading);
            empty = text(15, FAINT);
            empty.setText("The party appears here once the game shows its portraits.");
            empty.setPadding(0, dp(16), 0, 0);
            column.addView(empty);
            for (int i = 0; i < MEMBERS; i++) {
                cards[i] = new MemberCard();
                LayoutParams params = new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
                params.topMargin = dp(6);
                column.addView(cards[i].root, params);
            }
            view = column;
        }

        void setFace(int slot, Bitmap face) {
            if (slot >= 0 && slot < MEMBERS && cards[slot].ring != null) {
                cards[slot].ring.setFace(face);
            }
        }

        @Override
        void update(Map<String, String> values, long[] frames) {
            // The selected character's name, when one alone is selected, is theirs.
            String selectedName = values.get("GUI.SelectedChar.Name");
            if (selectedName != null && !selectedName.isEmpty()
                    && "1".equals(values.get("Party.SelectedCount"))) {
                for (int i = 0; i < MEMBERS; i++) {
                    String prefix = "Party.Members." + i + ".";
                    String portrait = values.get(prefix + "Portrait");
                    if ("1".equals(values.get(prefix + "UserSelected")) && portrait != null
                            && !portrait.isEmpty()) {
                        names.put(portrait, selectedName);
                    }
                }
            }
            int shown = 0;
            for (int i = 0; i < MEMBERS; i++) {
                String prefix = "Party.Members." + i + ".";
                String portrait = values.get(prefix + "Portrait");
                boolean present = portrait != null && !portrait.isEmpty()
                        && number(values.get(prefix + "Attributes.7.Total")) > 0;
                cards[i].root.setVisibility(present ? VISIBLE : INVISIBLE);
                if (!present) {
                    continue;
                }
                shown++;
                // The member's own name (the deck's reads), else learnt, else the portrait's.
                String name = values.get(prefix + "Name");
                if (name == null || name.isEmpty()) {
                    name = names.get(portrait);
                }
                cards[i].show(name != null ? name : portraitName(portrait), prefix, values);
            }
            empty.setVisibility(shown == 0 ? VISIBLE : GONE);
            String potions = values.get("Party.HealingPotionCount");
            heading.setText(gameMode(values.get("GameModeController.CurrentGameMode"))
                    + (potions != null ? "   ·   healing potions " + potions : ""));
        }
    }

    // ---- PLAN 8.8: the touch quickbar, the deck's one write path ----

    /** Where a quickbar tap goes: the host sends it to the game, off the UI thread. */
    interface Quickbar {
        void tap(int slot);
    }

    private Quickbar quickbar;
    private QuickbarTab quickbarTab;
    private PartyTab partyTab;

    /** A party slot's face, as the game last drew its portrait. UI thread. */
    void setFace(int slot, Bitmap face) {
        if (partyTab != null) {
            partyTab.setFace(slot, face);
        }
    }

    void setQuickbar(Quickbar sink) {
        quickbar = sink;
    }

    /** The game's answer to a tap: "" pressed, else why not; null when unreachable. */
    void quickslotAnswer(int slot, String why) {
        if (quickbarTab != null) {
            quickbarTab.answer(slot, why);
        }
    }

    /**
     * The selected character's six quick slots as the HUD observes them
     * (GUI.SelectedChar.Stats.QuickSlots.N: the icon, the cooldown, the stack, whether it
     * can be used), large enough to hit with a thumb. A tap presses the slot's own button
     * in the game - the face button for 0-2, with RT held for 3-5 - so the game does
     * exactly what that press does, no more; the game refuses taps outside exploration and
     * combat. The buttons are named as the Button prompts option draws them.
     */
    private final class QuickbarTab extends Tab {
        private static final int SLOTS = 6;
        private final TextView heading;
        private final TextView note;
        private final QuickTile[] tiles = new QuickTile[SLOTS];
        private final String[] buttonNames;
        private final String triggerName;
        private long noteUntil;

        QuickbarTab() {
            super("Quickbar");
            String glyphs = readWord(new java.io.File(context.getFilesDir(), "glyphs"));
            // Slots 0-2 are the left, top and right face buttons (X, Y, B on an Xbox pad).
            if ("nintendo".equals(glyphs)) {
                buttonNames = new String[] {"Y", "X", "A"};
                triggerName = "ZR";
            } else if ("playstation".equals(glyphs)) {
                buttonNames = new String[] {"□", "△", "○"};
                triggerName = "R2";
            } else {
                buttonNames = new String[] {"X", "Y", "B"};
                triggerName = "RT";
            }
            LinearLayout column = new LinearLayout(context);
            column.setOrientation(VERTICAL);
            heading = text(15, PARCHMENT);
            heading.setTypeface(headingFace);
            column.addView(heading);
            note = text(13, FAINT);
            note.setText("A tap presses the slot's own button in the game.");
            note.setPadding(0, dp(2), 0, dp(6));
            column.addView(note);
            for (int row = 0; row < 2; row++) {
                LinearLayout line = new LinearLayout(context);
                line.setOrientation(HORIZONTAL);
                for (int c = 0; c < 3; c++) {
                    final int slot = row * 3 + c;
                    QuickTile tile = new QuickTile(context,
                            (row == 1 ? triggerName + " + " : "") + buttonNames[c]);
                    tile.setOnClickListener(v -> {
                        if (quickbar != null && tile.occupied) {
                            tile.flash();
                            quickbar.tap(slot);
                        }
                    });
                    tiles[slot] = tile;
                    LayoutParams params = new LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f);
                    params.leftMargin = c == 0 ? 0 : dp(8);
                    line.addView(tile, params);
                }
                LayoutParams rowParams = new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
                rowParams.topMargin = row == 0 ? 0 : dp(8);
                column.addView(line, rowParams);
            }
            view = column;
            quickbarTab = this;
        }

        void answer(int slot, String why) {
            if (why == null) {
                note.setText("The game cannot be reached.");
            } else if (why.isEmpty()) {
                note.setText("Pressed " + tiles[slot].button + ".");
            } else {
                note.setText("Not pressed: " + why + ".");
            }
            note.setTextColor(why != null && why.isEmpty() ? BRASS : FAINT);
            noteUntil = System.currentTimeMillis() + 2500;
        }

        @Override
        void update(Map<String, String> values, long[] frames) {
            String name = values.get("GUI.SelectedChar.Name");
            heading.setText(name != null && !name.isEmpty() ? name + "'s quickbar" : "Quickbar");
            if (noteUntil != 0 && System.currentTimeMillis() > noteUntil) {
                noteUntil = 0;
                note.setText("A tap presses the slot's own button in the game.");
                note.setTextColor(FAINT);
            }
            String mode = values.get("GameModeController.CurrentGameMode");
            boolean usable = "0".equals(mode) || "1".equals(mode);
            for (int i = 0; i < SLOTS; i++) {
                if (i == 2) {
                    // The right face button is the healing potion on the 360's HUD, whatever
                    // slot 2 holds (Survival there in run 544 - a passive skill no button
                    // uses): the tile shows and presses what the HUD does.
                    int potions = (int) number(values.get("Party.HealingPotionCount"));
                    tiles[i].set(true, "ico_hlth_poultice", 0, potions, potions <= 0, false, usable);
                    tiles[i].setTitle("Health poultice");
                    tiles[i].countAlways = true;
                    continue;
                }
                String prefix = "GUI.SelectedChar.Stats.QuickSlots." + i;
                String icon = values.get(prefix + ".ItemIcon");
                String value = values.get(prefix);
                boolean occupied = icon != null && !icon.isEmpty() && value != null
                        && !"0".equals(value);
                double cooldown = number(values.get(prefix + ".CooldownPercentage"));
                if (cooldown > 1) {
                    cooldown /= 100.0;
                }
                tiles[i].set(occupied, occupied ? icon : null,
                        Math.max(0, Math.min(1, cooldown)),
                        (int) number(values.get(prefix + ".SourceItem.StackSize")),
                        "1".equals(values.get(prefix + ".IsError")),
                        "1".equals(values.get(prefix + ".PendingActivation")), usable);
            }
        }
    }

    /** One quick slot: its icon, the button, a readable name, the cooldown's shade. */
    private final class QuickTile extends View {
        final String button;
        boolean occupied;
        private Bitmap icon;
        private String iconName;
        private String title = "";
        private double cooldown;
        private int stack;
        private boolean error;
        private boolean pending;
        private boolean usable;
        private long flashAt;
        /** The count shown even at 0 and 1 (the healing potions). */
        boolean countAlways;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        private final android.graphics.RectF box = new android.graphics.RectF();
        private final android.graphics.Rect dest = new android.graphics.Rect();

        QuickTile(Context context, String button) {
            super(context);
            this.button = button;
            setClickable(true);
        }

        void set(boolean occupied, String iconName, double cooldown, int stack, boolean error,
                boolean pending, boolean usable) {
            boolean changed = occupied != this.occupied || cooldown != this.cooldown
                    || stack != this.stack || error != this.error || pending != this.pending
                    || usable != this.usable
                    || (iconName == null ? this.iconName != null : !iconName.equals(this.iconName));
            this.occupied = occupied;
            if (iconName == null ? this.iconName != null : !iconName.equals(this.iconName)) {
                this.iconName = iconName;
                icon = art != null && iconName != null ? art.icon(iconName) : null;
                title = iconName != null ? readableName(iconName) : "";
            }
            this.cooldown = cooldown;
            this.stack = stack;
            this.error = error;
            this.pending = pending;
            this.usable = usable;
            if (changed) {
                invalidate();
            }
        }

        void flash() {
            flashAt = android.os.SystemClock.uptimeMillis();
            invalidate();
        }

        void setTitle(String text) {
            if (!text.equals(title)) {
                title = text;
                invalidate();
            }
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float w = getWidth(), h = getHeight();
            long since = android.os.SystemClock.uptimeMillis() - flashAt;
            boolean flashing = since < 300;
            box.set(dp(1), dp(1), w - dp(1), h - dp(1));
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(flashing ? EDGE : STONE);
            canvas.drawRoundRect(box, dp(6), dp(6), paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(dp(flashing || pending ? 2.5f : 1.2f));
            paint.setColor(flashing || pending ? BRASS : occupied && usable ? BRASS_DIM : EDGE);
            canvas.drawRoundRect(box, dp(6), dp(6), paint);
            paint.setStyle(Paint.Style.FILL);
            // The button, top-left.
            paint.setTypeface(headingFace);
            paint.setTextSize(dp(15));
            paint.setColor(occupied ? BRASS : FAINT);
            canvas.drawText(button, dp(10), dp(22), paint);
            if (!occupied) {
                paint.setTypeface(bodyFace);
                paint.setTextSize(dp(13));
                paint.setColor(FAINT);
                paint.setTextAlign(Paint.Align.CENTER);
                canvas.drawText("empty", w / 2, h / 2 + dp(5), paint);
                paint.setTextAlign(Paint.Align.LEFT);
                if (flashing) {
                    postInvalidateDelayed(300 - since);
                }
                return;
            }
            // The icon, centred, as large as the tile leaves room for.
            float side = Math.min(w - dp(24), h - dp(58));
            int left = (int) ((w - side) / 2), top = (int) dp(30);
            dest.set(left, top, (int) (left + side), (int) (top + side));
            if (icon != null) {
                paint.setAlpha(usable && !error ? 255 : 110);
                canvas.drawBitmap(icon, null, dest, paint);
                paint.setAlpha(255);
            } else {
                paint.setColor(EDGE);
                canvas.drawRect(dest, paint);
            }
            // The cooldown: the icon shaded from the top by what is left of it.
            if (cooldown > 0.001) {
                paint.setColor(0xB0000000);
                canvas.drawRect(dest.left, dest.top, dest.right,
                        dest.top + (float) (side * cooldown), paint);
            }
            // The stack, bottom-right of the icon.
            if (stack > 1 || countAlways) {
                paint.setTypeface(headingFace);
                paint.setTextSize(dp(15));
                paint.setColor(PARCHMENT);
                paint.setTextAlign(Paint.Align.RIGHT);
                canvas.drawText("×" + stack, dest.right - dp(2), dest.bottom - dp(4), paint);
                paint.setTextAlign(Paint.Align.LEFT);
            }
            // The name, under the icon.
            paint.setTypeface(bodyFace);
            paint.setTextSize(dp(13));
            paint.setColor(error ? FAINT : TEXT);
            paint.setTextAlign(Paint.Align.CENTER);
            String shown = TextUtils.ellipsize(title, new android.text.TextPaint(paint),
                    w - dp(12), TextUtils.TruncateAt.END).toString();
            canvas.drawText(shown, w / 2, h - dp(10), paint);
            paint.setTextAlign(Paint.Align.LEFT);
            if (flashing) {
                postInvalidateDelayed(300 - since);
            }
        }
    }

    /**
     * A readable name from an icon's: "skl_ico_survival_4" -> "Survival",
     * "spl_ico_winters_grasp" -> "Winters Grasp". The HUD itself shows only the icon.
     */
    private static String readableName(String icon) {
        String s = icon.toLowerCase(Locale.ROOT);
        int ico = s.indexOf("ico_");
        if (ico >= 0) {
            s = s.substring(ico + 4);
        }
        s = s.replaceAll("_\\d+$", "").replace('_', ' ').trim();
        StringBuilder out = new StringBuilder();
        for (String word : s.split(" ")) {
            if (word.isEmpty()) {
                continue;
            }
            if (out.length() > 0) {
                out.append(' ');
            }
            out.append(Character.toUpperCase(word.charAt(0))).append(word.substring(1));
        }
        return out.toString();
    }

    private static String readWord(java.io.File file) {
        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            String line = reader.readLine();
            return line == null ? "" : line.trim();
        } catch (Exception e) {
            return "";
        }
    }

    /** One member: the name, health and mana or stamina as bars, the conditions. */
    private final class MemberCard {
        final LinearLayout root;
        final TextView name;
        final TextView status;
        final Bar health;
        final Bar energy;

        PortraitRing ring;
        private final LinearLayout column;

        MemberCard() {
            root = new LinearLayout(context);
            root.setOrientation(HORIZONTAL);
            root.setGravity(Gravity.CENTER_VERTICAL);
            root.setPadding(dp(10), dp(6), dp(10), dp(6));
            root.setBackground(frame(STONE, EDGE));
            if (art != null) {
                ring = new PortraitRing(context);
                LayoutParams ringParams = new LayoutParams(dp(78), dp(78));
                ringParams.rightMargin = dp(10);
                root.addView(ring, ringParams);
            }
            column = new LinearLayout(context);
            column.setOrientation(VERTICAL);
            root.addView(column, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            LinearLayout top = new LinearLayout(context);
            top.setOrientation(HORIZONTAL);
            name = text(18, PARCHMENT);
            name.setTypeface(headingFace);
            name.setSingleLine(true);
            status = text(13, BRASS);
            status.setGravity(Gravity.END);
            top.addView(name, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            top.addView(status);
            column.addView(top);
            health = new Bar(HEALTH);
            energy = new Bar(STAMINA);
            column.addView(health.root);
            column.addView(energy.root);
        }

        void show(String title, String prefix, Map<String, String> values) {
            boolean selected = "1".equals(values.get(prefix + "UserSelected"));
            name.setText(title);
            name.setTextColor(selected ? BRASS : PARCHMENT);
            root.setBackground(frame(STONE, selected ? BRASS_DIM : EDGE));
            List<String> marks = new ArrayList<>();
            if ("1".equals(values.get(prefix + "Poisoned"))) {
                marks.add("poisoned");
            }
            if ("1".equals(values.get(prefix + "Paralyzed"))) {
                marks.add("paralysed");
            }
            if ("1".equals(values.get(prefix + "PortraitLevelUp"))) {
                marks.add("level up");
            }
            if ("1".equals(values.get(prefix + "IsDead"))) {
                marks.add("fallen");
            }
            String kind = (orEmpty(values.get(prefix + "Stats.RaceName")) + " "
                    + orEmpty(values.get(prefix + "Stats.ClassName"))).trim();
            if (!kind.isEmpty()) {
                marks.add(0, kind);
            }
            status.setText(TextUtils.join(" · ", marks));
            health.set("Health", number(values.get(prefix + "Attributes.7.Current")),
                    number(values.get(prefix + "Attributes.7.Total")), HEALTH);
            boolean mana = "1".equals(values.get(prefix + "Stats.ClassHasMana"));
            energy.set(mana ? "Mana" : "Stamina",
                    number(values.get(prefix + "Attributes.8.Current")),
                    number(values.get(prefix + "Attributes.8.Total")), mana ? MANA : STAMINA);
            if (ring != null) {
                ring.set(title, fraction(values.get(prefix + "Attributes.7.Current"),
                        values.get(prefix + "Attributes.7.Total")),
                        fraction(values.get(prefix + "Attributes.8.Current"),
                                values.get(prefix + "Attributes.8.Total")), mana, selected);
            }
        }
    }

    private static float fraction(String current, String total) {
        double t = number(total);
        return t > 0 ? (float) Math.max(0, Math.min(1, number(current) / t)) : 0f;
    }

    /**
     * The HUD's portrait as the game draws it, from its atlas: the dark disc, the health
     * arc on the left and the mana (blue) or stamina (gold) arc on the right - each shown
     * up to its fraction from the bottom - the gold ring, glowing for the selected member;
     * in the middle the member's face as the game last drew it (DeckFaces - the portraits
     * are rendered from the 3D models as the game runs, not files), or their initial
     * until it has.
     */
    private final class PortraitRing extends View {
        /** The face's circle, a fraction of the ring's size: inside the gold ring. */
        private static final float FACE_RADIUS = 0.42f;
        private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG | Paint.ANTI_ALIAS_FLAG);
        private final Paint letter = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final android.graphics.RectF box = new android.graphics.RectF();
        private final android.graphics.Path inner = new android.graphics.Path();
        private Bitmap face;
        private String initial = "";
        private float health;
        private float energy;
        private boolean mana;
        private boolean selected;

        PortraitRing(Context context) {
            super(context);
            letter.setColor(PARCHMENT);
            letter.setTypeface(headingFace);
            letter.setTextAlign(Paint.Align.CENTER);
        }

        void set(String name, float health, float energy, boolean mana, boolean selected) {
            String first = name.isEmpty() ? "" : name.substring(0, 1);
            if (first.equals(initial) && health == this.health && energy == this.energy
                    && mana == this.mana && selected == this.selected) {
                return;
            }
            initial = first;
            this.health = health;
            this.energy = energy;
            this.mana = mana;
            this.selected = selected;
            invalidate();
        }

        void setFace(Bitmap face) {
            this.face = face;
            invalidate();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float size = Math.min(getWidth(), getHeight());
            box.set((getWidth() - size) / 2, (getHeight() - size) / 2,
                    (getWidth() + size) / 2, (getHeight() + size) / 2);
            canvas.drawBitmap(art.get(DeckArt.PORTRAIT_DISC), null, box, paint);
            if (face != null) {
                // Inside the ring, as the HUD shows it.
                float radius = size * FACE_RADIUS;
                inner.reset();
                inner.addCircle(box.centerX(), box.centerY(), radius,
                        android.graphics.Path.Direction.CW);
                canvas.save();
                canvas.clipPath(inner);
                canvas.drawBitmap(face, null, new android.graphics.RectF(
                        box.centerX() - radius, box.centerY() - radius,
                        box.centerX() + radius, box.centerY() + radius), paint);
                canvas.restore();
            } else {
                letter.setTextSize(size * 0.42f);
                canvas.drawText(initial, box.centerX(), box.centerY() + size * 0.15f, letter);
            }
            arc(canvas, DeckArt.HEALTH_ARC, health);
            arc(canvas, mana ? DeckArt.MANA_ARC : DeckArt.STAMINA_ARC, energy);
            canvas.drawBitmap(art.get(selected ? DeckArt.PORTRAIT_SELECTED : DeckArt.PORTRAIT_RING),
                    null, box, paint);
        }

        private void arc(Canvas canvas, String cell, float part) {
            if (part <= 0f) {
                return;
            }
            canvas.save();
            canvas.clipRect(box.left, box.bottom - box.height() * part, box.right, box.bottom);
            canvas.drawBitmap(art.get(cell), null, box, paint);
            canvas.restore();
        }
    }

    /** A bar from the game's art: its two ends kept, its middle stretched to the width. */
    private static final class ThreeSlice extends android.graphics.drawable.Drawable {
        private final Bitmap bitmap;
        private final float cap;
        private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);

        ThreeSlice(Bitmap bitmap, float capFraction) {
            this.bitmap = bitmap;
            this.cap = capFraction;
        }

        @Override
        public void draw(Canvas canvas) {
            android.graphics.Rect bounds = getBounds();
            int w = bitmap.getWidth();
            int h = bitmap.getHeight();
            int capPixels = Math.max(1, Math.round(w * cap));
            int capOut = Math.round(capPixels * bounds.height() / (float) h);
            canvas.drawBitmap(bitmap, new android.graphics.Rect(0, 0, capPixels, h),
                    new android.graphics.Rect(bounds.left, bounds.top, bounds.left + capOut,
                            bounds.bottom), paint);
            canvas.drawBitmap(bitmap, new android.graphics.Rect(capPixels, 0, w - capPixels, h),
                    new android.graphics.Rect(bounds.left + capOut, bounds.top,
                            bounds.right - capOut, bounds.bottom), paint);
            canvas.drawBitmap(bitmap, new android.graphics.Rect(w - capPixels, 0, w, h),
                    new android.graphics.Rect(bounds.right - capOut, bounds.top, bounds.right,
                            bounds.bottom), paint);
        }

        @Override
        public void setAlpha(int alpha) {
            paint.setAlpha(alpha);
        }

        @Override
        public void setColorFilter(android.graphics.ColorFilter filter) {
            paint.setColorFilter(filter);
        }

        @Override
        public int getOpacity() {
            return android.graphics.PixelFormat.TRANSLUCENT;
        }
    }

    /** A labelled bar: the label, the bar, current / total. */
    private final class Bar {
        final LinearLayout root;
        final TextView label;
        final TextView amount;
        final View fill;
        final View rest;

        Bar(int colour) {
            root = new LinearLayout(context);
            root.setOrientation(HORIZONTAL);
            root.setGravity(Gravity.CENTER_VERTICAL);
            root.setPadding(0, dp(3), 0, 0);
            label = text(12, FAINT);
            root.addView(label, new LayoutParams(dp(56), ViewGroup.LayoutParams.WRAP_CONTENT));
            LinearLayout track = new LinearLayout(context);
            track.setOrientation(HORIZONTAL);
            track.setBackgroundColor(STEEL);
            fill = new View(context);
            fill.setBackgroundColor(colour);
            rest = new View(context);
            track.addView(fill, new LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 0f));
            track.addView(rest, new LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f));
            root.addView(track, new LayoutParams(0, dp(10), 1f));
            amount = text(12, TEXT);
            amount.setGravity(Gravity.END);
            root.addView(amount, new LayoutParams(dp(72), ViewGroup.LayoutParams.WRAP_CONTENT));
        }

        void set(String what, double current, double total, int colour) {
            label.setText(what);
            fill.setBackgroundColor(colour);
            float part = total > 0 ? (float) Math.max(0, Math.min(1, current / total)) : 0f;
            ((LayoutParams) fill.getLayoutParams()).weight = part;
            ((LayoutParams) rest.getLayoutParams()).weight = 1f - part;
            fill.requestLayout();
            amount.setText(Math.round(current) + " / " + Math.round(total));
        }
    }

    private static double number(String text) {
        if (text == null || text.isEmpty()) {
            return 0;
        }
        try {
            return Double.parseDouble(text);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    /**
     * "portrait_gen00fl_alistair" -> "Alistair" (the last word after the module's code):
     * until the member's own name is learnt. The player's is "portrait_player0".
     */
    private static String portraitName(String portrait) {
        String[] words = portrait.split("_");
        String name = "";
        for (int i = words.length - 1; i >= 0 && name.isEmpty(); i--) {
            String word = words[i].replaceAll("[0-9]+$", "");
            if (!word.isEmpty() && word.chars().allMatch(Character::isLetter)
                    && !word.equals("portrait")) {
                name = word;
            }
        }
        if (name.isEmpty() || name.equals("player")) {
            return "The Warden";
        }
        return Character.toUpperCase(name.charAt(0)) + name.substring(1);
    }

    /** The game's mode (warden_game.cc's list), in words. */
    private static String gameMode(String mode) {
        if (mode == null) {
            return "";
        }
        switch (mode) {
            case "0": return "Exploring";
            case "1": return "In combat";
            case "2": return "Fallen";
            case "3": return "In the menus";
            case "4": return "In conversation";
            case "5": return "Cutscene";
            case "8": return "Title screen";
            case "9": return "Loading";
            case "11": return "Character creation";
            case "12": return "Levelling up";
            default: return "";
        }
    }

    // ---- the journal's, the inventory's and the tactics' data: the deck's own reads ----
    //
    // warden_deck.cc reads these paths every few seconds while the deck pulls (its
    // patterns): Party.Members.N.{Name, Stats.*, Equipment.S.*, Tactics.*},
    // Party.Backpack.I.*, Party.Money.*, Journal.PlotList.*, Journal.PlotGivers.*,
    // MapNoteList.MapNotes.*, Journal.Conversations.*, Journal.CodexGroups.*.

    /** A tab whose body is one scrolling column, rebuilt when its data changes. */
    private abstract class ListTab extends Tab {
        final LinearLayout column;
        private String shownKey = "";

        ListTab(String label) {
            super(label);
            ScrollView scroll = new ScrollView(context);
            column = new LinearLayout(context);
            column.setOrientation(VERTICAL);
            scroll.addView(column);
            view = scroll;
        }

        /** What the body shows, as one string: rebuilt only when it changes. */
        abstract String key(Map<String, String> values);

        abstract void build(Map<String, String> values);

        @Override
        void update(Map<String, String> values, long[] frames) {
            String key = key(values);
            if (key.equals(shownKey)) {
                return;
            }
            shownKey = key;
            column.removeAllViews();
            build(values);
        }

        TextView heading(String text) {
            TextView view = text(17, BRASS);
            view.setTypeface(headingFace);
            view.setText(text);
            if (art != null) {
                view.setBackground(new ThreeSlice(art.get(DeckArt.HEADING_BAR), 0.06f));
                view.setPadding(dp(14), dp(5), dp(14), dp(5));
                LayoutParams params = new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT);
                params.topMargin = dp(8);
                params.bottomMargin = dp(3);
                column.addView(view, params);
            } else {
                view.setPadding(0, dp(8), 0, dp(2));
                column.addView(view);
            }
            return view;
        }

        TextView line(String text, int sp, int colour) {
            TextView view = text(sp, colour);
            view.setText(rich(text));
            view.setPadding(dp(8), dp(1), 0, dp(1));
            column.addView(view);
            return view;
        }

        /** A line with the item's icon (the game's own, DeckArt) before its text. */
        void itemLine(String iconName, String text) {
            Bitmap icon = art != null ? art.icon(iconName) : null;
            if (icon == null) {
                line(text, 14, TEXT);
                return;
            }
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(8), dp(2), 0, dp(2));
            android.widget.ImageView image = new android.widget.ImageView(context);
            image.setImageBitmap(icon);
            row.addView(image, new LayoutParams(dp(30), dp(30)));
            TextView label = text(14, TEXT);
            label.setText(text);
            label.setPadding(dp(8), 0, 0, 0);
            row.addView(label);
            column.addView(row);
        }

        void note(String text) {
            TextView view = text(15, FAINT);
            view.setText(text);
            view.setPadding(0, dp(16), 0, 0);
            column.addView(view);
        }
    }

    /**
     * The game's text markup as the deck shows it: {@code <emp>...</emp>}, its emphasis, in
     * bold; any other tag dropped; line breaks kept. Text without a tag is returned as is.
     */
    static CharSequence rich(String text) {
        if (text == null || text.indexOf('<') < 0) {
            return text;
        }
        // The emphasis marked with two private-use characters (which trim() keeps), every
        // tag dropped, the rest escaped, then the marks made bold.
        String plain = text.replaceAll("(?i)<emp>", "")
                .replaceAll("(?i)</emp>", "")
                .replaceAll("<[^>]*>", "");
        // A text that opens with its emphasis - a quest's objective before its
        // description - has it on a line of its own, as the journal does.
        if (plain.trim().startsWith("") && plain.indexOf('') > 0) {
            int end = plain.indexOf('') + 1;
            plain = plain.substring(0, end) + "\n" + plain.substring(end).trim();
        }
        String html = android.text.TextUtils.htmlEncode(plain)
                .replace("", "<b>")
                .replace("", "</b>")
                .replace("\n", "<br>");
        return android.text.Html.fromHtml(html, android.text.Html.FROM_HTML_MODE_LEGACY);
    }

    /** Quests: the journal's plots and objectives, the plot followed, the givers nearby. */
    private final class QuestsTab extends ListTab {
        QuestsTab() {
            super("Quests");
        }

        @Override
        String key(Map<String, String> values) {
            return subtree(values, "Journal.PlotList.") + subtree(values, "Journal.PlotGivers.")
                    + values.get("Journal.ActivePlot") + values.get("Party.InCamp");
        }

        @Override
        void build(Map<String, String> values) {
            List<Integer> plots = indices(values, "Journal.PlotList.");
            if ("1".equals(values.get("Party.InCamp"))) {
                line("In the party's camp", 13, FAINT);
            }
            if (plots.isEmpty()) {
                note("No quests in the journal yet.");
            } else {
                heading("Quests");
            }
            // Each quest's name and its text, as the journal shows them - the open ones
            // first, then the completed. The plot's other fields (Completed, PlotGroup ...)
            // are the journal's bookkeeping, not shown (the owner, 2026-10-31).
            for (boolean completed : new boolean[] {false, true}) {
                for (int plot : plots) {
                    String prefix = "Journal.PlotList." + plot + ".";
                    if ("1".equals(values.get(prefix + "Completed")) != completed) {
                        continue;
                    }
                    String name = orEmpty(values.get(prefix + "Name"));
                    line((name.isEmpty() ? "Quest " + (plot + 1) : name)
                                    + (completed ? "  (completed)" : ""), 16,
                            completed ? FAINT : PARCHMENT)
                            .setTypeface(headingFace);
                    String description = orEmpty(values.get(prefix + "Description"));
                    if (!description.isEmpty()) {
                        line(description, 13, completed ? FAINT : TEXT);
                    }
                }
            }
            List<Integer> givers = indices(values, "Journal.PlotGivers.");
            if (!givers.isEmpty()) {
                heading("Quest givers nearby");
                for (int giver : givers) {
                    line(orEmpty(values.get("Journal.PlotGivers." + giver + ".PlaceableName")),
                            14, TEXT);
                }
            }
        }
    }

    /**
     * Map: the area's notes, the quest givers and the party by their world positions
     * (the same coordinates for all three), fitted to the screen - north up. The area's
     * own map image is 8.4's.
     */
    private final class MapTab extends Tab {
        private final MapView map;

        MapTab() {
            super("Map");
            map = new MapView(context);
            view = map;
        }

        @Override
        void update(Map<String, String> values, long[] frames) {
            map.set(values);
        }
    }

    private final class MapView extends View {
        private final Paint pin = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint giver = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint member = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint faint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint mapPaint = new Paint(Paint.FILTER_BITMAP_FLAG);
        private final List<float[]> points = new ArrayList<>();
        private final List<String> names = new ArrayList<>();
        private final List<Integer> kinds = new ArrayList<>();
        private String shownKey = "";

        MapView(Context context) {
            super(context);
            pin.setColor(PARCHMENT);
            giver.setColor(BRASS);
            member.setColor(HEALTH);
            label.setColor(TEXT);
            label.setTextSize(dp(12));
            label.setTypeface(bodyFace);
            faint.setColor(FAINT);
            faint.setTextSize(dp(13));
            faint.setTypeface(bodyFace);
            setBackground(frame(STONE, EDGE));
        }

        // The area's own map (DeckMaps), loaded off the UI thread when the area changes.
        private String mapTexture = "";
        private Bitmap map;
        private float mapLeft, mapRight, mapTop, mapBottom, mapWidth, mapHeight;
        /** Quarter turns the game's map screen gives the area's map (Area.North). */
        private int quarterTurns;

        void set(Map<String, String> values) {
            String texture = orEmpty(values.get("Area.Minimap"));
            mapLeft = (float) number(values.get("Area.MinimapLeft"));
            mapRight = (float) number(values.get("Area.MinimapRight"));
            mapTop = (float) number(values.get("Area.MinimapTop"));
            mapBottom = (float) number(values.get("Area.MinimapBottom"));
            mapWidth = (float) number(values.get("Area.MinimapWidth"));
            mapHeight = (float) number(values.get("Area.MinimapHeight"));
            quarterTurns = ((int) number(values.get("Area.North"))) & 3;
            if (!texture.equals(mapTexture)) {
                mapTexture = texture;
                map = null;
                final String wanted = texture;
                new Thread(() -> {
                    Bitmap loaded = DeckMaps.load(context, wanted);
                    post(() -> {
                        if (wanted.equals(mapTexture)) {
                            map = loaded;
                            invalidate();
                        }
                    });
                }, "deck-map").start();
            }
            String key = subtree(values, "MapNoteList.MapNotes.")
                    + subtree(values, "Journal.PlotGivers.") + positions(values) + subtree(values, "Area.");
            if (key.equals(shownKey)) {
                return;
            }
            shownKey = key;
            points.clear();
            names.clear();
            kinds.clear();
            for (int note : indices(values, "MapNoteList.MapNotes.")) {
                String prefix = "MapNoteList.MapNotes." + note + ".";
                String name = orEmpty(values.get(prefix + "MapNote"));
                add(values, prefix, name.isEmpty() ? orEmpty(values.get(prefix + "Name")) : name, 0);
            }
            for (int g : indices(values, "Journal.PlotGivers.")) {
                String prefix = "Journal.PlotGivers." + g + ".";
                add(values, prefix, orEmpty(values.get(prefix + "PlaceableName")), 1);
            }
            for (int m : indices(values, "Party.Members.")) {
                String prefix = "Party.Members." + m + ".";
                add(values, prefix, orEmpty(values.get(prefix + "Name")), 2);
            }
            invalidate();
        }

        private String positions(Map<String, String> values) {
            StringBuilder out = new StringBuilder();
            for (int m : indices(values, "Party.Members.")) {
                out.append(values.get("Party.Members." + m + ".WorldPositionX")).append(',')
                        .append(values.get("Party.Members." + m + ".WorldPositionY")).append(';');
            }
            return out.toString();
        }

        private void add(Map<String, String> values, String prefix, String name, int kind) {
            String x = values.get(prefix + "WorldPositionX");
            String y = values.get(prefix + "WorldPositionY");
            if (x == null || y == null) {
                return;
            }
            points.add(new float[] {(float) number(x), (float) number(y)});
            names.add(name);
            kinds.add(kind);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            if (points.isEmpty()) {
                canvas.drawText("The area's markers appear here as the game shows them.",
                        dp(12), dp(28), faint);
                return;
            }
            float minX = Float.MAX_VALUE, minY = Float.MAX_VALUE;
            float maxX = -Float.MAX_VALUE, maxY = -Float.MAX_VALUE;
            for (float[] p : points) {
                minX = Math.min(minX, p[0]);
                maxX = Math.max(maxX, p[0]);
                minY = Math.min(minY, p[1]);
                maxY = Math.max(maxY, p[1]);
            }
            float pad = dp(40);
            float legend = dp(30);
            boolean withMap = map != null && mapRight > mapLeft && mapTop > mapBottom
                    && mapWidth > 0 && mapHeight > 0;
            if (withMap) {
                // The area's own map: its edges are the world's MinimapLeft..Right and
                // MinimapTop..Bottom, the texture's used share MinimapWidth x Height.
                minX = mapLeft;
                maxX = mapRight;
                minY = mapBottom;
                maxY = mapTop;
                pad = dp(6);
            }
            float spanX = Math.max(maxX - minX, 20f);
            float spanY = Math.max(maxY - minY, 20f);
            // As the game's map screen shows the area: its map turned by Area.North's
            // quarter turns anticlockwise (Ostagar's King's Camp, 3: a quarter turn
            // clockwise - run 471 against the game's own map screen). The markers turn with
            // it; their names stay upright.
            final int turns = withMap ? quarterTurns : 0;
            final boolean sideways = (turns & 1) != 0;
            float scale = Math.min((getWidth() - 2 * pad) / (sideways ? spanY : spanX),
                    (getHeight() - legend - 2 * pad) / (sideways ? spanX : spanY));
            final float centreX = getWidth() / 2f;
            final float centreY = (getHeight() - legend) / 2f;
            final float midX = (minX + maxX) / 2;
            final float midY = (minY + maxY) / 2;
            final float drawScale = scale;
            final float degrees = -90f * turns;  // the canvas turns clockwise
            final double radians = Math.toRadians(degrees);
            final float cos = (float) Math.cos(radians);
            final float sin = (float) Math.sin(radians);
            // A world position on the screen: about the middle, scaled, y up, turned.
            java.util.function.BiFunction<Float, Float, float[]> onScreen = (wx, wy) -> {
                float dx = (wx - midX) * drawScale;
                float dy = -(wy - midY) * drawScale;
                return new float[] {centreX + dx * cos - dy * sin, centreY + dx * sin + dy * cos};
            };
            if (withMap) {
                android.graphics.Rect source = new android.graphics.Rect(0, 0,
                        Math.round(map.getWidth() * mapWidth), Math.round(map.getHeight() * mapHeight));
                android.graphics.RectF target = new android.graphics.RectF(
                        -spanX * scale / 2, -spanY * scale / 2, spanX * scale / 2, spanY * scale / 2);
                canvas.save();
                canvas.translate(centreX, centreY);
                canvas.rotate(degrees);
                canvas.drawBitmap(map, source, target, mapPaint);
                canvas.restore();
            }
            // The markers, then their names - right of the marker, else left, above or
            // below it, else left out where they would overlap one already written.
            List<android.graphics.RectF> written = new ArrayList<>();
            float lineHeight = label.getTextSize() * 1.25f;
            for (int i = 0; i < points.size(); i++) {
                float[] at = onScreen.apply(points.get(i)[0], points.get(i)[1]);
                float x = at[0];
                float y = at[1];
                int kind = kinds.get(i);
                float radius = dp(kind == 2 ? 7 : 5);
                canvas.drawCircle(x, y, radius, kind == 0 ? pin : kind == 1 ? giver : member);
            }
            for (int i = points.size() - 1; i >= 0; i--) {
                String name = names.get(i);
                if (name.isEmpty()) {
                    continue;
                }
                int kind = kinds.get(i);
                String text = kind == 1 ? "! " + name : name;
                float[] at = onScreen.apply(points.get(i)[0], points.get(i)[1]);
                float px = at[0];
                float py = at[1];
                float gap = dp(kind == 2 ? 11 : 9);
                float width = label.measureText(text);
                float[][] places = {
                    {px + gap, py + dp(4)}, {px - gap - width, py + dp(4)},
                    {px - width / 2, py - gap}, {px - width / 2, py + gap + lineHeight - dp(4)},
                };
                for (float[] place : places) {
                    float x = place[0];
                    float y = place[1];
                    android.graphics.RectF box = new android.graphics.RectF(x, y - lineHeight + dp(3),
                            x + width, y + dp(3));
                    boolean clear = true;
                    for (android.graphics.RectF other : written) {
                        if (android.graphics.RectF.intersects(box, other)) {
                            clear = false;
                            break;
                        }
                    }
                    if (clear) {
                        written.add(box);
                        canvas.drawText(text, x, y, label);
                        break;
                    }
                }
            }
            canvas.drawText("● the party   ● quest givers   ● the area's notes", dp(10),
                    getHeight() - dp(10), faint);
        }
    }

    /** Tactics, read-only: each member's slots - condition, command and target by number. */
    private final class TacticsTab extends ListTab {
        TacticsTab() {
            super("Tactics");
        }

        @Override
        String key(Map<String, String> values) {
            StringBuilder key = new StringBuilder();
            for (int m : indices(values, "Party.Members.")) {
                key.append(subtree(values, "Party.Members." + m + ".Tactics."));
                key.append(values.get("Party.Members." + m + ".Name"));
            }
            return key.toString();
        }

        @Override
        void build(Map<String, String> values) {
            boolean any = false;
            for (int m : indices(values, "Party.Members.")) {
                String prefix = "Party.Members." + m + ".Tactics.";
                List<Integer> slots = indices(values, prefix + "TacticsList.");
                if (slots.isEmpty()) {
                    continue;
                }
                any = true;
                heading(orEmpty(values.get("Party.Members." + m + ".Name"))
                        + ("0".equals(values.get(prefix + "Enabled")) ? "  (tactics off)" : ""));
                for (int slot : slots) {
                    String s = prefix + "TacticsList." + slot + ".";
                    String item = orEmpty(values.get(s + "ItemName"));
                    // The names as the tactics screen shows them (the deck's reads
                    // resolve them); the game's numbers where a name is not read.
                    if ("0".equals(values.get(s + "Condition"))
                            && "0".equals(values.get(s + "Command"))
                            && "0".equals(values.get(s + "Target"))) {
                        line((slot + 1) + ". (empty)", 13, FAINT);
                        continue;
                    }
                    String target = named(values, s + "Target");
                    String condition = named(values, s + "Condition").replace("%s", "…");
                    String command = named(values, s + "Command");
                    String ability = orEmpty(values.get(s + "CommandParamName"));
                    if (!ability.isEmpty() && item.isEmpty()) {
                        command += ": " + ability;
                    }
                    line(String.format(Locale.ROOT, "%d. %s%s: %s → %s%s", slot + 1,
                            "0".equals(values.get(s + "Enabled")) ? "(off) " : "", target,
                            condition, command, item.isEmpty() ? "" : " - " + item), 13, TEXT);
                }
            }
            if (!any) {
                note("Each companion's tactics appear here once the game has read them.");
            }
        }
    }

    /**
     * Codex and journal: the codex's groups with what is unlocked (a tap opens an entry),
     * and the journal's conversations.
     */
    private final class CodexTab extends ListTab {
        private String open = "";

        CodexTab() {
            super("Codex");
        }

        @Override
        String key(Map<String, String> values) {
            return subtree(values, "Journal.CodexGroups.") + subtree(values, "Journal.Conversations.")
                    + open;
        }

        @Override
        void build(Map<String, String> values) {
            boolean any = false;
            for (int g : indices(values, "Journal.CodexGroups.")) {
                String group = "Journal.CodexGroups." + g + ".";
                List<Integer> unlocked = new ArrayList<>();
                for (int e : indices(values, group + "CodexList.")) {
                    if ("0".equals(values.get(group + "CodexList." + e + ".Locked"))) {
                        unlocked.add(e);
                    }
                }
                if (unlocked.isEmpty()) {
                    continue;
                }
                any = true;
                heading(orEmpty(values.get(group + "Name")) + "  (" + unlocked.size() + ")");
                for (int e : unlocked) {
                    String entry = group + "CodexList." + e + ".";
                    final String id = entry;
                    TextView name = line(orEmpty(values.get(entry + "Name"))
                            + ("1".equals(values.get(entry + "Updated")) ? "  •" : ""), 15,
                            PARCHMENT);
                    name.setOnClickListener(v -> {
                        open = open.equals(id) ? "" : id;
                        update(lastValues, lastFrames);
                    });
                    if (open.equals(entry)) {
                        String text = orEmpty(values.get(entry + "Descriptions"));
                        TextView body = line(text.isEmpty() ? "(the entry's text is not read yet)"
                                : text, 13, INK_ON_PARCHMENT);
                        body.setBackgroundColor(PARCHMENT);
                        body.setPadding(dp(10), dp(6), dp(10), dp(6));
                    }
                }
            }
            List<Integer> conversations = indices(values, "Journal.Conversations.");
            if (!conversations.isEmpty()) {
                heading("Conversations");
                for (int c : conversations) {
                    String prefix = "Journal.Conversations." + c + ".ConversationLines.";
                    for (int l : indices(values, prefix)) {
                        // The line's and the speaker's text from the talk table (the
                        // deck's reads resolve them); the player's lines are "You".
                        String p = prefix + l + ".";
                        String said = orEmpty(values.get(p + "LineText"));
                        if (said.isEmpty()) {
                            continue;
                        }
                        String who = "1".equals(values.get(p + "Player")) ? "You"
                                : orEmpty(values.get(p + "SpeakerText"));
                        line((who.isEmpty() ? "" : who + ": ") + said, 13, TEXT);
                    }
                }
            }
            if (!any && conversations.isEmpty()) {
                note("The codex's entries appear here as they are found.");
            }
        }
    }

    /** Items, read-only: what each member wears, the backpack, the purse. */
    private final class ItemsTab extends ListTab {
        ItemsTab() {
            super("Items");
        }

        @Override
        String key(Map<String, String> values) {
            return subtree(values, "Party.Backpack.") + subtree(values, "Party.Money.")
                    + equipment(values);
        }

        private String equipment(Map<String, String> values) {
            StringBuilder key = new StringBuilder();
            for (int m : indices(values, "Party.Members.")) {
                key.append(subtree(values, "Party.Members." + m + ".Equipment."));
            }
            return key.toString();
        }

        @Override
        void build(Map<String, String> values) {
            List<String> money = new ArrayList<>();
            for (Map.Entry<String, String> coin : sorted(values, "Party.Money.")) {
                money.add(coin.getValue());
            }
            if (!money.isEmpty()) {
                line("Purse: " + TextUtils.join(" · ", money), 14, BRASS);
            }
            for (int m : indices(values, "Party.Members.")) {
                String prefix = "Party.Members." + m + ".Equipment.";
                List<Integer> slots = indices(values, prefix);
                if (slots.isEmpty()) {
                    continue;
                }
                heading(orEmpty(values.get("Party.Members." + m + ".Name")));
                for (int slot : slots) {
                    String name = orEmpty(values.get(prefix + slot + ".Name"));
                    if (!name.isEmpty()) {
                        itemLine(values.get(prefix + slot + ".Icon"),
                                name + stack(values.get(prefix + slot + ".StackSize")));
                    }
                }
            }
            List<Integer> items = indices(values, "Party.Backpack.");
            heading("Backpack" + (items.isEmpty() ? "" : "  (" + items.size() + ")"));
            if (items.isEmpty()) {
                line("Empty, or not read yet.", 13, FAINT);
            }
            for (int item : items) {
                String prefix = "Party.Backpack." + item + ".";
                itemLine(values.get(prefix + "Icon"),
                        orEmpty(values.get(prefix + "Name")) + stack(values.get(prefix + "StackSize")));
            }
        }

        private String stack(String size) {
            return size != null && !size.equals("1") && !size.equals("0") && !size.isEmpty()
                    ? "  ×" + size : "";
        }
    }

    // ---- reading the snapshot ----

    /** The numbers right after `prefix` among the keys ("Party.Members." -> 0, 1, 2, 3). */
    private static List<Integer> indices(Map<String, String> values, String prefix) {
        java.util.TreeSet<Integer> found = new java.util.TreeSet<>();
        for (String key : values.keySet()) {
            if (!key.startsWith(prefix)) {
                continue;
            }
            int end = prefix.length();
            while (end < key.length() && Character.isDigit(key.charAt(end))) {
                end++;
            }
            if (end > prefix.length() && (end == key.length() || key.charAt(end) == '.')) {
                found.add(Integer.parseInt(key.substring(prefix.length(), end)));
            }
        }
        return new ArrayList<>(found);
    }

    /** The keys under `prefix` with their values, in key order. */
    private static List<Map.Entry<String, String>> sorted(Map<String, String> values,
                                                          String prefix) {
        List<Map.Entry<String, String>> out = new ArrayList<>();
        java.util.NavigableMap<String, String> map = values instanceof java.util.NavigableMap
                ? (java.util.NavigableMap<String, String>) values : new java.util.TreeMap<>(values);
        for (Map.Entry<String, String> entry : map.tailMap(prefix, true).entrySet()) {
            if (!entry.getKey().startsWith(prefix)) {
                break;
            }
            out.add(entry);
        }
        return out;
    }

    /** Everything under `prefix`, as one string: a tab rebuilds only when it changes. */
    private static String subtree(Map<String, String> values, String prefix) {
        StringBuilder out = new StringBuilder();
        for (Map.Entry<String, String> entry : sorted(values, prefix)) {
            out.append(entry.getKey()).append('=').append(entry.getValue()).append(';');
        }
        return out.toString();
    }

    /** A tactic's field by its name where the deck's reads found one, else its number. */
    private static String named(Map<String, String> values, String path) {
        String name = values.get(path + "Name");
        return name != null && !name.isEmpty() ? name : "#" + orEmpty(values.get(path));
    }

    private static String orEmpty(String text) {
        return text == null ? "" : text;
    }

    private final class PlaceholderTab extends Tab {
        PlaceholderTab(String label, String note) {
            super(label);
            TextView text = text(15, FAINT);
            text.setText(note);
            text.setGravity(Gravity.CENTER);
            view = text;
        }

        @Override
        void update(Map<String, String> values, long[] frames) {
        }
    }

    /** Every path the mirror holds and its value (files/deck-trace): for finding a tab's data. */
    private final class DataTab extends Tab {
        private final TextView body;

        DataTab() {
            super("Data");
            ScrollView scroll = new ScrollView(context);
            body = text(11, TEXT);
            body.setTypeface(Typeface.MONOSPACE);
            scroll.addView(body);
            view = scroll;
        }

        @Override
        void update(Map<String, String> values, long[] frames) {
            List<String> paths = new ArrayList<>(values.keySet());
            Collections.sort(paths);
            StringBuilder text = new StringBuilder();
            for (String path : paths) {
                text.append(path).append(" = ").append(values.get(path)).append('\n');
            }
            body.setText(text);
        }
    }

    /**
     * The performance overlay (8.6): the game's frames a second and the screen's, the
     * game's frame times as a graph, the slowest, and the handheld's battery and GPU as
     * far as an app may read them.
     */
    /**
     * The performance numbers (PerfStats, the same as the main screen's overlay): the
     * game's frames a second, the frame time, the 1% low and the power drawn as large
     * figures, the screen's rate, the frame times as bars, the battery and the GPU.
     */
    private final class PerformanceTab extends Tab {
        private final PerfStats stats = new PerfStats();
        private final TextView fps;
        private final TextView total;
        private final TextView totalLabel;
        private final TextView frameTime;
        private final TextView low;
        private final TextView power;
        private final TextView pipeline;
        private final TextView sizes;
        private final TextView screen;
        private final TextView detail;
        private final FrameGraph graph;

        PerformanceTab() {
            super("Perf");
            LinearLayout column = new LinearLayout(context);
            column.setOrientation(VERTICAL);
            LinearLayout numbers = new LinearLayout(context);
            numbers.setOrientation(HORIZONTAL);
            fps = figure(BRASS);
            total = figure(BRASS);
            frameTime = figure(PARCHMENT);
            low = figure(PARCHMENT);
            power = figure(PARCHMENT);
            totalLabel = text(12, FAINT);
            numbers.addView(labelled(fps, text(12, FAINT), "native fps"), cell());
            numbers.addView(labelled(total, totalLabel, "total fps"), cell());
            numbers.addView(labelled(frameTime, text(12, FAINT), "frame time"), cell());
            numbers.addView(labelled(low, text(12, FAINT), "1% low fps"), cell());
            numbers.addView(labelled(power, text(12, FAINT), "power drawn"), cell());
            column.addView(numbers);
            pipeline = text(14, PARCHMENT);
            pipeline.setPadding(0, dp(8), 0, 0);
            column.addView(pipeline);
            sizes = text(14, PARCHMENT);
            column.addView(sizes);
            screen = text(13, TEXT);
            screen.setPadding(0, dp(6), 0, 0);
            column.addView(screen);
            graph = new FrameGraph(context);
            LayoutParams graphParams = new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
            graphParams.topMargin = dp(8);
            graphParams.bottomMargin = dp(8);
            column.addView(graph, graphParams);
            detail = text(13, TEXT);
            column.addView(detail);
            view = column;
        }

        private TextView figure(int colour) {
            TextView number = text(24, colour);
            number.setTypeface(headingFace);
            number.setSingleLine(true);
            return number;
        }

        private LayoutParams cell() {
            return new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        }

        private View labelled(TextView number, TextView label, String what) {
            LinearLayout box = new LinearLayout(context);
            box.setOrientation(VERTICAL);
            box.addView(number);
            label.setText(what);
            box.addView(label);
            return box;
        }

        @Override
        void update(Map<String, String> values, long[] frames) {
            stats.update(context, frames);
            stats.render(values);
            fps.setText(PerfStats.rate(stats.gameFps));
            // The screen's rate: the game's frames and the generated ones between them.
            total.setText(PerfStats.rate(stats.screenFps));
            totalLabel.setText(stats.generating() ? "total, frame gen" : "total fps");
            frameTime.setText(stats.frameTime());
            low.setText(stats.low());
            power.setText(stats.power());
            power.setTextSize(TypedValue.COMPLEX_UNIT_SP, stats.charging ? 14 : 24);
            pipeline.setText(stats.pipeline());
            sizes.setText(stats.sizes());
            screen.setText(String.format(Locale.ROOT,
                    "The slowest of the game's last %d frames %s.", stats.intervals.length,
                    stats.slowestMs == PerfStats.NONE ? "–"
                            : String.format(Locale.ROOT, "%.1f ms", stats.slowestMs)));
            graph.set(stats.intervals);
            detail.setText(stats.device());
        }
    }

    /** The game's last frame times as bars, the 30 and 60 fps marks drawn across. */
    private final class FrameGraph extends View {
        private final Paint bar = new Paint();
        private final Paint slow = new Paint();
        private final Paint mark = new Paint();
        private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
        private long[] intervals = new long[0];

        FrameGraph(Context context) {
            super(context);
            bar.setColor(BRASS_DIM);
            slow.setColor(BLOOD);
            mark.setColor(FAINT);
            mark.setStrokeWidth(dp(1));
            label.setColor(FAINT);
            label.setTextSize(dp(10));
            label.setTypeface(bodyFace);
            setBackground(frame(STONE, EDGE));
        }

        void set(long[] intervals) {
            this.intervals = intervals;
            invalidate();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float width = getWidth();
            float height = getHeight();
            // Up the graph to half again the slowest frame shown, 50 ms at least and 150
            // at most; a frame half again the median's is marked slow.
            long slowest = 0;
            long[] sorted = intervals.clone();
            java.util.Arrays.sort(sorted);
            for (long interval : intervals) {
                slowest = Math.max(slowest, interval);
            }
            long median = sorted.length == 0 ? 0 : sorted[sorted.length / 2];
            float top = Math.max(50e6f, Math.min(150e6f, slowest * 1.5f));
            float scale = height / top;
            if (intervals.length > 0) {
                // The newest 200 frames, as bars.
                int shown = Math.min(intervals.length, 200);
                float step = width / 200f;
                float x = width - shown * step;
                for (int i = intervals.length - shown; i < intervals.length; i++) {
                    long interval = intervals[i];
                    float h = Math.min(height, interval * scale);
                    canvas.drawRect(x + 1, height - h, x + step - 1, height,
                            median > 0 && interval > median * 3 / 2 ? slow : bar);
                    x += step;
                }
            }
            for (float ms : new float[] {16.67f, 33.33f, 66.67f}) {
                float y = height - ms * 1e6f * scale;
                if (y < dp(12)) {
                    continue;
                }
                canvas.drawLine(0, y, width, y, mark);
                canvas.drawText(Math.round(1000 / ms) + " fps", dp(4), y - dp(3), label);
            }
        }
    }

    // ---- helpers ----

    private static String readLine(String path) {
        try (BufferedReader reader = new BufferedReader(new FileReader(path))) {
            return reader.readLine();
        } catch (Exception e) {
            return null;
        }
    }

    private GradientDrawable frame(int fill, int edge) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(fill);
        drawable.setStroke(dp(1), edge);
        drawable.setCornerRadius(dp(2));
        return drawable;
    }

    private View rule(int colour) {
        View view = new View(context);
        view.setBackgroundColor(colour);
        return view;
    }

    private TextView text(float sp, int colour) {
        TextView view = new TextView(context);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setTextColor(colour);
        view.setTypeface(bodyFace);
        view.setEllipsize(TextUtils.TruncateAt.END);
        return view;
    }

    private int dp(float value) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
                context.getResources().getDisplayMetrics()));
    }
}
