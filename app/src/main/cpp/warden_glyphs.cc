// Button prompts for the controller in hand (PLAN Phase 7, docs/GLYPHS.md).
//
// The title draws every button prompt - the UI's own, and the `<A/>`-style tokens in
// its text, which the engine expands to `<img src='img://XBoxGamepad_BottomAction.dds'>`
// - from images named by the button's *position*: XBoxGamepad_BottomAction,
// _RightAction, _LeftAction, _TopAction, _LeftBumper and so on. Each name is a cell of
// a UI atlas, and the atlases' descriptions (guiexport.erf, atl_*_dat.xml) give each
// name its page and rectangle. The game was also released on the PS3, and the 360's
// shared atlas carries that build's complete set too, in the same art: PS3Gamepad_*
// (cross, circle, square, triangle, L1, R2, L3, Start, Select...).
//
// So a glyph set is a set of redirects - this name drawn from that cell - applied to
// the atlas descriptions, with no pixel changed:
//
//   PlayStation  every XBoxGamepad_* name from its PS3Gamepad_* cell
//   Nintendo     A and B trade cells, and X and Y: B drawn at the bottom, A on the
//                right, X on top, Y on the left
//
// plus the copies some screens keep of the face buttons in their own atlases (the
// dialogue pop-up's, character creation's, the tutorial's), found by eye in PLAN 7.1's
// survey. What a button does never changes - only the picture of it (D3, GLYPHS.md).
//
// No logging and no file access here, so tools/glyphs_host.cc builds this file on the
// computer; warden_glyphs_apply.cc reads the archive and writes the result.

#include "warden_glyphs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

namespace warden::glyphs {

namespace {

bool EqualsNoCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) ==
                  std::tolower(static_cast<unsigned char>(y));
         });
}

// The positions both consoles' sets name, from the shared atlas.
constexpr const char* kPositions[] = {
    "BottomAction", "RightAction", "LeftAction", "TopAction",
    "LeftBumper", "RightBumper", "LeftTrigger", "RightTrigger",
    "LeftStick", "RightStick", "LeftStickPush", "RightStickPush",
    "LSwithLeftAndRightArrows", "LSwithUpAndDownArrows",
    "Select", "Start", "SystemButton",
    "DPad", "DPad_Up", "DPad_Down", "DPad_Left", "DPad_Right",
};

// Screens' own copies of the Xbox prompts, and what each becomes. The PS3 build drew
// its own; on the 360 they are Xbox only, so they borrow the shared PS3 cells (or a
// screen's own PS3 copy, where it has one).
constexpr Redirect kPlayStationCopies[] = {
    {"DialoguePop_I3A.dds", "PS3Gamepad_BottomAction.dds"},  // A
    {"DialoguePop_I41.dds", "PS3Gamepad_RightAction.dds"},   // B
    {"DialoguePop_I46.dds", "PS3Gamepad_TopAction.dds"},     // Y
    {"DialoguePop_I4B.dds", "PS3Gamepad_LeftAction.dds"},    // X
    {"CharGen_I198.dds", "PS3Gamepad_BottomAction.dds"},     // A
    {"CharGen_I164.dds", "CharGen_I166.dds"},                // LS -> its own L
    {"Tutorial_I11D.dds", "PS3Gamepad_BottomAction.dds"},    // A, the tutorials'
    {"abilities_IE4.dds", "abilities_IE6.dds"},              // RS -> its own R
    {"NewContentAvailable_I2.dds", "PS3Gamepad_LeftStick.dds"},
    {"NewContentAvailable_I4.dds", "PS3Gamepad_RightStick.dds"},
    {"Portraits_IF.dds", "PS3Gamepad_RightBumper.dds"},      // RB, party switching
};

// Nintendo's letters on the same positions: A and B swapped, X and Y swapped.
constexpr Redirect kNintendo[] = {
    {"XBoxGamepad_BottomAction.dds", "XBoxGamepad_RightAction.dds"},
    {"XBoxGamepad_RightAction.dds", "XBoxGamepad_BottomAction.dds"},
    {"XBoxGamepad_TopAction.dds", "XBoxGamepad_LeftAction.dds"},
    {"XBoxGamepad_LeftAction.dds", "XBoxGamepad_TopAction.dds"},
    {"DialoguePop_I3A.dds", "DialoguePop_I41.dds"},
    {"DialoguePop_I41.dds", "DialoguePop_I3A.dds"},
    {"DialoguePop_I46.dds", "DialoguePop_I4B.dds"},
    {"DialoguePop_I4B.dds", "DialoguePop_I46.dds"},
    {"CharGen_I198.dds", "XBoxGamepad_RightAction.dds"},
    {"Tutorial_I11D.dds", "XBoxGamepad_RightAction.dds"},
};

// One <SourceTexture .../> element of an atlas description.
struct Element {
  size_t file = 0;      // index into the files
  size_t begin = 0;     // the element's text, from '<' to after "/>"
  size_t end = 0;
  size_t line = 0;      // where its line starts, for its indentation
  std::string page;     // the <AtlasTexture> it is in
  size_t page_close = 0;  // that block's "</AtlasTexture>"
  std::string name;
};

// The value of attribute `key` in an element's text; empty if absent.
std::string Attribute(std::string_view element, std::string_view key) {
  size_t at = 0;
  while ((at = element.find(key, at)) != std::string_view::npos) {
    const size_t eq = at + key.size();
    const bool word_start = at > 0 && std::isspace(static_cast<unsigned char>(element[at - 1]));
    if (word_start && eq + 1 < element.size() && element[eq] == '=' && element[eq + 1] == '"') {
      const size_t close = element.find('"', eq + 2);
      if (close != std::string_view::npos) {
        return std::string(element.substr(eq + 2, close - (eq + 2)));
      }
    }
    at = eq;
  }
  return {};
}

// `element` with attribute `key`'s value replaced.
std::string WithAttribute(std::string element, std::string_view key, std::string_view value) {
  size_t at = 0;
  while ((at = element.find(key, at)) != std::string::npos) {
    const size_t eq = at + key.size();
    const bool word_start = at > 0 && std::isspace(static_cast<unsigned char>(element[at - 1]));
    if (word_start && eq + 1 < element.size() && element[eq] == '=' && element[eq + 1] == '"') {
      const size_t close = element.find('"', eq + 2);
      if (close != std::string::npos) {
        element.replace(eq + 2, close - (eq + 2), value);
        return element;
      }
    }
    at = eq;
  }
  return element;
}

void Scan(const std::string& text, size_t file, std::vector<Element>& out) {
  size_t at = 0;
  while ((at = text.find("<AtlasTexture", at)) != std::string::npos) {
    const size_t head_end = text.find('>', at);
    const size_t close = text.find("</AtlasTexture>", at);
    if (head_end == std::string::npos || close == std::string::npos) {
      return;
    }
    const std::string page = Attribute(std::string_view(text).substr(at, head_end - at), "Name");
    size_t e = head_end;
    while ((e = text.find("<SourceTexture", e)) != std::string::npos && e < close) {
      const size_t end = text.find("/>", e);
      if (end == std::string::npos || end > close) {
        break;
      }
      Element element;
      element.file = file;
      element.begin = e;
      element.end = end + 2;
      element.line = text.rfind('\n', e);
      element.line = element.line == std::string::npos ? 0 : element.line + 1;
      element.page = page;
      element.page_close = close;
      element.name = Attribute(std::string_view(text).substr(e, element.end - e), "Name");
      out.push_back(std::move(element));
      e = element.end;
    }
    at = close + 1;
  }
}

struct Edit {
  size_t begin;
  size_t end;
  std::string text;
};

}  // namespace

std::optional<Set> ParseSet(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  if (EqualsNoCase(text, "xbox")) return Set::kXbox;
  if (EqualsNoCase(text, "nintendo")) return Set::kNintendo;
  if (EqualsNoCase(text, "playstation")) return Set::kPlayStation;
  return std::nullopt;
}

const char* SetName(Set set) {
  switch (set) {
    case Set::kNintendo:
      return "nintendo";
    case Set::kPlayStation:
      return "playstation";
    default:
      return "xbox";
  }
}

std::vector<Redirect> Redirects(Set set) {
  std::vector<Redirect> out;
  if (set == Set::kPlayStation) {
    // Built from the position names, once: each XBoxGamepad_ name, then its PS3 one.
    static const std::vector<std::string> names = [] {
      std::vector<std::string> list;
      for (const char* position : kPositions) {
        list.push_back(std::string("XBoxGamepad_") + position + ".dds");
        list.push_back(std::string("PS3Gamepad_") + position + ".dds");
      }
      return list;
    }();
    for (size_t i = 0; i < names.size(); i += 2) {
      out.push_back({names[i].c_str(), names[i + 1].c_str()});
    }
    out.insert(out.end(), std::begin(kPlayStationCopies), std::end(kPlayStationCopies));
  } else if (set == Set::kNintendo) {
    out.assign(std::begin(kNintendo), std::end(kNintendo));
  }
  return out;
}

std::map<std::string, std::string> PatchAtlases(
    const std::map<std::string, std::string>& atlases, Set set, std::string* report) {
  std::vector<std::string> names;
  std::vector<std::string> texts;
  std::vector<Element> elements;
  for (const auto& [name, text] : atlases) {
    names.push_back(name);
    texts.push_back(text);
    Scan(text, names.size() - 1, elements);
  }
  auto find_all = [&](std::string_view image) {
    std::vector<const Element*> found;
    for (const Element& element : elements) {
      if (EqualsNoCase(element.name, image)) {
        found.push_back(&element);
      }
    }
    return found;
  };
  // A name can be on two pages (the login screens keep their own D-pad); the source
  // is the shared atlas's where it has one.
  auto find_source = [&](std::string_view image) -> const Element* {
    const Element* first = nullptr;
    for (const Element* element : find_all(image)) {
      if (names[element->file].find("shared") != std::string::npos) {
        return element;
      }
      if (!first) {
        first = element;
      }
    }
    return first;
  };

  std::vector<std::vector<Edit>> edits(texts.size());
  for (const Redirect& redirect : Redirects(set)) {
    const Element* source = find_source(redirect.source);
    const std::vector<const Element*> targets = find_all(redirect.image);
    if (!source || targets.empty()) {
      if (report) {
        *report += std::string("  ") + redirect.image + " <- " + redirect.source + ": " +
                   (source ? "no such image" : "no such source") + "\n";
      }
      continue;
    }
    // From the source's text before any edit, so swaps read the original cells.
    const std::string& source_text = texts[source->file];
    const std::string moved = WithAttribute(
        source_text.substr(source->begin, source->end - source->begin), "Name",
        redirect.image);
    for (const Element* target : targets) {
      if (target->file == source->file && target->page == source->page) {
        edits[target->file].push_back({target->begin, target->end, moved});
      } else {
        // Another page: the entry moves to the source's page - out of its own
        // description (its whole line), into the source's.
        const std::string& text = texts[target->file];
        size_t line_end = text.find('\n', target->end);
        line_end = line_end == std::string::npos ? target->end : line_end + 1;
        edits[target->file].push_back({target->line, line_end, ""});
        const std::string indent = source_text.substr(source->line, source->begin - source->line);
        const std::string newline =
            source->line >= 2 && source_text[source->line - 2] == '\r' ? "\r\n" : "\n";
        // Before the line holding "</AtlasTexture>", indented as the source's line.
        size_t close_line = source_text.rfind('\n', source->page_close);
        close_line = close_line == std::string::npos ? source->page_close : close_line + 1;
        edits[source->file].push_back({close_line, close_line, indent + moved + newline});
      }
      if (report) {
        *report += std::string("  ") + redirect.image + " (" + target->page + ") <- " +
                   redirect.source + " (" + source->page + ")\n";
      }
    }
  }

  std::map<std::string, std::string> changed;
  for (size_t f = 0; f < texts.size(); ++f) {
    if (edits[f].empty()) {
      continue;
    }
    // Back to front, so the offsets before each edit stay valid; insertions at one
    // place keep their order.
    std::stable_sort(edits[f].begin(), edits[f].end(),
                     [](const Edit& a, const Edit& b) { return a.begin > b.begin; });
    std::string text = texts[f];
    for (const Edit& edit : edits[f]) {
      text.replace(edit.begin, edit.end - edit.begin, edit.text);
    }
    changed[names[f]] = std::move(text);
  }
  return changed;
}

}  // namespace warden::glyphs
