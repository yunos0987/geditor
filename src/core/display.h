// The display model of docs/PLAN.md 4.5 - 4.7 (M3): wrap width, line-number
// gutter, whitespace and end-of-line marks, text-scale and the view settings
// that View commands change per window. The Win32 renderer draws what these
// functions produce and computes no wrap positions of its own.
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "encoding.h"
#include "layout.h"
#include "piece_tree.h"

namespace geditor {

// Wrap width in cells for `mode` (0: no wrapping) when the text area right of
// the line numbers is `text_columns` cells wide (PLAN 4.5). The window width
// is text_columns - 1: the last cell is kept for the end-of-line mark. `max`
// is the narrower of the window width and `column`; `fixed` is `column` even
// when the window is narrower (decision E3).
int wrap_width(WrapMode mode, int column, int text_columns);
const wchar_t* wrap_mode_name(WrapMode mode);
// "none" / "window" / "fixed" / "max"; false for anything else.
bool parse_wrap_mode(std::wstring_view name, WrapMode* mode);

// Cells of the line-number gutter (decision E2): the digits of the last line
// number (at least 2) and one blank cell; 0 without line numbers.
int gutter_columns(size_t line_count, bool line_numbers);

// text-scale (decision E5): each step multiplies the font size by 1.2; the
// pixel height (size in points at `dpi`) is kept within 6..200 pixels.
inline constexpr double kTextScaleStep = 1.2;
inline constexpr double kMinFontPixels = 6;
inline constexpr double kMaxFontPixels = 200;
double text_scale_pixels(double size_pt, int steps, double dpi);

// Decision E1: a font name that is not installed is reported once per value
// of the settings' font.family / font.fallback. Rebuilds of the font for
// text-scale, a DPI change or font.size report nothing again.
class MissingFontReport {
 public:
  // The names to show for a rebuild with `family` / `fallback` that found
  // `missing` not installed: all of them when family or fallback differ from
  // the last rebuild, none otherwise.
  std::vector<std::wstring> take(const std::wstring& family, const std::vector<std::wstring>& fallback,
                                 const std::vector<std::wstring>& missing);

 private:
  bool known_ = false;
  std::wstring family_;
  std::vector<std::wstring> fallback_;
};

// Whitespace marks (PLAN 4.7).
inline constexpr char32_t kSpaceMark = U'·';           // middle dot
inline constexpr char32_t kTabMark = U'→';             // rightwards arrow
inline constexpr char32_t kFullwidthSpaceMark = U'□';  // white square

// The view settings in effect (settings file values, or a View command's
// value for this window).
struct DisplayOptions {
  bool line_numbers = true;
  bool eol_marks = true;
  bool space = false;
  bool tab = true;
  bool fullwidth_space = true;
  bool trailing = true;
  std::wstring eol_lf = L"↓";
  std::wstring eol_crlf = L"↵";
  std::wstring eol_cr = L"←";
};

// A value that a View command set for this window (decision E6). It holds
// until the settings file's value for the item changes.
template <typename T>
struct ViewSetting {
  bool active = false;
  T value{};
  T file_value{};  // the settings file's value when the command set it

  T get(const T& file) const { return active ? value : file; }
  void set(const T& v, const T& file) {
    active = true;
    value = v;
    file_value = file;
  }
  // After a settings reload.
  void reload(const T& file) {
    if (active && !(file == file_value)) active = false;
  }
};

// The whitespace flags of the settings file (space, tab, fullwidth space,
// trailing) as one item for toggle-whitespace-marks.
using WhitespaceFlags = std::array<bool, 4>;
WhitespaceFlags whitespace_flags(const Config& config);
// The flags shown when whitespace marks are on: the file's flags, or all of
// them when the file enables none.
WhitespaceFlags shown_whitespace_flags(const WhitespaceFlags& file);

// The end-of-line mark of a logical line (PLAN 4.7, M3: files whose lines all
// use the buffer's line end). None on the last line (it has no line end) or
// when marks are off.
struct EolMark {
  bool shown = false;
  std::wstring symbol;
  ColorIndex color = kColorEol;
};
EolMark eol_mark_for_line(const PieceTree& tree, size_t line, Eol eol, const DisplayOptions& options);

// Offset where the trailing whitespace (spaces, TABs, ideographic spaces) of
// `line` starts; the line end when there is none.
size_t trailing_whitespace_start(const PieceTree& tree, size_t line);

// One drawn cell of a row.
struct DisplayCell {
  char32_t c = 0;        // the character, or its whitespace mark when `mark`
  int column = 0;        // relative to the first visible column
  int width = 0;         // cells (0 for zero-width characters)
  size_t offset = 0;
  bool mark = false;     // c is a whitespace mark (colour "whitespace")
  bool trailing = false; // trailing whitespace ("whitespace" as background)
};

// One screen row as the renderer draws it.
struct DisplayRow {
  RowPos pos;
  bool first_row = false;  // the line number is shown on it
  bool complete = false;   // the cells reach the end of the row
  bool last_row = false;   // valid when complete
  int end_column = 0;      // valid when complete: relative to the first visible column
  size_t line_end = 0;     // offset of the line's end (its '\n' or the buffer end)
  bool has_newline = false;
  std::vector<DisplayCell> cells;
  EolMark eol;             // shown on the last row, at end_column
};

// Builds row `pos` for the visible columns [left, left + columns).
void build_display_row(const PieceTree& tree, const WrapLayout& layout, Eol eol,
                       const DisplayOptions& options, RowPos pos, int left, int columns,
                       DisplayRow* out);

// Vertical scroll bar (PLAN 4.7): positions are logical lines, an
// approximation when lines wrap. Range 0..max with a page of `page`; the
// thumb is at `pos` (the top line).
struct ScrollBarState {
  size_t max = 0;
  size_t page = 1;
  size_t pos = 0;
};

}  // namespace geditor
