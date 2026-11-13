// Fitting text into the one-line echo area / minibuffer line, short messages
// and character rules of the window layer. Pure logic (unit-tested).
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace geditor::win {

// Width in cells (East Asian Width; TAB counts as 1 cell here).
int text_cells(std::wstring_view text);

// Shortens `message` to at most `columns` cells by replacing a middle part
// with "…". A message with a path keeps the start of the path and everything
// after its last backslash ("Cannot write C:\…\file.txt: Access is denied"),
// so that the file name and the reason stay readable.
std::wstring fit_message(std::wstring_view message, int columns);

struct MinibufferLine {
  std::wstring text;      // what to draw, starting at column 0
  int cursor_column = 0;  // cell column of the cursor in `text`
};

// The minibuffer line: the prompt stays visible; when the input does not fit,
// its left part is replaced by "…" so that the cursor stays visible. The
// message (such as "[No match]") follows when there is room.
MinibufferLine fit_minibuffer(std::wstring_view prompt, std::wstring_view input, size_t cursor,
                              std::wstring_view message, int columns);

// Most echo area rows in a window of `total_rows` rows: the mode line and at
// least one text row stay visible (at least 1).
int max_echo_rows(int total_rows);

// "" for one file; "Opened only the first of N dropped files" for N > 1.
std::wstring dropped_files_note(unsigned count);

// Invisible format characters that are not drawn at all (no glyph box):
// U+200B..U+200F, U+2060, U+FE00..U+FE0F, U+FEFF.
bool is_invisible_format_char(char32_t c);

}  // namespace geditor::win
