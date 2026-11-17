#include "display.h"

#include <algorithm>
#include <cmath>

namespace geditor {
namespace {

constexpr wchar_t kIdeographicSpace = L'　';

}  // namespace

int wrap_width(WrapMode mode, int column, int text_columns) {
  const int window = std::max(1, text_columns - 1);
  switch (mode) {
    case WrapMode::kNone: return 0;
    case WrapMode::kWindow: return window;
    case WrapMode::kFixed: return std::max(1, column);
    case WrapMode::kMax: return std::max(1, std::min(window, column));
  }
  return 0;
}

const wchar_t* wrap_mode_name(WrapMode mode) {
  switch (mode) {
    case WrapMode::kNone: return L"none";
    case WrapMode::kWindow: return L"window";
    case WrapMode::kFixed: return L"fixed";
    case WrapMode::kMax: return L"max";
  }
  return L"none";
}

bool parse_wrap_mode(std::wstring_view name, WrapMode* mode) {
  for (const WrapMode m : {WrapMode::kNone, WrapMode::kWindow, WrapMode::kFixed, WrapMode::kMax}) {
    if (name == wrap_mode_name(m)) {
      *mode = m;
      return true;
    }
  }
  return false;
}

int gutter_columns(size_t line_count, bool line_numbers) {
  if (!line_numbers) return 0;
  int digits = 1;
  for (size_t n = line_count; n >= 10; n /= 10) ++digits;
  return std::max(2, digits) + 1;
}

double text_scale_pixels(double size_pt, int steps, double dpi) {
  const double px = size_pt * dpi / 72.0 * std::pow(kTextScaleStep, steps);
  return std::clamp(px, kMinFontPixels, kMaxFontPixels);
}

std::vector<std::wstring> MissingFontReport::take(const std::wstring& family,
                                                  const std::vector<std::wstring>& fallback,
                                                  const std::vector<std::wstring>& missing) {
  if (known_ && family == family_ && fallback == fallback_) return {};
  known_ = true;
  family_ = family;
  fallback_ = fallback;
  return missing;
}

WhitespaceFlags whitespace_flags(const Config& config) {
  return {config.whitespace_space, config.whitespace_tab, config.whitespace_fullwidth_space,
          config.whitespace_trailing};
}

WhitespaceFlags shown_whitespace_flags(const WhitespaceFlags& file) {
  if (std::any_of(file.begin(), file.end(), [](bool b) { return b; })) return file;
  return {true, true, true, true};
}

EolMark eol_mark_for_line(const PieceTree& tree, size_t line, Eol eol, const DisplayOptions& options) {
  EolMark mark;
  if (!options.eol_marks || line + 1 >= tree.line_count()) return mark;
  mark.shown = true;
  // The line's own line end; eol_other when it differs from the default
  // (decision F8).
  const Eol own = tree.line_eol(line);
  switch (own) {
    case Eol::kLf: mark.symbol = options.eol_lf; break;
    case Eol::kCrLf: mark.symbol = options.eol_crlf; break;
    case Eol::kCr: mark.symbol = options.eol_cr; break;
  }
  mark.color = own == eol ? kColorEol : kColorEolOther;
  return mark;
}

size_t trailing_whitespace_start(const PieceTree& tree, size_t line) {
  const size_t start = tree.line_start(line);
  size_t p = tree.line_end(line);
  while (p > start && is_trailing_whitespace(tree.char_at(p - 1))) --p;
  // On a cluster boundary (as WrapLayout::trailing_start).
  return p > start ? next_cluster_offset(tree, p - 1) : p;
}

void build_display_row(const PieceTree& tree, const WrapLayout& layout, Eol eol,
                       const DisplayOptions& options, RowPos pos, int left, int columns,
                       DisplayRow* out) {
  out->pos = pos;
  out->first_row = pos.row == 0;
  out->cells.clear();
  out->line_end = tree.line_end(pos.line);
  out->has_newline = pos.line + 1 < tree.line_count();
  out->eol = EolMark();

  std::vector<RowChar> chars;
  const RowInfo info = layout.row_chars(tree, pos.line, pos.row, left, left + columns, &chars);
  out->complete = info.complete;
  out->last_row = info.complete && info.last;
  out->end_column = info.end_column - left;

  // From the layout's per-line value: a row does not walk the whitespace
  // tail of its line again (review R2).
  const size_t trailing_from = options.trailing ? layout.trailing_start(tree, pos.line) : out->line_end;
  out->cells.reserve(chars.size());
  for (const RowChar& rc : chars) {
    DisplayCell cell;
    cell.c = rc.c;
    cell.column = rc.column - left;
    cell.width = rc.width;
    cell.offset = rc.offset;
    cell.length = rc.length;
    cell.code_points = rc.code_points;
    cell.emoji = rc.emoji;
    // Whitespace is a cluster of one code point; a space that carries a
    // combining mark is drawn as text.
    const bool single = rc.code_points == 1;
    if (single && rc.c == U' ' && options.space) {
      cell.c = kSpaceMark;
      cell.mark = true;
    } else if (single && rc.c == U'\t' && options.tab) {
      cell.c = kTabMark;
      cell.mark = true;
    } else if (single && rc.c == kIdeographicSpace && options.fullwidth_space) {
      cell.c = kFullwidthSpaceMark;
      cell.mark = true;
    }
    if (cell.mark) cell.code_points = 1;
    cell.trailing = options.trailing && single && rc.offset >= trailing_from && is_trailing_whitespace(rc.c);
    out->cells.push_back(cell);
  }
  if (out->last_row) out->eol = eol_mark_for_line(tree, pos.line, eol, options);
}

}  // namespace geditor
