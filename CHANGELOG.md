# Changelog

## Unreleased

### Fixed

- **Updating from the launcher could fail right after you tapped Install** in Android's dialog,
  with "the install was cancelled": the launcher, back in front, cancelled the install Android
  had just started. It now waits for Android's answer. If Android's dialog was closed without
  installing, the row offers **Try again** after half a minute. This fix works from the version
  after this one onwards; from 0.1.4-alpha and earlier, try again, or install the APK from the
  releases page by hand over the app (your saves, game data and settings stay).

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
