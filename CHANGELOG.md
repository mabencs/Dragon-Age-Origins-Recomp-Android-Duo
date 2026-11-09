# Changelog

## 0.2.2-alpha - "Open Alpha"

### Changed

- **Much better standby battery.** You can close the lid or lock the screen with Dragon Age
  open and come back to almost the same charge. On the AYN Thor, an hour of closed-lid standby
  with the game open took the battery from 100% to 99%.
  - Before, about half the times you closed the lid during play, the game kept a whole CPU core
    busy until you came back. That core now waits with the rest of the game.
  - The app also stops its background work while the game is put away: the sound, the second
    screen and the on-screen controller.
  - The launcher open on its own now uses no CPU at all.
- Playing is unchanged: the same frame rate, and the game picks up where you left it.

### Measured: CPU while the screen is locked

On the AYN Thor, as a share of one CPU core:

| With the screen locked | 0.2.1-alpha | 0.2.2-alpha |
|---|---|---|
| In play | up to 94% (about half the times) | under 2%, every time |
| At the title screen | 29% | 1.4% |
| The launcher, no game started | 7.7% | 0% |

### Installing

- **From 0.2.0-alpha or 0.2.1-alpha:** the launcher installs this update under **Launch Dragon
  Age**. No Build again is needed. Your saves, game data and settings stay.
- **From 0.1.9-alpha or earlier:** the launcher installs it too, and then shows **Build again**.
  Choose it once (about eight minutes). The game's code changed in 0.2.0-alpha, so it has to be
  made again.

## 0.2.1-alpha - "Open Alpha"

### New

- **On-screen controller size**, on the Game tab of Settings: from 50% to 150% (100% as
  before). Smaller shows more of the game; bigger is easier to hit. Controls near an edge move
  in as they grow, so none is cut off. It changes at once, in the game too.
- **Adjust on-screen controls layout**, on the Game tab: drag each on-screen control - the
  buttons, the D-pad, the sticks - where you want it. **Done**, small at the top, saves the layout
  and takes you back to the settings (the controller's A, B or Start does too); **Reset** puts
  everything back where it started. The game waits while you place them, and nothing you touch
  reaches it.

### Changed

- **The launcher and the settings are on tabs:**
  - **Launcher** - Launch, your files, Build, Saves, Mods and About (before the game only).
  - **Visual** - frame rate, resolution, upscaling, frame generation, post-processing and the
    rest of the picture.
  - **Audio** - Headphones and Volume boost.
  - **Performance** - Heat guard and the Performance tab/overlay.
  - **Game** - button prompts, intro logos, the autosave, the second screen, the on-screen
    controller and the cheats.

  Tap a tab's name, or press LB and RB. In the game, the panel opens on the tab you left it on.

### Installing

- **From 0.2.0-alpha:** the launcher installs this update under **Launch Dragon Age**. No Build
  again is needed. Your saves, game data and settings stay.
- **From 0.1.9-alpha or earlier:** the launcher installs it too, and then shows **Build again**.
  Choose it once (about eight minutes). The game's code changed in 0.2.0-alpha, so it has to be
  made again.

## 0.2.0-alpha - "Open Alpha"

<!-- manual-update -->

### Changed

- **The game runs much cooler, at the same frame rate.** The game's threads used to keep CPU cores
  spinning flat out while they waited, for each other and for the GPU. Now they rest while they
  wait. This is on by default. On the AYN Thor at Frame rate 30, the hottest CPU core runs about
  21 C cooler and the handheld draws about a quarter less power.
- **Heat guard is now a section of its own in Settings**, with three options, each turned on or
  off by itself:
  - **Rest the CPU in the game's waits** - on by default. If you notice stutter, try it off.
  - **Rest the CPU while the GPU draws** - on by default.
  - **Lower the frame rate when hot** - the old Heat guard, still off by default. It now steps in
    at temperatures an AYN Thor with its fan running actually reaches (the GPU at 75 C or a CPU
    core at 88 C). Before, it waited for 90 C on the GPU and never acted.
- **A diagnostic record of the game's functions is now kept only in Debugging Mode**, for less
  CPU work.

### Measured: before (both rests off) and after (both on)

On the AYN Thor: 1360x768, FXAA, FSR 3.1 off, the fan at a fixed 50% above 45 C. Each King's Camp
run was 12 minutes, and its numbers are from the last 4 minutes, once the heat had levelled out.

| Where | Run length | Frame rate setting | fps | 1% low fps | Hottest CPU core | CPU cores, average | GPU |
|---|---|---|---|---|---|---|---|
| King's Camp | 12 min | 30, frame generation off | 30.0 → 30.0 | 22.5 → 27.5 | 94.6 → 73.6 C (−21) | 84.2 → 69.6 C (−14.6) | 75.0 → 65.5 C (−9.5) |
| King's Camp | 12 min | 120, frame generation off | 40.3 → 40.5 | 25.2 → 34.9 | 94.9 → 85.4 C (−9.5) | 85.8 → 80.5 C (−5.3) | 80.5 → 76.0 C (−4.5) |
| King's Camp | 12 min | 120, Lossless 2x | 29.5 → 30.0 | 23.8 → 26.7 | 94.5 → 78.4 C (−16.1) | 84.2 → 73.0 C (−11.2) | 78.3 → 70.2 C (−8.1) |

At the King's Camp at Frame rate 30, the handheld drew 10.5 W before and 7.8 W after. Stutters
(frames over 50 ms) went from 11 to 0 in those 4 minutes, and from 2 to 0 at 120.

### Installing

- **This update is installed by hand.** The launcher shows **Manual update**, which opens this
  page: download the APK below and install it over the app. Your saves, game data and settings
  stay.
- **Then choose Build again** in the launcher, once (about eight minutes). The new rest for the
  render thread needs the game's code made again.

## 0.1.8-alpha - "Open Alpha"

### New

- **Aspect ratio: 16:9 or 4:3 (experimental)**, in Settings above Rendering resolution. 4:3 is
  for 4:3 screens such as 1280x960 handhelds: the game draws its own 4:3 picture and lays out its
  HUD, menus and conversations for it, filling the screen. Rendering resolution then offers
  640x480, 960x720, 1280x960 and 1440x1080, and FSR 3.1 and frame generation work at 4:3 too.
  16:9 stays the default.
- **4:3 is experimental:** it has only been tested on the AYN Thor's widescreen display (where it
  shows with bars at the sides), not yet on an actual 4:3 screen. If you try it on one, a report
  of how the launcher, the settings, and the game look is very welcome.

### Installing

Updates from the launcher: 0.1.5-alpha to 0.1.7-alpha install this version under **Launch Dragon
Age**. From 0.1.2-alpha to 0.1.4-alpha the launcher's update can fail after you tap Update - tap
**Try again**, or download the APK below and install it over the app by hand. Your saves, game
data and settings stay.

## 0.1.7-alpha - "Open Alpha"

### Fixed

- **The game's volume now follows Android's volume exactly.** On the AYN Thor, apps shown on
  the second screen get a separate volume of their own, and the deck put the game in that
  group. That is why holding volume-up at the top could make the game louder still, and why
  muting while the game was put away could leave it too quiet afterwards. The deck is now drawn
  as part of the game's own window, so the game follows only Android's media volume. With the
  deck turned off, the volume is no longer applied twice either.

### Changed

- **The party on the deck's map:** smaller icons that move smoothly as the party walks, and the
  character you control is marked with a gold ring. It follows when you switch characters.

### Installing

Updates from the launcher: 0.1.5-alpha and 0.1.6-alpha install this version under **Launch
Dragon Age**. From 0.1.2-alpha to 0.1.4-alpha the launcher's update can fail after you tap
Update - tap **Try again**, or download the APK below and install it over the app by hand. Your
saves, game data and settings stay.

## 0.1.6-alpha - "Open Alpha"

### Fixed

- **The deck's map was upside down in some areas**, among them the King's Camp at Ostagar,
  the City Elf alienage, and the Tower of Ishal's top floor. It now faces the same way as the
  game's own map screen and minimap, in every area.

### Changed

- **A bigger map on the deck:** it now takes up nearly the whole tab, with its hint written
  over the map instead of below it.
- **The line at the bottom of the deck** (the game's title and a count of values) is gone from
  every tab. It only appears while the deck is waiting for the game.

### Installing

Updates from the launcher: 0.1.5-alpha installs this version under **Launch Dragon Age**.
From 0.1.2-alpha to 0.1.4-alpha the launcher's update can fail after you tap Update - tap
**Try again**, or download the APK below and install it over the app by hand. Your saves, game
data and settings stay.

## 0.1.5-alpha - "Open Alpha"

### Fixed

- **Updating from the launcher could fail right after you tapped Update** in Android's dialog,
  with "The update failed: the install was cancelled". The launcher came back to the front and
  cancelled the install Android had just started. It now waits for Android to finish. If
  Android's dialog was closed without installing, the row offers **Try again** after half a
  minute.

### Installing

0.1.2-alpha to 0.1.4-alpha still have the old launcher, so updating to this version from them
can fail the same way. Tap **Try again** - it sometimes gets through - or download the APK below
and install it over the app by hand: your saves, game data and settings stay. Updates from this
version on install normally.

## 0.1.4-alpha - "Open Alpha"

### New

- **See releases on GitHub.** A new row under **Launch Dragon Age** opens the project's
  releases page in your browser, where you can see every version and what each one changed.
  The app itself downloads nothing for it - your browser opens the page.

### Installing

Updates from the launcher: 0.1.2-alpha and 0.1.3-alpha offer this version under **Launch
Dragon Age**. Earlier versions are installed by hand; your saves, game data and settings stay.

## 0.1.3-alpha - "Open Alpha"

### New

- **The deck's Map tab, redrawn.** Each marker is now an icon of what it is - a moneybag for
  a merchant, an anvil for a smith, an exclamation mark for a quest giver, a door for an exit,
  a tent for a camp, and so on. Tap an icon to show its name, and again to hide it. Pinch to
  zoom (up to 6x) and drag to pan; zoomed out, icons may overlap a little, and they spread
  apart as you zoom in. Names have a dark outline and stay readable over the map.
- **Corner vignette** in Settings: the game darkens the corners of the picture in play. You
  can now turn that off - in the launcher's settings, or in the options panel while playing,
  where it takes effect at once.
- **Release notes before an update.** When the launcher finds a newer version, Download &
  install first shows what that version changes, and you choose whether to go on.

### Fixed

- **The upper corners of the world juddered with FSR 3.1 on**, whether the camera moved or
  not. The game's corner darkening was mistaken for part of the HUD and drawn from the
  unstabilised frame; it is now treated as the shade it is. With frame generation on, the same
  corners also stepped at the game's own frame rate - fixed by the same change.
- **With FSR 3.1 chosen on a device where it cannot run, the whole world juddered** under a
  still HUD. The game now runs without FSR 3.1 there, steadily, and says so in its log.

### Changed

- The debug build, for people building from source, is named **DAO Debug** on the home screen,
  so it can be told from the published app installed beside it.

### Installing

Updates from the launcher: 0.1.2-alpha offers this version under **Launch Dragon Age**.
Earlier versions are installed by hand; your saves, game data and settings stay.

## 0.1.2-alpha - "Open Alpha"

### New

- **Updates from the launcher.** Each time the launcher opens it asks GitHub whether a newer
  version is out. If so, a row under Launch offers **Download & install**: it checks the
  download against GitHub's checksum and hands it to Android, which asks you to confirm. Your
  saves, game data and settings stay. The first time, Android also asks you to allow installs
  from this app, and the app closes as it installs, so open it again after.
- Some releases are marked **Manual update**: the row then opens the release's page.

**This changes the app's privacy promise.** It used to have no network permission at all. The
launcher now connects to GitHub for this, and for nothing else. The game itself stays offline,
and nothing about you or your game is sent, though GitHub sees an ordinary request from your
connection. Without a connection there is no row, and the game plays as ever.

### Fixed

- **The second screen could be missing after the device woke**, showing the home app instead of
  the deck until the app was restarted. The deck now waits for the screen to come on, and comes
  back with the game even if it had closed.

### Known limits

- Updates from the launcher start with this version: 0.1.1-alpha and earlier need this one
  installed by hand.

## 0.1.1-alpha - "Open Alpha"

An update for the second screen and three fixes. It installs over 0.1.0-alpha with your
saves and game data as they are, and the game's code you already made keeps working: there
is nothing to rebuild.

### Fixed

- **Conversations could freeze the game for nearly 4 seconds** as they started, with an
  upscaler and Lossless 2x frame generation on. In the same conversation, the worst frame
  went from over 2 seconds to under 67 ms. Menus use the same path and should no longer
  rebuild the frame generator.
- **The game's sound ignored the media volume on AYN devices.** It could still be heard with
  the volume at 0. It now follows every step, down to silence. Volume boost is applied
  first. Over Bluetooth the headset keeps control of the volume.
- **The second screen could stop responding and close** after the top display was left on
  the second screen, for example by the Thor's dual-screen assistant. The focus now goes
  back to the game by itself.

### Second screen

- **Quests** now has the journal's Current and Completed pages and its quest groups, in the
  game's order. Groups and quests collapse and expand with a tap.
- **Codex** categories collapse and expand, and entries can be read as soon as they are
  unlocked, whether or not you have opened them in the game. Reading one on the deck leaves
  it marked unread in the game.
- **Items:** a tap shows an item's stats, properties, requirements and description, as the
  game's Inspect does.
- **Items:** Equip, Use and Unequip buttons, for the character selected in the game, while
  exploring or fighting. What that character cannot use is greyed out.
- What you have unfolded on Quests and Codex is remembered between starts.

### Known limits

- A trap's Use does nothing from the deck, because it needs a place on the ground.
- Item details leave out the item's type, material and rune slots.
- Unequip works only on the selected character's items.
- The Completed Quests page has not yet been tested with a finished quest in it.
- The volume fix has been tested on the Thor's speaker only, not on wired, USB or Bluetooth
  audio.

The open bugs from 0.1.0-alpha still stand: `docs/BUGS.md` has them.

## 0.1.0-alpha - "Open Alpha"

The first public release.
