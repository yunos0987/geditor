// Wrap layout of the 4 wrap modes ([M3-1], packet [C2]): break positions and
// position <-> (row, cell) in both directions.

#include <string>
#include <vector>

#include "char_width.h"
#include "display.h"
#include "layout.h"
#include "piece_tree.h"
#include "test_harness.h"

using geditor::PieceTree;
using geditor::RowPos;
using geditor::WrapLayout;
using geditor::WrapMode;

namespace {

WrapLayout make_layout(int width, int tab_width = 4) {
  WrapLayout l;
  l.configure(width, tab_width);
  return l;
}

// Start offsets of every row of line 0.
std::vector<size_t> row_starts(const PieceTree& t, const WrapLayout& l, size_t line = 0) {
  std::vector<size_t> starts;
  const size_t rows = l.row_count(t, line);
  for (size_t r = 0; r < rows; ++r) starts.push_back(l.row_start(t, line, r));
  return starts;
}

bool same(const std::vector<size_t>& a, const std::vector<size_t>& b) {
  return a == b;
}

// row_of for `offset`: "row:column".
std::string where(const PieceTree& t, const WrapLayout& l, size_t offset) {
  int column = -1;
  const RowPos p = l.row_of(t, offset, &column);
  return std::to_string(p.line) + ":" + std::to_string(p.row) + ":" + std::to_string(column);
}

// Every character start round-trips: offset -> (row, cell) -> offset.
bool round_trips(const PieceTree& t, const WrapLayout& l) {
  const size_t n = t.length();
  for (size_t off = 0; off <= n; off = geditor::next_char_offset(t, off)) {
    int column = 0;
    const RowPos p = l.row_of(t, off, &column);
    // Zero-width characters share the cell of the position after them.
    const char32_t c = geditor::code_point_at(t, off);
    const bool zero_width = off < n && c != U'\n' && c != U'\t' && geditor::char_width(c) == 0;
    if (!zero_width && l.offset_at(t, p.line, p.row, column) != off) return false;
    if (off == n) break;
  }
  return true;
}

}  // namespace

TEST(wrap_width_of_the_four_modes) {
  // text_columns 81: the window width is 80 (the last cell is reserved).
  CHECK_EQ(geditor::wrap_width(WrapMode::kNone, 80, 81), 0);
  CHECK_EQ(geditor::wrap_width(WrapMode::kWindow, 80, 81), 80);
  CHECK_EQ(geditor::wrap_width(WrapMode::kFixed, 100, 81), 100);  // E3: wider than the window
  CHECK_EQ(geditor::wrap_width(WrapMode::kFixed, 40, 81), 40);
  // max = min(window, column), both ways round.
  CHECK_EQ(geditor::wrap_width(WrapMode::kMax, 80, 61), 60);
  CHECK_EQ(geditor::wrap_width(WrapMode::kMax, 50, 81), 50);
  CHECK_EQ(geditor::wrap_width(WrapMode::kMax, 80, 81), 80);
  // A tiny window still wraps at 1 cell.
  CHECK_EQ(geditor::wrap_width(WrapMode::kWindow, 80, 1), 1);
}

TEST(wrap_ascii_break_positions) {
  const PieceTree t(L"abcdefghij");
  const WrapLayout l = make_layout(4);
  CHECK_EQ(l.row_count(t, 0), size_t{3});
  CHECK(same(row_starts(t, l), {0, 4, 8}));
  CHECK_EQ(where(t, l, 0), std::string("0:0:0"));
  CHECK_EQ(where(t, l, 3), std::string("0:0:3"));
  CHECK_EQ(where(t, l, 4), std::string("0:1:0"));  // a row's first character starts that row
  CHECK_EQ(where(t, l, 10), std::string("0:2:2"));  // line end
  CHECK_EQ(l.offset_at(t, 0, 1, 2), size_t{6});
  CHECK_EQ(l.offset_at(t, 0, 2, 9), size_t{10});  // past the end of the last row: line end
  // Past the end of a row that continues: its last character.
  const PieceTree u(L"abcdefgh");
  const WrapLayout l8 = make_layout(5);
  CHECK_EQ(l8.offset_at(u, 0, 0, 7), size_t{4});
  CHECK(round_trips(t, l));
}

TEST(wrap_line_exactly_as_wide_as_the_width) {
  const PieceTree t(L"abcd\nabcde");
  const WrapLayout l = make_layout(4);
  CHECK_EQ(l.row_count(t, 0), size_t{1});
  // The end of a full row is on that row, in the reserved cell.
  CHECK_EQ(where(t, l, 4), std::string("0:0:4"));
  CHECK_EQ(l.row_count(t, 1), size_t{2});
  CHECK(same(row_starts(t, l, 1), {5, 9}));
  std::vector<geditor::RowChar> chars;
  const geditor::RowInfo info = l.row_chars(t, 0, 0, 0, 100, &chars);
  CHECK(info.complete && info.last);
  CHECK_EQ(info.end_column, 4);
  CHECK(round_trips(t, l));
}

TEST(wrap_full_width_characters) {
  // 2-cell characters: "あいう" in 6 cells is one row; in 5 cells the third
  // one would leave 1 cell and goes to the next row.
  const PieceTree t(L"あいう");
  CHECK_EQ(make_layout(6).row_count(t, 0), size_t{1});
  const WrapLayout l = make_layout(5);
  CHECK(same(row_starts(t, l), {0, 2}));
  CHECK_EQ(where(t, l, 1), std::string("0:0:2"));
  CHECK_EQ(where(t, l, 2), std::string("0:1:0"));
  std::vector<geditor::RowChar> chars;
  const geditor::RowInfo info = l.row_chars(t, 0, 0, 0, 100, &chars);
  CHECK(info.complete && !info.last);
  CHECK_EQ(info.end_column, 4);  // the row ends 1 cell short
  // The goal column inside a wide character selects that character.
  CHECK_EQ(l.offset_at(t, 0, 0, 3), size_t{1});
  CHECK(round_trips(t, l));
}

TEST(wrap_mixed_ascii_and_full_width) {
  const PieceTree t(L"ab日本cd語");
  const WrapLayout l = make_layout(5);
  // a b 日 (cells 0-3), 本 needs cells 4-5: next row; 本 c d (0-3), 語 next.
  CHECK(same(row_starts(t, l), {0, 3, 6}));
  CHECK_EQ(where(t, l, 3), std::string("0:1:0"));
  CHECK_EQ(where(t, l, 5), std::string("0:1:3"));
  CHECK(round_trips(t, l));
}

TEST(wrap_tab_stops_and_cut_tabs) {
  // Width 6, tab width 4: "ab" + TAB (to 4) + "cd" fills the row; the next
  // TAB starts the next row (the row is full) and spans 4 cells there.
  const PieceTree t(L"ab\tcd\tx");
  const WrapLayout l = make_layout(6, 4);
  CHECK(same(row_starts(t, l), {0, 5}));
  CHECK_EQ(where(t, l, 3), std::string("0:0:4"));
  CHECK_EQ(where(t, l, 6), std::string("0:1:4"));
  // A TAB with fewer cells left than its stop is cut to the row end and does
  // not start a row of its own.
  const PieceTree u(L"abcde\tf");
  CHECK(same(row_starts(u, l), {0, 6}));
  CHECK_EQ(where(u, l, 6), std::string("0:1:0"));
  CHECK(round_trips(t, l));
  CHECK(round_trips(u, l));
}

TEST(wrap_zero_width_characters_stay_on_their_row) {
  // U+0301 after a full row stays on that row; 'd' starts the next one.
  const PieceTree t(L"abćd");
  const WrapLayout l = make_layout(3);
  CHECK(same(row_starts(t, l), {0, 4}));
  CHECK_EQ(where(t, l, 3), std::string("0:0:3"));
  CHECK_EQ(where(t, l, 4), std::string("0:1:0"));
  // The goal column passes over a zero-width character at it.
  const PieceTree u(L"ábc");
  CHECK_EQ(make_layout(10).offset_at(u, 0, 0, 1), size_t{2});
  CHECK(round_trips(t, l));
}

TEST(wrap_surrogate_pairs_are_one_character) {
  // U+1F600 is 2 cells wide: in 2 cells it cannot follow 'a'.
  const PieceTree t(L"a\U0001F600b");
  const WrapLayout l = make_layout(2);
  CHECK(same(row_starts(t, l), {0, 1, 3}));
  CHECK_EQ(where(t, l, 1), std::string("0:1:0"));
  CHECK_EQ(where(t, l, 3), std::string("0:2:0"));
  CHECK_EQ(l.offset_at(t, 0, 1, 1), size_t{1});  // never the low surrogate
  CHECK(round_trips(t, l));
}

TEST(wrap_character_wider_than_the_width_gets_its_own_row) {
  const PieceTree t(L"あいa");
  const WrapLayout l = make_layout(1);
  CHECK(same(row_starts(t, l), {0, 1, 2}));
}

TEST(wrap_empty_line) {
  const PieceTree t(L"abc\n\nxyz");
  const WrapLayout l = make_layout(2);
  CHECK_EQ(l.row_count(t, 1), size_t{1});
  CHECK_EQ(where(t, l, 4), std::string("1:0:0"));
  CHECK_EQ(l.offset_at(t, 1, 0, 5), size_t{4});
  std::vector<geditor::RowChar> chars;
  const geditor::RowInfo info = l.row_chars(t, 1, 0, 0, 10, &chars);
  CHECK(info.complete && info.last && chars.empty());
  CHECK_EQ(info.end_column, 0);
  // Empty buffer.
  const PieceTree e;
  CHECK_EQ(l.row_count(e, 0), size_t{1});
  CHECK_EQ(where(e, l, 0), std::string("0:0:0"));
}

TEST(wrap_none_is_one_row_per_line) {
  const PieceTree t(std::wstring(500, L'x') + L"\nあ\tb");
  const WrapLayout l = make_layout(0);
  CHECK_EQ(l.row_count(t, 0), size_t{1});
  CHECK_EQ(where(t, l, 300), std::string("0:0:300"));
  // Tab stops count from the line start; the logical column too.
  int64_t logical = 0;
  int column = 0;
  l.row_of(t, 503, &column, &logical);
  CHECK_EQ(column, 4);
  CHECK_EQ(logical, int64_t{4});
  CHECK_EQ(l.offset_at(t, 1, 0, 3), size_t{502});  // inside the TAB: the TAB
  CHECK(round_trips(t, l));
}

TEST(wrap_max_and_fixed_through_the_editor_window) {
  // The window width comes from the host; see test_visual_line.cpp for the
  // editor. Here: max picks the narrower of window and column, fixed keeps
  // its column in a narrower window (E3), both on the same text.
  const PieceTree t(std::wstring(50, L'x'));
  const WrapLayout fixed = make_layout(geditor::wrap_width(WrapMode::kFixed, 40, 31));
  CHECK(same(row_starts(t, fixed), {0, 40}));
  const WrapLayout max = make_layout(geditor::wrap_width(WrapMode::kMax, 40, 31));
  CHECK(same(row_starts(t, max), {0, 30}));
  const WrapLayout window = make_layout(geditor::wrap_width(WrapMode::kWindow, 40, 21));
  CHECK(same(row_starts(t, window), {0, 20, 40}));
}

TEST(wrap_row_chars_window_of_columns) {
  // Only cells reaching into [from, to) are returned; a wide character that
  // straddles `from` is included.
  const PieceTree t(L"abあcd");
  const WrapLayout l = make_layout(0);
  std::vector<geditor::RowChar> chars;
  const geditor::RowInfo info = l.row_chars(t, 0, 0, 3, 5, &chars);
  CHECK(!info.complete);
  CHECK_EQ(chars.size(), size_t{2});
  if (chars.size() == 2) {
    CHECK_EQ(chars[0].offset, size_t{2});
    CHECK_EQ(chars[0].column, 2);
    CHECK_EQ(chars[1].offset, size_t{3});
  }
}

TEST(wrap_next_previous_and_counted_rows) {
  const PieceTree t(L"abcdefghij\nxy\nlmnopq");
  const WrapLayout l = make_layout(4);
  RowPos p{0, 2};
  CHECK(l.next_row(t, &p));
  CHECK(p == (RowPos{1, 0}));
  CHECK(l.previous_row(t, &p));
  CHECK(p == (RowPos{0, 2}));
  RowPos q{0, 0};
  CHECK_EQ(l.forward_rows(t, &q, 5), size_t{5});
  CHECK(q == (RowPos{2, 1}));
  CHECK_EQ(l.forward_rows(t, &q, 5), size_t{0});
  CHECK_EQ(l.backward_rows(t, &q, 3), size_t{3});
  CHECK(q == (RowPos{0, 2}));
  CHECK(l.last_row(t) == (RowPos{2, 1}));
}

TEST(wrap_edit_keeps_layout_consistent) {
  PieceTree t(L"abcdefgh");
  WrapLayout l = make_layout(3);
  CHECK_EQ(l.row_count(t, 0), size_t{3});
  t.insert(1, L"XY");
  l.note_edit(t, 1);
  CHECK(same(row_starts(t, l), {0, 3, 6, 9}));
  // A change that the layout was not told about is noticed too.
  t.erase(0, 4);
  CHECK(same(row_starts(t, l), {0, 3}));
}
