// Parts of the headless mode (--headless, docs/PLAN.md 2 "headless") that
// need no window: the WM_COPYDATA requests of the tools (tools/app_driver.py)
// and the state text that they read back from a file.
//
// Requests (dwData: four ASCII letters, little-endian; payload UTF-16):
//   'GCAP' path   draw the current frame into a BMP file (any mode)
//   'GFLT' name   make the next "target" / "font" creation fail (any mode),
//                 or the next "clipboard-read" / "clipboard-write" of the
//                 private clipboard fail (headless only)
//   'GSTA' path   write the state (state_line lines, UTF-16LE) to a file
//   'GCBW' text   put `text` (CRLF line ends, as on the real clipboard) into
//                 the private clipboard (headless only)
//   'GANS' label  press the button `label` of the pending question
//                 (headless only)
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

// The pending question as the tools read it: its kind, the text and the
// button labels exactly as the task dialog shows them, and the default.
inline std::wstring question_state(const Question& q) {
  std::wstring text = state_line(L"question", question_kind_name(q.kind));
  text += state_line(L"instruction", q.instruction);
  text += state_line(L"content", q.content);
  for (const QuestionButton& b : q.buttons) text += state_line(L"button", b.label);
  for (const QuestionButton& b : q.buttons) {
    if (b.id == q.default_button) text += state_line(L"default", b.label);
  }
  return text;
}

}  // namespace geditor::win
