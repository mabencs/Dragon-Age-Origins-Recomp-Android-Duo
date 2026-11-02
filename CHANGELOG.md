# Changelog

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
