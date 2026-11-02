// The title's on-screen keyboard (XamShowKeyboardUI - a character's name), answered by
// the app's own text dialog. The guest call files the prompt and returns pending, as
// the console's does; GameActivity polls for it on the main thread (Java calls native,
// never the reverse - NativeBridge's rule) and answers with the text or a cancel, which
// writes the guest's buffer and completes the title's overlapped.
#pragma once

#include <cstdint>
#include <string>

namespace warden {

struct KeyboardPrompt {
  std::u16string title;
  std::u16string description;
  std::u16string default_text;
  // Characters the title's buffer holds, without the terminator.
  uint32_t max_chars = 0;
};

// The prompt waiting for an answer, if there is one.
bool PendingKeyboardPrompt(KeyboardPrompt* prompt);

// Answers the waiting prompt: the text (cut to max_chars) when accepted, a cancel
// otherwise. Does nothing when no prompt is waiting.
void AnswerKeyboardPrompt(const std::u16string& text, bool accepted);

}  // namespace warden
