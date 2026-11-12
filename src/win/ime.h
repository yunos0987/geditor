// IMM32: composition and candidate window placement and the committed text
// (docs/PLAN.md 4.2 / 4.7). The IME draws the composition string itself.
#pragma once

#include <string>

#include <windows.h>

namespace geditor::win {

// Puts the composition window at `caret` (client pixels) with the editor
// font, and the candidate window below a caret `line_height` pixels high.
void place_ime_windows(HWND hwnd, POINT caret, int line_height, const wchar_t* font_family,
                       int font_height);

// Reads the committed string of WM_IME_COMPOSITION (GCS_RESULTSTR).
bool take_ime_result(HWND hwnd, std::wstring& text);

}  // namespace geditor::win
