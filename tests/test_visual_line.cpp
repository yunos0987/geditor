// Vertical motion and scrolling by screen rows in wrapped text (PLAN 2
// "vertical motion in wrapped lines", decision E4; packet [C3]).

#include "fake_host.h"

using test::Session;

namespace {

// A session whose window wraps at 10 cells (no line numbers, wrap "window":
// 11 cells across, the last one reserved).
void wrap_at_10(Session& s) {
  s.host.columns = 11;
  s.settings("{\"line_numbers\": false, \"wrap\": {\"mode\": \"window\"}}");
  CHECK_EQ(s.ed.wrap_width(), 10);
}

// 23 characters: rows "0123456789" "abcdefghij" "XYZ", then "short".
const wchar_t kWrapped[] = L"0123456789abcdefghijXYZ\nshort";

}  // namespace

TEST(visual_next_line_moves_through_the_rows_of_a_wrapped_line) {
  Session s;
  wrap_at_10(s);
  s.load(kWrapped);
  s.set_point(3);
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{13});  // row 1, cell 3
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{23});  // row 2 has 3 cells: its end
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{27});  // next logical line, goal cell 3
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{29});  // last row: end of buffer
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
}

TEST(visual_previous_line_moves_through_the_rows_of_a_wrapped_line) {
  Session s;
  wrap_at_10(s);
  s.load(kWrapped);
  s.set_point(27);  // "sho|rt"
  s.keys(L"C-p");
  CHECK_EQ(s.point(), size_t{23});  // the short last row of line 1
  s.keys(L"<up>");
  CHECK_EQ(s.point(), size_t{13});
  s.keys(L"C-p");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"C-p");
  CHECK_EQ(s.point(), size_t{0});
  CHECK_EQ(s.message(), std::wstring(L"Beginning of buffer"));
}

TEST(visual_goal_column_is_kept_in_cells_across_rows) {
  Session s;
  wrap_at_10(s);
  // Row 0: "abcdefgh" + "あ" (cells 8-9); row 1: "いうえおか"
  // (10 cells); then "xy".
  s.load(L"abcdefghあいうえおか\nxy");
  s.set_point(3);  // cell 3
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{10});  // cell 3 is inside the 2nd wide char (cells 2-3)
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{17});  // "xy": goal 3 is past its end
  s.keys(L"C-p C-p");
  CHECK_EQ(s.point(), size_t{3});   // the goal survived the wide characters
  // A goal on a wide character's first cell.
  s.set_point(9);  // the 1st wide char of row 1 (cell 0)
  s.keys(L"C-p");
  CHECK_EQ(s.point(), size_t{0});
  s.set_point(8);  // "あ" at cells 8-9 of row 0
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{13});  // cells 8-9 of row 1
}

TEST(visual_prefix_count_moves_screen_rows) {
  Session s;
  wrap_at_10(s);
  s.load(kWrapped);
  s.set_point(2);
  s.keys(L"C-u 3 C-n");  // E4: rows, not logical lines
  CHECK_EQ(s.point(), size_t{26});
  s.keys(L"C-u 2 C-p");
  CHECK_EQ(s.point(), size_t{12});
  s.keys(L"M-- C-n");  // a negative count goes the other way
  CHECK_EQ(s.point(), size_t{2});
}

TEST(visual_beginning_and_end_of_line_use_the_logical_line) {
  Session s;
  wrap_at_10(s);
  s.load(kWrapped);
  s.set_point(15);  // row 1
  s.keys(L"C-a");
  CHECK_EQ(s.point(), size_t{0});
  s.keys(L"C-e");
  CHECK_EQ(s.point(), size_t{23});
}

TEST(visual_kill_line_kills_to_the_logical_line_end) {
  Session s;
  wrap_at_10(s);
  s.load(kWrapped);
  s.set_point(5);  // row 0
  s.keys(L"C-k");
  CHECK_EQ(s.text(), std::wstring(L"01234\nshort"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"56789abcdefghijXYZ"));
}

TEST(visual_scroll_commands_move_by_rows_and_keep_point_visible) {
  Session s;
  wrap_at_10(s);
  s.host.lines = 4;
  // One logical line of 10 rows, then 3 short lines: 13 rows.
  s.load(std::wstring(100, L'x') + L"\na\nb\nc");
  CHECK_EQ(s.ed.layout().row_count(s.ed.buffer().text, 0), size_t{10});
  s.keys(L"C-v");  // visible - 2 = 2 rows
  CHECK(s.ed.top() == (geditor::RowPos{0, 2}));
  CHECK_EQ(s.point(), size_t{20});  // point left the window: start of the top row
  s.keys(L"C-u 5 C-v");
  CHECK(s.ed.top() == (geditor::RowPos{0, 7}));
  s.keys(L"C-v C-v");
  // Stops where the last row is on the bottom row: rows 9..12 = (0,9)..(3,0).
  CHECK(s.ed.top() == (geditor::RowPos{0, 9}));
  s.keys(L"C-v");
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
  s.keys(L"M-v");
  CHECK(s.ed.top() == (geditor::RowPos{0, 7}));
  // Point stays on screen: below the window it moves to the bottom row.
  s.keys(L"M->");
  CHECK(s.ed.top() == (geditor::RowPos{0, 9}));
  s.keys(L"C-u 3 M-v");
  CHECK(s.ed.top() == (geditor::RowPos{0, 6}));
  const geditor::Editor::PointView v = s.ed.point_view();
  CHECK(v.pos == (geditor::RowPos{0, 9}));
  CHECK_EQ(s.point(), size_t{90});
}

TEST(visual_point_below_the_window_scrolls_by_rows) {
  Session s;
  wrap_at_10(s);
  s.host.lines = 3;
  s.load(std::wstring(100, L'x'));
  s.keys(L"C-n C-n C-n");  // row 3: the window scrolls by one row
  CHECK(s.ed.top() == (geditor::RowPos{0, 1}));
  s.keys(L"M->");
  CHECK(s.ed.top() == (geditor::RowPos{0, 7}));  // rows 7..9
  s.keys(L"C-l");  // recenter: by rows too
  CHECK(s.ed.top() == (geditor::RowPos{0, 8}));
}

TEST(visual_recenter_counts_rows) {
  Session s;
  wrap_at_10(s);
  s.host.lines = 5;
  s.load(std::wstring(200, L'x'));
  s.set_point(105);  // row 10
  s.keys(L"C-l");
  CHECK(s.ed.top() == (geditor::RowPos{0, 8}));   // center: 2 rows above
  s.keys(L"C-l");
  CHECK(s.ed.top() == (geditor::RowPos{0, 10}));  // top
  s.keys(L"C-l");
  CHECK(s.ed.top() == (geditor::RowPos{0, 6}));   // bottom
}

TEST(visual_wrap_none_keeps_logical_line_motion) {
  Session s;
  s.host.columns = 11;
  s.settings("{\"line_numbers\": false, \"wrap\": {\"mode\": \"none\"}}");
  CHECK_EQ(s.ed.wrap_width(), 0);
  s.load(kWrapped);
  s.set_point(15);
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{29});  // the next logical line, clamped to its end
  s.keys(L"C-p");
  CHECK_EQ(s.point(), size_t{15});  // goal column 15 in cells, as in M1
}

TEST(visual_fixed_wider_than_the_window) {
  // E3: "fixed" wraps at its column even when the window is narrower.
  Session s;
  s.host.columns = 11;
  s.settings("{\"line_numbers\": false, \"wrap\": {\"mode\": \"fixed\", \"column\": 15}}");
  CHECK_EQ(s.ed.wrap_width(), 15);
  s.load(std::wstring(40, L'x'));
  CHECK_EQ(s.ed.layout().row_count(s.ed.buffer().text, 0), size_t{3});
  s.set_point(12);
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{27});
}

TEST(visual_max_takes_the_narrower_width) {
  Session s;
  s.settings("{\"line_numbers\": false, \"wrap\": {\"mode\": \"max\", \"column\": 20}}");
  s.host.columns = 11;
  CHECK_EQ(s.ed.wrap_width(), 10);  // the window is narrower
  s.host.columns = 41;
  CHECK_EQ(s.ed.wrap_width(), 20);  // the column is narrower
  // Line numbers take cells from the window (E2: 2 digits + 1 blank).
  s.settings("{\"wrap\": {\"mode\": \"window\"}}");
  CHECK_EQ(s.ed.gutter_columns(), 3);
  CHECK_EQ(s.ed.wrap_width(), 37);
}
