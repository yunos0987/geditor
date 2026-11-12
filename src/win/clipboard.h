// Windows clipboard (CF_UNICODETEXT). The core uses "\n" line ends; the
// clipboard holds CRLF (docs/PLAN.md 2 "writing to the clipboard").
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <windows.h>

#include "host.h"

namespace geditor::win {

// "\n" -> "\r\n". Other characters, surrogate pairs included, are copied.
std::wstring to_clipboard_text(std::wstring_view text);
// Reads at most `max_units` units of `data`, stopping at the first NUL, and
// turns "\r\n" and a lone "\r" into "\n".
std::wstring from_clipboard_text(const wchar_t* data, size_t max_units);

// kNoText when the clipboard holds no text; kFailed when it cannot be opened
// or its data cannot be read.
ClipboardRead read_clipboard(HWND owner, std::wstring& text);
// false when the clipboard cannot be opened or written.
bool write_clipboard(HWND owner, const std::wstring& text);

}  // namespace geditor::win
