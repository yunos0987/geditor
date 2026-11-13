// Settings: the model of docs/PLAN.md 4.5 with its built-in defaults, the
// validation of a settings file, and the keymap layers of PLAN 4.3
// (settings > Windows compatible keys > Emacs defaults).
//
// M2 applies only the key settings; the other items are validated and kept
// for the milestones that use them (decision D1).
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "jsonc.h"
#include "key.h"
#include "keymap.h"

namespace geditor {

struct Diagnostic {
  TextPos pos;
  std::wstring message;
};

enum class KeymapKind { kGlobal, kMinibuffer, kIsearch, kQueryReplace };
constexpr int kKeymapCount = 4;

// One item of "keys" / "minibuffer_keys" / "isearch_keys" /
// "query_replace_keys", validated except for prefix conflicts.
struct KeySetting {
  KeySequence keys;
  TextPos pos;          // the key string of the item
  bool unbind = false;  // null
  Binding binding;      // when !unbind
};

enum class WrapMode { kNone, kWindow, kFixed, kMax };
enum class CaseFold { kSmart, kAlways, kNever };

enum ColorIndex {
  kColorBackground,
  kColorForeground,
  kColorCursor,
  kColorSelection,
  kColorLineNumber,
  kColorLineNumberCurrent,
  kColorSearchMatch,
  kColorSearchCurrent,
  kColorWhitespace,
  kColorEol,
  kColorEolOther,
  kColorModeLineBackground,
  kColorModeLineForeground,
  kColorCount,
};

struct Config {
  std::wstring font_family = L"Consolas";
  double font_size = 12;  // points
  std::vector<std::wstring> font_fallback;
  // 0xRRGGBB, indexed by ColorIndex.
  std::array<uint32_t, kColorCount> colors = {
      0x1e1e1e, 0xd4d4d4, 0xffffff, 0x264f78, 0x858585, 0xc6c6c6, 0x613214,
      0x9e6a03, 0x404040, 0x505050, 0xd7ba7d, 0x007acc, 0xffffff,
  };
  int tab_width = 4;
  bool line_numbers = true;
  bool menu_bar = true;
  WrapMode wrap_mode = WrapMode::kMax;
  int wrap_column = 80;
  bool whitespace_space = false;
  bool whitespace_tab = true;
  bool whitespace_fullwidth_space = true;
  bool whitespace_trailing = true;
  bool eol_marks_show = true;
  std::wstring eol_mark_lf = L"↓";
  std::wstring eol_mark_crlf = L"↵";
  std::wstring eol_mark_cr = L"←";
  std::wstring new_file_encoding = L"utf-8";
  std::wstring new_file_eol = L"lf";
  bool backup = true;
  CaseFold case_fold = CaseFold::kSmart;
  // Key settings in file order, per KeymapKind.
  std::array<std::vector<KeySetting>, kKeymapCount> keys;
};

struct ConfigParse {
  bool syntax_ok = false;  // false: the file is rejected as a whole
  Config config;           // defaults overlaid with the valid items
  std::vector<Diagnostic> errors;
};

// Parses and validates settings text (UTF-8 JSONC). Semantic errors (unknown
// item, wrong type or range, invalid colour, invalid key notation, unknown
// command, wrong arguments, an item written twice) drop only their item.
ConfigParse parse_config(std::string_view utf8);

// The built-in layers of a keymap kind: Emacs defaults and the Windows
// compatible keys over them (global only).
std::span<const DefaultBinding> emacs_layer(KeymapKind kind);
std::span<const DefaultBinding> compat_layer(KeymapKind kind);

// Builds a keymap: `emacs`, then `compat` over it, then `settings` in file
// order. A setting that conflicts with a prefix is skipped and reported in
// `errors` (when not null).
Keymap build_keymap(std::span<const DefaultBinding> emacs, std::span<const DefaultBinding> compat,
                    std::span<const KeySetting> settings, std::vector<Diagnostic>* errors);

// Echo area lines for settings errors: "<file>:<line>:<column>: <message>",
// at most `max_lines`; when there are more errors, the last line is
// "... and N more".
std::vector<std::wstring> format_diagnostics(std::span<const Diagnostic> errors,
                                             std::wstring_view file_name, size_t max_lines);

// Text of a new settings file (find-user-init-file): the example of PLAN 4.5
// as comments, then "{}", so that saving it unchanged changes nothing.
std::wstring_view settings_template();

}  // namespace geditor
