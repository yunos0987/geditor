// Parts of the headless mode (--headless, docs/PLAN.md 2 "headless") that
// need no window: the WM_COPYDATA requests of the tools (tools/app_driver.py)
// and the state text that they read back from a file.
//
// Requests (dwData: four ASCII letters, little-endian; payload UTF-16):
//   'GCAP' path   draw the current frame into a BMP file (any mode)
//   'GFLT' name   make the next "target" / "font" creation fail (any mode),
//                 the next HMENU creation ("menu") or context menu
//                 ("popup", as if TrackPopupMenu failed) fail (any mode),
//                 or the next "clipboard-read" / "clipboard-write" of the
//                 private clipboard fail (headless only); "shape": the next
//                 shaping of a cluster; "color": the next colour glyph (any
//                 mode, M3d decision D3); "file-info": the next reading of
//                 a file's time and attributes; "save-inspect", "save-link",
//                 "save-temp-create", "save-temp-write", "save-flush",
//                 "save-replace", "save-move", "save-backup",
//                 "save-in-place": that step of the next save (any mode,
//                 M4a decision F4)
//   'GRDW' count  draw `count` frames at once (1..1000) and put their times
//                 into the state (redraw_* lines; any mode, M3d [C4])
//   'GSTA' path   write the state (state_line lines, UTF-16LE) to a file
//   'GCBW' text   put `text` (CRLF line ends, as on the real clipboard) into
//                 the private clipboard (headless only)
//   'GANS' label  press the button `label` of the pending question, or
//                 choose the item `label` ("Edit > Undo"; "Undo" in the
//                 right-click menu; "Cancel") of the pending menu
//                 (headless only)
// Headless menus (decision B7): menu-bar-open (F10) and WM_CONTEXTMENU do
// not show a menu; the menu is pending in the state as a question
// ("menu_bar" / "context_menu") with every item of the built HMENU
// (menu_item lines) and its position (menu_x, menu_y); the menu bar also
// names the submenu that a visible window drops down (menu_open: File).
// A request that is not understood returns 0, is counted in the state
// ("rejected", "last_rejected") and goes to OutputDebugString.
#pragma once

#include <cstddef>
#include <cwchar>
#include <optional>
#include <string>
#include <string_view>

#include <windows.h>

#include "question.h"

namespace geditor::win {

inline constexpr ULONG_PTR kCopyDataState = 0x41545347;          // 'GSTA'
inline constexpr ULONG_PTR kCopyDataClipboardWrite = 0x57424347;  // 'GCBW'
inline constexpr ULONG_PTR kCopyDataAnswer = 0x534E4147;          // 'GANS'

// Payloads larger than this are rejected (a path or a label is far
// smaller; clipboard text of the checks too).
inline constexpr size_t kMaxCopyDataUnits = 16 * 1024 * 1024;

// The UTF-16 text of a WM_COPYDATA payload; nullopt when the structure is
// missing, the size is odd or too large, or the data pointer is null while
// the size is not 0. An empty payload gives an empty text.
inline std::optional<std::wstring> copy_data_text(const COPYDATASTRUCT* data) {
  if (data == nullptr) return std::nullopt;
  if (data->cbData % sizeof(wchar_t) != 0) return std::nullopt;
  const size_t units = data->cbData / sizeof(wchar_t);
  if (units > kMaxCopyDataUnits) return std::nullopt;
  if (units == 0) return std::wstring();
  if (data->lpData == nullptr) return std::nullopt;
  return std::wstring(static_cast<const wchar_t*>(data->lpData), units);
}

// Four letters of a dwData value, for messages ("GSTA"); "0x..." when it
// is not four printable ASCII letters.
inline std::wstring copy_data_kind_text(ULONG_PTR kind) {
  std::wstring text;
  bool printable = kind <= 0xFFFFFFFF;
  for (int i = 0; i < 4 && printable; ++i) {
    const unsigned c = static_cast<unsigned>((kind >> (8 * i)) & 0xFF);
    if (c < 0x20 || c > 0x7E) printable = false;
    text += static_cast<wchar_t>(c);
  }
  if (printable) return text;
  wchar_t hex[24];
  swprintf_s(hex, L"0x%llX", static_cast<unsigned long long>(kind));
  return hex;
}

// "key=value\n" with "\" -> "\\", CR -> "\r" and LF -> "\n" in the value,
// so that every value stays on one line.
inline std::wstring state_line(std::wstring_view key, std::wstring_view value) {
  std::wstring line(key);
  line += L'=';
  for (const wchar_t c : value) {
    if (c == L'\\') {
      line += L"\\\\";
    } else if (c == L'\r') {
      line += L"\\r";
    } else if (c == L'\n') {
      line += L"\\n";
    } else {
      line += c;
    }
  }
  line += L'\n';
  return line;
}

// One menu item for the tools: "path TAB key text TAB flags", the flags
// being "separator", "submenu", "enabled" or "disabled", then "checked" and
// "radio" when they apply, separated by spaces.
inline std::wstring menu_entry_text(const MenuEntry& e) {
  std::wstring flags;
  auto add = [&](const wchar_t* flag) {
    if (!flags.empty()) flags += L' ';
    flags += flag;
  };
  if (e.separator) {
    add(L"separator");
  } else {
    if (e.submenu) add(L"submenu");
    add(e.enabled ? L"enabled" : L"disabled");
    if (e.checked) add(L"checked");
    if (e.radio) add(L"radio");
  }
  return e.path + L"\t" + e.key_text + L"\t" + flags;
}

// The pending question as the tools read it: its kind, the text and the
// button labels exactly as the task dialog shows them, and the default; for
// a menu also its position and every item (menu_x, menu_y, menu_item).
inline std::wstring question_state(const Question& q) {
  std::wstring text = state_line(L"question", question_kind_name(q.kind));
  text += state_line(L"instruction", q.instruction);
  text += state_line(L"content", q.content);
  for (const QuestionButton& b : q.buttons) text += state_line(L"button", b.label);
  for (const QuestionButton& b : q.buttons) {
    if (b.id == q.default_button) text += state_line(L"default", b.label);
  }
  if (q.kind == QuestionKind::kMenuBar || q.kind == QuestionKind::kContextMenu) {
    text += state_line(L"menu_x", std::to_wstring(q.menu_position.x));
    text += state_line(L"menu_y", std::to_wstring(q.menu_position.y));
    if (q.kind == QuestionKind::kMenuBar) text += state_line(L"menu_open", q.menu_open);
    for (const MenuEntry& e : q.menu) text += state_line(L"menu_item", menu_entry_text(e));
  }
  return text;
}

}  // namespace geditor::win
