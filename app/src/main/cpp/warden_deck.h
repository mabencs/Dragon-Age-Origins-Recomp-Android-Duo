// PLAN 8.3: the second screen's state mirror - what the game's own UI is shown, by the
// engine's token paths, for the deck (DeckActivity) to read. See warden_deck.cc.
#ifndef WARDEN_DECK_H_
#define WARDEN_DECK_H_

#include <cstdint>
#include <string>
#include <vector>

namespace warden {
// The engine's game mode (warden_game.cc): 0 explore, 1 combat, 3 menu, 4 conversation ...
int32_t GameMode();
}  // namespace warden

namespace warden::deck {

// The latest published snapshot: one "path\tvalue" line each, or empty before the first.
// Any thread; never waits on the game's.
std::string Snapshot();

// The frames, for the deck's performance tab: the game's swaps and the panel's presents
// so far, and the game's last frame intervals (nanoseconds, oldest first).
struct FrameStats {
  int64_t now_nanos = 0;
  uint64_t guest_swaps = 0;
  uint64_t presents = 0;
  std::vector<int64_t> intervals;
};
FrameStats Frames();

// Called where the frames are counted (warden_frame_times.cc).
void NoteGuestSwap(int64_t now_nanos);
void NotePresent();

// PLAN 8.8: the touch quickbar - the deck's one write path. A tap on quick slot `slot`
// (0-5, the HUD's GUI.SelectedChar.Stats.QuickSlots.N) becomes the press that slot's
// own button makes: a short synthetic face-button press (slots 3-5 with RT held, the
// 360's second set), merged into the pad the game polls, exactly as a real press. Only
// while the game explores or fights (the mirror's own GameModeController.CurrentGameMode),
// so a tap never lands in a menu or a conversation. Returns "" when queued, else why not.
std::string QueueQuickslot(int slot);

// Called from XamInputGetState for the first pad: ORs in the queued press while it lasts.
// Returns true while a quick-slot press is being held (the pad then counts as connected).
bool ApplyQuickslot(uint16_t& buttons, uint8_t& right_trigger);

// The deck's faces: the HUD's party portraits, read back from the GPU's resolves while
// the deck pulls (the Vulkan backend calls the first two; see warden_deck.cc).
bool WantsPortraits();
void NotePortrait(uint32_t address, const uint8_t* data, uint32_t length);
// A party member's portrait made, by its name ("portrait_gen00fl_alistair2"), from the
// game's own portrait builder (warden_game.cc): its first draw is the next new address.
void NotePortraitMade(const std::string& name);
// Each party slot's face generation (0 none yet), changed when its face is redrawn.
std::vector<uint32_t> PortraitGenerations();
// A slot's face, 146x146 RGBA, or empty.
std::vector<uint8_t> Portrait(int slot);

// The performance tab's and overlay's picture lines: what the game's frames go through
// as it runs (not what the options say for the next start). Set where each is decided.
enum class RenderNote { kFrameGeneration, kFsr3, kPaint, kOutput };
void SetRenderNote(RenderNote which, const std::string& value);
// "Render.<name>\t<value>" lines: FrameGeneration, Upscaler, Size (rendered), Output.
std::string RenderReport();

}  // namespace warden::deck

#endif  // WARDEN_DECK_H_
