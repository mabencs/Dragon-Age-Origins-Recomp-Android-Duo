// DAORecomp's text helpers (text.h) for Android, as far as the mod converter (PLAN 10.13)
// needs them. Paths are UTF-8 here already, so most of it is the identity; wide strings
// hold UTF-16 code units (the PC archives' names) or UTF-32 (std::filesystem's wide
// forms), both written out as UTF-8.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace dao {

// Code units (UTF-16 pairs joined, or UTF-32) as UTF-8; unpaired surrogates as U+FFFD.
inline std::string WideToUtf8(std::wstring_view text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    uint32_t c = uint32_t(text[i]);
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && uint32_t(text[i + 1]) >= 0xDC00 &&
        uint32_t(text[i + 1]) <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (uint32_t(text[i + 1]) - 0xDC00);
      ++i;
    } else if ((c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) {
      c = 0xFFFD;
    }
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xF0 | (c >> 18));
      out += char(0x80 | ((c >> 12) & 0x3F));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  return out;
}

inline std::string Utf8(const std::filesystem::path& path) { return path.string(); }

inline std::filesystem::path FromUtf8(std::string_view text) {
  return std::filesystem::path(std::string(text));
}

inline bool IsUtf8(std::string_view) { return true; }

inline std::optional<std::string> ToAnsi(const std::filesystem::path& name) {
  return name.string();
}

// ASCII lowercase.
inline std::string Lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    c = char(tolower(uint8_t(c)));
  }
  return out;
}

}  // namespace dao
