// UTF-16 helpers, simple case mapping and word classes.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace geditor {

inline bool is_high_surrogate(wchar_t c) {
  return c >= 0xD800 && c <= 0xDBFF;
}

inline bool is_low_surrogate(wchar_t c) {
  return c >= 0xDC00 && c <= 0xDFFF;
}

inline char32_t combine_surrogates(wchar_t high, wchar_t low) {
  return 0x10000 + ((static_cast<char32_t>(high) - 0xD800) << 10) +
         (static_cast<char32_t>(low) - 0xDC00);
}

// Appends `c` to `out` as UTF-16 (one unit or a surrogate pair).
void append_utf16(std::wstring& out, char32_t c);
std::wstring to_utf16(char32_t c);

// Decodes the code point that starts at `i` in `s`; `units` receives 1 or 2.
// A lone surrogate is returned as itself.
char32_t decode_at(std::wstring_view s, size_t i, size_t* units);

// Simple case mapping (UnicodeData.txt fields 12/13) for the blocks Basic
// Latin, Latin-1 Supplement, Latin Extended-A, Greek and Coptic (U+0370..03FF),
// Cyrillic (U+0400..04FF), Latin Extended Additional and Halfwidth/Fullwidth
// Forms. Other characters map to themselves.
char32_t to_upper(char32_t c);
char32_t to_lower(char32_t c);

// Case-insensitive comparison (per UTF-16 unit, using to_lower).
bool fold_equal(std::wstring_view a, std::wstring_view b);
bool fold_starts_with(std::wstring_view s, std::wstring_view prefix);
int fold_compare(std::wstring_view a, std::wstring_view b);

// Word classes: a word is a run of characters of one class other than kNone.
// kExtender (U+30FC prolonged sound mark, U+3099..309C voiced / semi-voiced
// marks and their half-width forms) continues the class of the character
// before it.
enum class WordClass { kNone, kAlnum, kHiragana, kKatakana, kHan, kExtender };
WordClass word_class(char32_t c);

// Converts UTF-8 text without validation errors (invalid bytes become U+FFFD).
std::wstring widen(std::string_view utf8);

// Converts "\r\n" and lone "\r" to "\n" (text entering the buffer).
std::wstring normalize_line_ends(std::wstring_view text);

}  // namespace geditor
