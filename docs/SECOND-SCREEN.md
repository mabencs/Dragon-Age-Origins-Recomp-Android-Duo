# Second-screen companion deck

A Dragon Age-styled companion display on the AYN Thor's lower screen.

**Decision D5:** six tabs — Party, Quests, Map, Tactics, Codex/Journal,
Inventory/Equipment.
**Decision D6:** read-only mirror first. No writes into game state in Phase 8.

**As built (0.1 Open Alpha, 2026-11-02):** eight tabs - Party, Quickbar, Quests, Map,
Tactics, Codex, Items, and Perf with the *Performance tab/overlay* option on. The deck
runs in its own process (8.7) and is read-only but for the Quickbar's taps (8.8) and the
Items tab's Equip, Use and Unequip buttons (8.12); the Quests and Codex tabs fold as the journal
does, and their long texts are read only while open (8.11); the Party tab shows each member's own face, read back from the GPU (8.9). Not built: the
map's pan, pinch and world-map toggle. This document is the design it was built from;
where they differ, `docs/PLAN.md` Phase 8 has what was built and why, and
[`FEATURES.md`](FEATURES.md) the state of each tab.

---

## 1. Target surface

Measured from the device, not assumed. See [`../device/thor-profile.md`](../device/thor-profile.md).

| | |
|---|---|
| Display ID | **4** (`Screen-2`, unique ID `local:4630946482288158084`) |
| Resolution in landscape | **1240 × 1080** |
| **Logical size** | **≈ 538 × 468 dp** at 369 dpi |
| Refresh | 60 / 120 Hz |
| Touch | **Yes** (`touch EXTERNAL`) |
| Colour | Display-P3 (no plain sRGB mode offered) |
| Display group | 0 — same group as the main screen |
| Flags | includes `FLAG_PRESENTATION` |

Two things about that geometry drive the whole layout:

- **It is nearly square** (1.148:1). Layouts designed for a wide strip will look wrong.
- **538 × 468 dp is compact** — comparable to a large phone, not a tablet. Touch targets
  need to be at least 48 dp and text needs to stay generous. Do not try to fit a desktop
  journal onto it.

Build the layout responsively rather than pinning to 1240 × 1080, and verify orientation
on-device early: the base and override display info report different rotations, so the
effective orientation needs confirming empirically (open question Q6).

---

## 2. Getting a window onto display 4

On **API 33**, `Presentation` is restricted on displays reported as `type=INTERNAL`,
and display 4 is internal despite also carrying `FLAG_PRESENTATION`. So there are two
candidate routes and we test both (task 8.1):

**Route A — second Activity (expected to be the one that works):**

```java
DisplayManager dm = getSystemService(DisplayManager.class);
Display deck = null;
for (Display d : dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
    if (d.getDisplayId() != Display.DEFAULT_DISPLAY && d.getState() == Display.STATE_ON) {
        deck = d;
        break;
    }
}
if (deck != null) {
    ActivityOptions opts = ActivityOptions.makeBasic();
    opts.setLaunchDisplayId(deck.getDisplayId());
    startActivity(new Intent(this, DeckActivity.class), opts.toBundle());
}
```

Both displays being in group 0 is what makes `setLaunchDisplayId` viable here.

**Route B — `Presentation`:** simpler if it works, since it binds directly to a
`Display`. Worth a quick test given `FLAG_PRESENTATION` is set.

**Discover the display by capability, never by model name.** Enumerate displays and pick
a non-default, powered-on, presentation-capable one. That way the deck also works on
other dual-screen handhelds, and it degrades gracefully to "no deck" on single-screen
devices rather than crashing.

---

## 3. Where the data comes from

**As built (session 37):** the movies are not pushed data - they *observe* the engine's
data model by token path (`Party.Members.N.Attributes.7.Current`, `GUI.SelectedChar.Name`,
`Journal.PlotGivers` ...) through the TokenManager class every movie shares, and the
engine sends each changed value as text once a frame. `warden_deck.cc` hooks the two ends
(an observer's registration names it; the push carries its value), read-only, and keeps
path -> latest text. What the HUD's movies observe (the party, the selected character)
arrives that way; what the journal, inventory and tactics screens only *pull* while open,
the mirror reads itself through the same engine getters their handlers use - read-only,
on the game's thread, within half a millisecond in each 16 ms, while the deck pulls (the
deck's own reads, PLAN 8.5). The plan below stands.

This is the most important design decision in the phase, and it is what makes D6
(read-only) cheap rather than restrictive.

**Mirror the feed the game already produces. Do not reverse-engineer memory layouts.**

The game *already* computes everything the deck needs, because it already draws its own
HUD, journal and map:

| Deck tab | Existing in-game source |
|---|---|
| Party | HUD party portraits — already has health and mana/stamina per member |
| Quests | `journal.gfx` — already has quest list and objective state |
| Map | `fademap.gfx` + worldmap atlases — already has markers and player position |
| Codex | Journal codex entries, already resolved against the talktables |
| Tactics | Tactics screen data, already assembled |
| Inventory | Inventory screen data, already assembled |

Since the UI is Scaleform, the game pushes this data across an ActionScript boundary to
populate those screens. Tapping that boundary gives us structured, already-validated
data, and it has three decisive advantages over reading structs out of guest memory:

1. **It survives.** Struct offsets are brittle; a semantic data feed is stable.
2. **It is read-only by construction.** Observing a feed cannot corrupt a save.
3. **It is already correct.** No risk of misinterpreting a field the game interprets
   differently.

Data flows one way in Phase 8:

```
  guest game state
        │  (Scaleform data feed the game already emits)
        ▼
  state mirror  ──► snapshot, double-buffered
        │  JNI, pull-based, never blocking
        ▼
  DeckActivity ──► renders the current snapshot
```

### Keeping the deck off the critical path

Non-negotiable (task 8.7). The deck must never be able to stall or crash the render
loop:

- The mirror writes to a **double-buffered snapshot**; the deck reads the stable buffer.
- The deck **pulls** on its own cadence (10–15 Hz is ample for HP bars and quest text).
  It does not get called from the game thread.
- No lock the game thread can block on. If the deck is slow or dead, the game must not
  notice.
- Deck updates run at a lower rate than the game — there is no reason to update a quest
  list at 60 Hz, and doing so wastes power on a handheld.
- Deck failure is **non-fatal**: an exception in the deck closes the deck, not the game.

---

## 4. Visual design

The deck has to look like it belongs to Dragon Age: Origins, not like an Android app
sitting underneath it.

### Source the real assets

The strongest move available: the game's own UI art and typefaces are right there in
`guiexport.erf`, and we already have a working extractor.

- **Typeface** — `fonts_en-us.gfx` and `gfxfontlib.gfx` contain the game's actual fonts.
  Extracting and reusing them beats approximating with a lookalike serif. **Done
  (session 37, `DeckFonts`):** "Nogard" and "Dragon Text", DefineFont3 outlines made into
  TrueType on the device, in the deck's cache.
- **Frames and ornament** — the atlas groups include the game's panel frames, dividers,
  scrollbars and heraldic motifs. **Done (session 37, `DeckArt`):** the HUD's portrait ring
  and arcs, the main menu's bar, the ornate divider and the journal's bar, from
  `atl_shared_dxt5_00.xds`, decoded on the device. `atl_shared_dxt5` (2048×2048) is the common set used
  across screens and is the natural place to start.
- **Icons** — ability, spell and item icons already exist as atlas cells, so the deck
  can show the same icons the game shows. **The items' (session 37):** 890 `ico_*` cells
  of `atl_shared_dxt1_00.xds`, shown on the Items tab.

Reusing the game's own assets is both the most authentic route and less work than
authoring imitations. It is personal-use only and nothing is redistributed.

### Palette

Dragon Age: Origins' UI language is dark stone and aged brass, with parchment for
anything text-heavy, and blood red for emphasis.

| Role | Colour | Use |
|---|---|---|
| Deep background | `#0E0B09` | Behind everything |
| Panel stone | `#1A1613` | Panel fills |
| Panel edge | `#2E2822` | Borders, dividers |
| Brass / gold leaf | `#C9A227` | Frames, active tab, headings |
| Brass dim | `#8C6D1F` | Inactive ornament |
| Parchment | `#D9C9A3` | Journal, codex, quest text backgrounds |
| Parchment aged | `#C9B68C` | Parchment edges, shadowing |
| Ink on parchment | `#2B2318` | Body text on parchment |
| Text on stone | `#D8CDB5` | Body text on dark panels |
| Blood red | `#7A1518` | Health, warnings, danger |
| Steel | `#4A4845` | Disabled, inactive |

Bar colours follow the game's own convention:

| Bar | Colour |
|---|---|
| Health | `#A3201F` red |
| Mana | `#2E5C8A` blue |
| Stamina | `#B8912E` amber |

### Texture and treatment

- Subtle parchment grain and stone noise — DAO's UI is never flat colour.
- Panels sit in ornate frames with a slight emboss; corners carry brass filigree.
- Soft inner shadow at panel edges to suggest depth.
- Dividers are thin brass rules, often with a small central motif.
- **No** modern flat-design conventions: no material elevation shadows, no bright
  accent-colour fills, no sans-serif UI text, no rounded-rectangle chips.
- Animation is slow and weighty. Tab changes cross-fade; they do not slide or bounce.

Display 4 runs in **Display-P3**, so author and tag colours for P3 rather than assuming
sRGB, or the warm brass tones will shift.

---

## 5. Layout

Within ≈538 × 468 dp:

```
┌──────────────────────────────────────────────────────────┐
│  ╔════╗ ╔════╗ ╔════╗ ╔════╗ ╔════╗ ╔════╗               │  tab rail
│  ║PRTY║ ║QUST║ ║ MAP ║ ║TACT║ ║CODX║ ║ INV ║             │  ~64 dp
│  ╚════╝ ╚════╝ ╚════╝ ╚════╝ ╚════╝ ╚════╝               │
├──────────────────────────────────────────────────────────┤
│                                                          │
│                                                          │
│                    active tab content                    │  ~388 dp
│                                                          │
│                                                          │
├──────────────────────────────────────────────────────────┤
│  gold · time · area name                      ⚙          │  ~16 dp
└──────────────────────────────────────────────────────────┘
```

Tab switching by **touch** (the screen has it) and optionally by a shoulder-button
chord, so the deck can be driven without lifting a thumb off the stick.

### Per-tab sketches

**Party** — the default tab, and the one that earns the second screen. Four portrait
cards, each with the character's actual portrait, name, level, health bar, mana/stamina
bar, and active status effects. Vertical stack of four fits 468 dp comfortably at ~90 dp
per card.

**Quests** — active quest list on parchment, selected quest expanding to show its
objectives with completion ticks. Current area's quests first.

**Map** — the area map with markers and the player's position and facing. Pan and pinch
by touch. A toggle between area map and world map.

**Tactics** — read-only in Phase 8: each party member's tactic slots shown as
condition → action rows. The drag-and-drop editor is deliberately deferred; it is a
write path.

**Codex & journal** — parchment-styled browsable list of unlocked codex entries by
category, with full text. The most natural fit for the aesthetic.

**Inventory & equipment** — read-only in Phase 8: equipped slots per character plus the
party inventory list with item icons and stats.

**Performance overlay** — a developer tab (task 8.6), toggled in settings. Framerate,
frametime graph, 1% lows, CPU/GPU load, temperature, battery. This is also the main
diagnostic instrument during Phases 5–6, so it is worth building early even though it
is listed late.

**As built (8.9, 8.10, session 40):** the Party tab's rings hold each member's face as
the game last drew it. The HUD's portraits are GPU renders, one 146x146 resolve per
member at an area load (destination pitch `009200A0`); while the deck pulls, the Vulkan
backend copies each one out and `warden_deck.cc` untiles it, the head's alpha kept, and
gives it to its party slot (a burst in slot order, a lone redraw to the slot its address
had). The deck asks for a face only when its generation changes (DeckService's
`PORTRAITS` and `PORTRAIT`). The **Performance tab/overlay** option (off by default)
decides where the numbers go: off, the Perf tab is hidden; on with the second screen
showing, the Perf tab; on without it, a box at the top centre of the main screen. Both
show the game's native frames a second, the total on the screen with generated frames,
the frame time, the 1% low, the power drawn, the frame generation and the upscaler as
they run (not as the options say for the next start), and the rendered and shown sizes
(`NativeBridge.deckRender`).

---

## 6. Build order

1. 8.1 — resolve Route A vs Route B on the real device
2. 8.2 — `DeckActivity` on display 4 with the tab rail and placeholder tabs
3. 8.3 — state mirror, starting with party health only, end to end
4. 8.4 — extract the game's fonts and frame art; establish the visual system
5. 8.6 — performance overlay (early, because it is a tool)
6. 8.5 — tabs in order: Party → Quests → Map → Codex → Tactics → Inventory
7. 8.7 — verify isolation: kill the deck mid-combat and confirm the game is unaffected
8. **Later and separately** — 8.8, the touch quickbar, the one sanctioned write path.
   **Built in session 40:** a Quickbar tab - the selected character's six quick slots
   (`GUI.SelectedChar.Stats.QuickSlots.N`) with the game's icons; a tap is the slot's own
   button press, merged into the pad the game polls (X, Y, then RT with X, Y, B; the
   right button is the healing potion, as on the HUD), only while the engine explores or
   fights. Nothing else is written: the engine sees a button, as from the controller.

---

## 7. Open questions

| # | Question | Status |
|---|---|---|
| Q6 | `Presentation` or second Activity on API 33? | **Both work** (runs 454-457); the second Activity in its own process is the default, for 8.7. Launching it makes display 4 the focused one (BUG-078): the deck gives the focus back once connected |
| — | Effective orientation of display 4 in handheld use | **Landscape**, 1240x1025 for the deck's window (run 454) |
| — | Can the Scaleform data feed be tapped cleanly, or is a struct reader needed after all? | **Tapped cleanly** (8.3, `warden_deck.cc`): the movies observe the engine's data model by token path; two read-only hooks see every registration and every changed value |
| — | Does the deck's 120 Hz panel need its own frame pacing, or is 15 Hz pull enough? | Measure in 8.2 |
| — | Power cost of driving a second display continuously | Measure in Phase 10.6; consider dimming or blanking the deck when idle |
