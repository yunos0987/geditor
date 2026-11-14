// The mouse of M3b (decision B5) in the core: positions through WrapLayout
// (Editor::offset_at_screen), press / drag / double-click / Shift+click, the
// wheel accumulator, the undo group a click ends, and the cases that do
// nothing ([C5]).

#include <string>

#include "commands.h"
#include "fake_host.h"
#include "mouse.h"

using geditor::WheelAccumulator;
using test::Session;

namespace {

// A session whose text area wraps at 10 cells (wrap "fixed", column 10).
void wrap_at_10(Session& s) {
  s.settings(R"({"wrap": {"mode": "fixed", "column": 10}})");
  CHECK_EQ(s.ed.wrap_width(), 10);
}

void press(Session& s, long long row, int column, bool shift = false) {
  CHECK(s.ed.mouse_press(s.ed.offset_at_screen(row, column), shift));
}

void drag(Session& s, long long row, int column) {
  CHECK(s.ed.mouse_drag(s.ed.offset_at_screen(row, column)));
}

}  // namespace

// ---- positions ----

TEST(mouse_click_on_ascii_moves_point_before_the_character) {
  Session s;
  s.load(L"hello\nworld\n");
  press(s, 1, 2);
  CHECK_EQ(s.point(), size_t{8});  // before the 'r' of "world"
  press(s, 0, 0);
  CHECK_EQ(s.point(), size_t{0});
}

TEST(mouse_click_on_either_half_of_a_full_width_character) {
  Session s;
  s.load(L"あいう\n");
  // 'い' covers cells 2 and 3: both halves put point before it.
  CHECK_EQ(s.ed.offset_at_screen(0, 2), size_t{1});
  CHECK_EQ(s.ed.offset_at_screen(0, 3), size_t{1});
  CHECK_EQ(s.ed.offset_at_screen(0, 1), size_t{0});
  press(s, 0, 3);
  CHECK_EQ(s.point(), size_t{1});
}

TEST(mouse_click_past_the_line_end_goes_to_the_row_end) {
  Session s;
  s.load(L"hello\nworld\n");
  press(s, 0, 50);
  CHECK_EQ(s.point(), size_t{5});
  press(s, 1, 6);
  CHECK_EQ(s.point(), size_t{11});
}

TEST(mouse_click_in_the_reserved_last_cell) {
  Session s;
  s.load(L"0123456789\nabcdefghijKLM\n");
  wrap_at_10(s);
  // Line 0 fills its row exactly: the reserved cell (column 10) is its end.
  press(s, 0, 10);
  CHECK_EQ(s.point(), size_t{10});
  // Line 1 continues on the next row: the reserved cell of its first row
  // is that row's end, before its last character (the offset after it is
  // the next row's start).
  press(s, 1, 10);
  CHECK_EQ(s.point(), size_t{20});
  CHECK_EQ(s.ed.point_view().pos.row, size_t{0});
}

TEST(mouse_click_on_a_continuation_row) {
  Session s;
  s.load(L"0123456789abcdefghij\nx");
  wrap_at_10(s);
  press(s, 1, 3);
  CHECK_EQ(s.point(), size_t{13});
  CHECK_EQ(s.ed.point_view().pos.row, size_t{1});
  // Past the end of the last row of the line: the line end.
  press(s, 1, 10);
  CHECK_EQ(s.point(), size_t{20});
}

TEST(mouse_click_in_the_gutter_goes_to_the_row_start) {
  Session s;
  s.load(L"0123456789abcdefghij\nxyz");
  wrap_at_10(s);
  press(s, 1, -1);
  CHECK_EQ(s.point(), size_t{10});
  press(s, 2, -1);
  CHECK_EQ(s.point(), size_t{21});
}

TEST(mouse_click_below_the_last_line_goes_to_the_buffer_end) {
  Session s;
  s.load(L"one\ntwo");
  press(s, 5, 0);
  CHECK_EQ(s.point(), size_t{7});
  press(s, 0, 0);
  press(s, 2, 1);
  CHECK_EQ(s.point(), size_t{7});
}

TEST(mouse_click_in_the_strip_below_the_last_row_stays_on_the_last_row) {
  // R1: a client height that is no multiple of the line height leaves a
  // blank strip under the last whole row; the window's hit test gives it
  // the row after the last one. A click there must not scroll.
  Session s;
  std::wstring text;
  for (int i = 0; i < 40; ++i) text += L"line" + std::to_wstring(i) + L"\n";
  s.load(text);
  const long long rows = s.host.lines;
  const size_t last_row_start = s.ed.buffer().text.line_start(static_cast<size_t>(rows) - 1);
  CHECK_EQ(s.ed.offset_at_click(rows, 2), last_row_start + 2);
  CHECK_EQ(s.ed.offset_at_click(rows + 5, 2), last_row_start + 2);
  CHECK_EQ(s.ed.offset_at_click(-1, 1), size_t{1});
  CHECK(s.ed.mouse_press(s.ed.offset_at_click(rows, 2), false));
  CHECK_EQ(s.ed.top().line, size_t{0});
  CHECK_EQ(s.point(), last_row_start + 2);
  CHECK(s.ed.mouse_double_click(s.ed.offset_at_click(rows, 1)));
  CHECK_EQ(s.ed.top().line, size_t{0});
  CHECK_EQ(s.ed.buffer().mark, last_row_start);
  s.keys(L"C-g");  // no active mark: Shift+click extends from point
  s.set_point(0);
  CHECK(s.ed.mouse_press(s.ed.offset_at_click(rows, 3), true));
  CHECK_EQ(s.ed.top().line, size_t{0});
  CHECK_EQ(s.ed.buffer().mark, size_t{0});
  CHECK_EQ(s.point(), last_row_start + 3);
  // Rows past the end of a short buffer within the window: still its end.
  Session t;
  t.load(L"one\ntwo");
  CHECK_EQ(t.ed.offset_at_click(rows, 0), size_t{7});
}

TEST(mouse_positions_follow_the_scrolled_view) {
  Session s;
  std::wstring text;
  for (int i = 0; i < 30; ++i) text += L"line" + std::to_wstring(i) + L"\n";
  s.load(text);
  s.ed.scroll_by_rows(5);
  CHECK_EQ(s.ed.top().line, size_t{5});
  press(s, 0, 0);
  CHECK_EQ(s.point(), s.ed.buffer().text.line_start(5));
  press(s, 2, 4);
  CHECK_EQ(s.point(), s.ed.buffer().text.line_start(7) + 4);
}

// ---- press, drag, double-click, Shift+click ----

TEST(mouse_press_deactivates_the_mark) {
  Session s;
  s.load(L"hello world");
  s.keys(L"C-x h");
  CHECK(s.ed.buffer().mark_active);
  press(s, 0, 3);
  CHECK(!s.ed.buffer().mark_active);
  CHECK_EQ(s.point(), size_t{3});
}

TEST(mouse_drag_selects_across_rows) {
  Session s;
  s.load(L"hello\nworld\nthird\n");
  press(s, 0, 2);
  CHECK(!s.ed.buffer().mark_active);
  drag(s, 1, 3);
  CHECK(s.ed.buffer().mark_active);
  CHECK_EQ(s.ed.buffer().mark, size_t{2});
  CHECK_EQ(s.point(), size_t{9});
  drag(s, 2, 1);
  CHECK_EQ(s.ed.buffer().mark, size_t{2});
  CHECK_EQ(s.point(), size_t{13});
  // Back up across the press position: the region reverses.
  drag(s, 0, 0);
  CHECK_EQ(s.point(), size_t{0});
  CHECK_EQ(s.ed.buffer().mark, size_t{2});
  s.ed.mouse_release();
  CHECK(!s.ed.mouse_drag(5));
  s.keys(L"C-w");
  CHECK_EQ(s.text(), std::wstring(L"llo\nworld\nthird\n"));
}

TEST(mouse_press_without_moving_activates_no_region) {
  Session s;
  s.load(L"hello");
  press(s, 0, 2);
  drag(s, 0, 2);
  CHECK(!s.ed.buffer().mark_active);
}

TEST(mouse_drag_below_the_window_scrolls_with_the_view) {
  // What the window's drag timer does: scroll one row, then drag to the
  // bottom row.
  Session s;
  std::wstring text;
  for (int i = 0; i < 40; ++i) text += L"row" + std::to_wstring(i) + L"\n";
  s.load(text);
  press(s, 0, 1);
  for (int i = 0; i < 5; ++i) {
    s.ed.scroll_by_rows(1);
    drag(s, s.host.lines - 1, 0);
  }
  CHECK_EQ(s.ed.top().line, size_t{5});
  CHECK_EQ(s.ed.buffer().mark, size_t{1});
  CHECK_EQ(s.point(), s.ed.buffer().text.line_start(5 + static_cast<size_t>(s.host.lines) - 1));
  CHECK(s.ed.buffer().mark_active);
  s.ed.mouse_release();
}

TEST(mouse_drag_from_inside_the_selection_starts_a_new_one) {
  Session s;
  s.load(L"abcdef\nghijkl\n");
  s.keys(L"C-x h");
  press(s, 0, 2);
  drag(s, 1, 2);
  CHECK_EQ(s.ed.buffer().mark, size_t{2});
  CHECK_EQ(s.point(), size_t{9});
  CHECK(s.ed.buffer().mark_active);
  // No text moved or copied.
  CHECK_EQ(s.text(), std::wstring(L"abcdef\nghijkl\n"));
  CHECK(!s.modified());
}

TEST(mouse_double_click_selects_the_word) {
  Session s;
  s.load(L"hello world, 漢字かな\n");
  CHECK(s.ed.mouse_double_click(s.ed.offset_at_screen(0, 8)));
  CHECK_EQ(s.ed.buffer().mark, size_t{6});
  CHECK_EQ(s.point(), size_t{11});
  CHECK(s.ed.buffer().mark_active);
  // The same words as M-f / M-b: kanji and kana are separate words.
  CHECK(s.ed.mouse_double_click(s.ed.offset_at_screen(0, 13)));
  CHECK_EQ(s.ed.buffer().mark, size_t{13});
  CHECK_EQ(s.point(), size_t{15});
  s.set_point(13);
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{15});
}

TEST(mouse_double_click_on_a_non_word_character_selects_it) {
  Session s;
  s.load(L"a + b\n");
  CHECK(s.ed.mouse_double_click(s.ed.offset_at_screen(0, 2)));
  CHECK_EQ(s.ed.buffer().mark, size_t{2});
  CHECK_EQ(s.point(), size_t{3});
  CHECK(s.ed.buffer().mark_active);
  size_t from = 0;
  size_t to = 0;
  geditor::word_bounds_at(s.ed.buffer().text, 6, &from, &to);
  CHECK_EQ(from, size_t{6});
  CHECK_EQ(to, size_t{6});
}

TEST(mouse_shift_click_extends_from_the_active_mark) {
  Session s;
  s.load(L"0123456789");
  s.set_point(2);
  s.keys(L"C-SPC");
  s.set_point(4);
  press(s, 0, 7, true);
  CHECK_EQ(s.ed.buffer().mark, size_t{2});
  CHECK_EQ(s.point(), size_t{7});
  CHECK(s.ed.buffer().mark_active);
  // A drag after Shift+click keeps extending from the same mark.
  drag(s, 0, 9);
  CHECK_EQ(s.ed.buffer().mark, size_t{2});
  CHECK_EQ(s.point(), size_t{9});
}

TEST(mouse_shift_click_without_a_mark_starts_at_the_old_point) {
  Session s;
  s.load(L"0123456789");
  s.set_point(3);
  CHECK(!s.ed.buffer().mark_active);
  press(s, 0, 8, true);
  CHECK_EQ(s.ed.buffer().mark, size_t{3});
  CHECK_EQ(s.point(), size_t{8});
  CHECK(s.ed.buffer().mark_active);
  s.keys(L"M-w");
  CHECK_EQ(s.host.clipboard, std::wstring(L"34567"));
}

// ---- undo, prefix keys, and the cases that do nothing ----

TEST(mouse_click_ends_the_typing_undo_group) {
  Session s;
  s.load(L"");
  s.type(L"ab");
  press(s, 0, 0);
  s.type(L"cd");
  CHECK_EQ(s.text(), std::wstring(L"cdab"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L""));
}

TEST(mouse_click_ends_pending_prefix_keys_and_the_message) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-x");
  CHECK(!s.ed.pending_keys_text().empty());
  s.ed.set_message(L"something");
  press(s, 0, 1);
  CHECK(s.ed.pending_keys_text().empty());
  CHECK_EQ(s.message(), std::wstring(L""));
  s.keys(L"C-f");
  CHECK_EQ(s.point(), size_t{2});
}

TEST(mouse_does_nothing_while_the_minibuffer_is_active) {
  Session s;
  s.load(L"hello\nworld");
  s.set_point(1);
  s.keys(L"M-x");
  CHECK(s.ed.minibuffer_active());
  CHECK(!s.ed.mouse_press(s.ed.offset_at_screen(1, 2), false));
  CHECK(!s.ed.mouse_double_click(s.ed.offset_at_screen(1, 2)));
  CHECK(!s.ed.mouse_drag(8));
  CHECK_EQ(s.point(), size_t{1});
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L""));
}

// ---- wheel ----

TEST(mouse_wheel_notches_scroll_by_the_rows_per_notch) {
  WheelAccumulator w;
  CHECK_EQ(w.add(120, 3), -3LL);  // away from the user: towards the start
  CHECK_EQ(w.add(-120, 3), 3LL);
  CHECK_EQ(w.add(-240, 3), 6LL);
  CHECK_EQ(w.add(-120, 8), 8LL);  // a page (WHEEL_PAGESCROLL) per notch
  CHECK_EQ(w.add(0, 3), 0LL);
}

TEST(mouse_wheel_partial_deltas_accumulate) {
  WheelAccumulator w;
  CHECK_EQ(w.add(-20, 3), 0LL);   // 60 of 120
  CHECK_EQ(w.add(-20, 3), 1LL);   // 120: one row
  CHECK_EQ(w.add(-30, 3), 0LL);   // 90
  CHECK_EQ(w.add(-30, 3), 1LL);   // 180: one row, 60 kept
  CHECK_EQ(w.add(-20, 3), 1LL);   // 120
  // A turn the other way drops what was kept.
  CHECK_EQ(w.add(-30, 3), 0LL);   // 90 kept
  CHECK_EQ(w.add(30, 3), 0LL);    // reset, then 90 the other way
  CHECK_EQ(w.add(10, 3), -1LL);   // 120
}

TEST(mouse_wheel_scroll_keeps_point_visible) {
  Session s;
  std::wstring text;
  for (int i = 0; i < 50; ++i) text += L"line" + std::to_wstring(i) + L"\n";
  s.load(text);
  s.set_point(s.ed.buffer().text.line_start(2));
  WheelAccumulator w;
  s.ed.scroll_by_rows(w.add(-120, 3));
  CHECK_EQ(s.ed.top().line, size_t{3});
  // Point (line 2) left the window: it moves to the top row.
  CHECK_EQ(s.ed.point_line(), size_t{3});
  s.ed.scroll_by_rows(w.add(-120, 3));
  CHECK_EQ(s.ed.top().line, size_t{6});
  s.ed.scroll_by_rows(w.add(120, 3));
  CHECK_EQ(s.ed.top().line, size_t{3});
  // Point is on screen: it stays.
  CHECK_EQ(s.ed.point_line(), size_t{6});
}
