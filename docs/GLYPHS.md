# Controller glyph remapping

**Decision D3: relabel only.** Button *functions* never change. Only the letter or
symbol drawn on screen changes. **Decision D4: Xbox is the shipped default**, with
Nintendo and PlayStation selectable in settings.

---

## 1. The requirement, precisely

Dragon Age: Origins on Xbox 360 draws Xbox glyphs because that is the only controller
it ever expected. On a handheld, or with a third-party pad, the prompt can disagree with
the hardware in your hands.

The important insight is that all three controller families use the **same four physical
positions**. Only the labels differ. So this is a pure presentation problem — we never
have to touch input routing.

| Physical position | Xbox (default) | Nintendo | PlayStation |
|---|---|---|---|
| **Bottom** | **A** | **B** | ✕ cross |
| **Right** | **B** | **A** | ○ circle |
| **Top** | **Y** | **X** | △ triangle |
| **Left** | **X** | **Y** | □ square |

Which is exactly what was asked for: Xbox shows `A` down, `B` right, `Y` up, `X` left;
Nintendo shows `B` down, `A` right, `X` up, `Y` left.

For Nintendo this reduces to a neat pair of swaps — **A↔B** and **X↔Y**. PlayStation is
a full symbol substitution - and the game ships that art itself, from its PS3 release
(section 2).

In Nintendo mode the shoulders, triggers, sticks, d-pad, Start and Back keep their Xbox
labels - only the face buttons trade places. PlayStation relabels them all (L1, R2, L3,
Select...), with the PS3 set's own art.

### What relabel-only explicitly does *not* do

It does not swap confirm and cancel. In Nintendo mode, the button that confirms is still
the **bottom** button — it just draws as `B`. This is deliberate: it keeps muscle memory
intact and removes any chance of a prompt disagreeing with what the game actually does.
A separate confirm/cancel swap was offered and declined.

---

## 2. What the game data holds (surveyed 2026-10-27, PLAN 7.1-7.2)

The first look (2026-10-22) expected three paths - bitmaps, text and a controller table.
The survey found one: **every prompt is an image named by the button's position**, and
the game ships the PlayStation art itself.

### Every prompt is a named image, named by position

The UI is Scaleform: 73 movies (`.gfx`) in `guiexport.erf`, whose bitmaps are external -
atlas cells, mostly named `<movie>_I<id>.dds` - beside one set named by gamepad
position: `XBoxGamepad_BottomAction`, `_RightAction`, `_LeftAction`, `_TopAction`,
`_LeftBumper`, `_RightBumper`, `_LeftTrigger`, `_RightTrigger`, `_LeftStick`,
`_RightStick`, `_LeftStickPush`, `_RightStickPush`, `_Select`, `_Start`,
`_SystemButton`, `_DPad` and its four directions, and the left stick with arrows. The
movies export them as image symbols, and their ActionScript picks prompt frames by
position: `ButtonIdToString` returns `BottomAction`, `LeftShoulder` and so on - names,
never letters.

**Text too.** The talk table's console strings carry tokens - `<A/>`, `<B/>`, `<X/>`,
`<Y/>`, `<LB/>`, `<RB/>`, `<LT/>`, `<RT/>`, `<Back/>`, `<theleftstick/>` and a few more:
"Press <LB/> or <RB/> to change which party member you are controlling", "<A/>
Continue" - and the engine expands each to an inline image of the same set, `<img
src='img://XBoxGamepad_BottomAction.dds' align='baseline' vspace='-10'/>` (UTF-16
strings in the executable). So the worry of 2026-10-22, text prompts still saying "A"
after the bitmaps change, does not arise: there is no text path apart from the images.

### The PlayStation set ships in the game

The game was also released on the PS3, and the 360's shared atlas
(`atl_shared_dxt5_00`, 2048x2048 DXT5) carries that build's whole set beside the Xbox
one, named the same way: `PS3Gamepad_BottomAction` (cross), `_RightAction` (circle),
`_LeftAction` (square), `_TopAction` (triangle), L1, R1, L2, R2, L, R, L3, R3, Select,
Start, the PS button and the D-pad - in the same gold-rimmed coin art as the Xbox coins.
**No new art is needed for PlayStation; Nintendo is the Xbox letters on other
positions.**

### Where each image is: the atlas descriptions

Each atlas page has a description, `atl_<movie, 10 characters>_<format>_dat.xml`, giving
every image name its page and rectangle:

```xml
<SourceTexture Name="XBoxGamepad_BottomAction.dds" OffsetAndScale="0.837891 0.751465 0.017090 0.017578" ... AtlasSize="2048 2048" OriginalImageSize="35 36"/>
```

The engine parses them with a generic XML reader and finds an image by name in its
movie's atlases and the shared ones.

### Some screens keep their own copies

Found by eye over every small UI cell (436 of them, `tools/dao_xds.py`): the dialogue
pop-up's A, B, X and Y (`DialoguePop_I3A`, `_I41`, `_I4B`, `_I46`), character creation's
A and left stick (`CharGen_I198`, `_I164` - with its own PS3 L, `_I166`), the tutorials'
large A (`Tutorial_I11D`), the abilities screen's right stick (`abilities_IE4` - with its
PS3 R, `_IE6`), the new-content screen's sticks (`NewContentAvailable_I2`, `_I4`), the
portraits' RB (`Portraits_IF`), and the login screens' D-pad and stick presses (online,
never shown).

**Baked into a picture:** the controller-layout loading screen (`load_controller.gfx`,
`atl_load_contr_dxt5_00`) is one image of an Xbox 360 pad with its coloured A, B, X and
Y - not cells to redirect. It stays as it is (7.8).

### The title update's patch folder

`packages/core/patch/` holds files that replace ones in the archives by name - the title
update's 25 (tables, talk tables, scripts, a texture). The title lists the folder once,
at its start, and from then on opens the files it found there by name; it reads the
atlas descriptions again at every load - the main menu, a save's load, an area's (runs
346-347).

### Not used

- **The UI's platform switch.** The ActionScript asks the engine
  `ExternalCommands.GetValue("Scaleform.Platform")` - "XBOX", "PC" or "PS3" - and picks
  some prompt clips by it. But it also gates other behaviour (the main menu's
  Screenshots entry only on XBOX; the PS3's confirm-on-circle convention,
  `GUI.PS3EnterAssignedToX`), and the screens' own copies above are Xbox-only in the
  360 build whatever it says.
- **`X360_ControllerInteraction.GDA`** (in the title update's patch folder): 10 rows of
  an object ID and 18 flags - which interactions an object type allows. Nothing about
  prompts.

---

## 3. Design, as built (2026-10-27)

**A glyph set is a set of redirects - this image name drawn from that cell - applied to
the atlas descriptions.** No pixel changes. `app/src/main/cpp/warden_glyphs.cc`:

| Set | Redirects |
|---|---|
| Xbox | none - the game's own |
| Nintendo | `XBoxGamepad_BottomAction` and `_RightAction` trade cells (B at the bottom, A on the right), and `_TopAction` and `_LeftAction` (X on top, Y on the left); the dialogue pop-up's four likewise; character creation's and the tutorials' A from the B coin |
| PlayStation | every `XBoxGamepad_*` from its `PS3Gamepad_*`; the screens' copies from the PS3 cells, or from their own PS3 copies |

A redirect within a page rewrites the cell's rectangle; one to another page moves the
entry into that page's description. In Nintendo mode the shoulders, triggers, sticks
and Start/Back keep their Xbox labels (only the face buttons are relabelled, as section
1 says); PlayStation relabels everything, as the PS3 build did.

**How it reaches the game.** As the title loads (`Runtime::LoadTitle`, before the guest
starts), the app reads the descriptions out of the extracted `guiexport.erf` (zlib),
applies the set's redirects, and writes the changed ones into the override tree (PLAN
7.3: `files/override/`, laid over D:\ by `warden_fs.cc`) as
`packages/core/patch/atl_*_dat.xml` - so the title's own patch folder replaces the
archive's descriptions (`warden_glyphs_apply.cc`; Nintendo changes 4, PlayStation 6; the
Xbox set writes none and removes what another left).

**The setting.** Back -> Settings -> Button prompts: Xbox, Nintendo or PlayStation
(`GameActivity.showSettings`), kept in `files/glyphs`. **A change shows from the next
start**, and the dialog offers to quit now. Changing the prompts while the game runs
was tried - the title reads the descriptions again at every load - and its next load
crashed (BUG-061), so it is not done.

**Checking a change on the computer:** `python tools/check_glyphs.py` builds
`warden_glyphs.cc` with `tools/glyphs_host.cc`, applies both sets to the disc's
descriptions, and draws every redirected image as the game will draw it
(`work/glyphs/check_<set>.png`: the disc's image left, the new one right).
`tools/dao_gfx.py` reads the movies (tags, images, exports, ActionScript), and
`tools/dao_xds.py` decodes atlas pages and their cells.

---

## 4. Implementation order - done (2026-10-27, session 35)

1. Survey (7.1) - section 2.
2. Decode the GDA (7.2) - not about prompts.
3. Override pipeline (7.3) - `files/override/` over D:\, and through it the title's
   patch folder, which replaces any archived file by name; Phase 9's texture packs can
   use it.
4. Art (7.4) - none needed: the PlayStation set ships in the game; Nintendo is the Xbox
   letters.
5. Redirection (7.5) - the atlas descriptions.
6. Text path (7.6) - done by 7.5: the tokens are images of the same set.
7. Settings (7.7) - from the next start; live switching crashed (BUG-061).
8. Audit (7.8) - section 5.

---

## 5. Audit checklist (7.8)

Checked on the Thor (runs 346-352), in the sets each line names:

- [x] **Main menu** - the sign-in prompt: Y; △ with PlayStation, X with Nintendo (runs
      346-347, 350)
- [x] **In-game HUD and quickbar** - the face-button coins: △ □ ○ with PlayStation, X Y
      A with Nintendo (runs 346-347)
- [x] **Pause menu** (START) - the selected item's coin: ✕ (run 351)
- [x] **In-game menu** (BACK), its map tab - the tab bar's L2 and R2, "L Navigate",
      "✕ Toggle Legend", △, "Close ○" (run 352)
- [ ] Tutorial popups (`tutorial.gfx`: the tutorials' own A, and the `<A/>`-style tokens
      in their text) - not reached by script; in play
- [ ] Conversation and dialogue wheel (`conversation.gfx`, `dialoguepop.gfx` and its own
      A, B, X, Y) - in play
- [ ] The in-game menu's other tabs (LT/RT step through them): inventory, character
      record, level-up, journal and codex, tactics - in play
- [ ] Character creation (its own A and left stick), the world map, the radial / battle
      menu, party picker, crafting, item upgrade, save/load screens, the death screen -
      in play
- [x] **Stays Xbox, by design:** the controller-layout loading screen
      (`load_controller.gfx`) - one picture of an Xbox 360 pad with its coloured letters,
      not cells a redirect reaches

---

## 6. Open questions

| # | Question | Status |
|---|---|---|
| Q4 | Which atlas cells hold the four face-button glyphs? | **Answered** (section 2): `XBoxGamepad_*Action` in the shared atlas, plus the screens' copies |
| R7 | Are any glyphs baked into larger non-atlased art? | **One**: the controller-layout loading screen's picture of an Xbox pad |
| - | Do the 19 GDA columns include a glyph reference, or only action bindings? | **Neither** - object interaction flags (section 2) |
| - | Does the Thor's built-in pad report a vendor/product ID usable for auto-detection? | Not looked into: the setting is chosen by hand |
