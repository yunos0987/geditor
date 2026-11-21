// The display model of M3a: settings reaching it ([C4] a), end-of-line marks
// ([M3-7] / [C7]), View commands ([C8]), text-scale ([C9]), the scroll bar
// ([C11]), the 64K checkpoints of long lines ([C12]) and the reporting of
// invalid display values ([C14]).

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "display.h"
#include "fake_host.h"
#include "unicode.h"

using geditor::ColorIndex;
using geditor::DisplayRow;
using geditor::RowPos;
using geditor::WrapMode;
using test::Session;

namespace {

DisplayRow row_of(const Session& s, size_t line, size_t row = 0, int left = 0, int columns = 200) {
  DisplayRow r;
  s.ed.display_row(RowPos{line, row}, left, columns, &r);
  return r;
}

// The characters of a row as drawn (marks replace their characters).
std::wstring drawn(const DisplayRow& r) {
  std::wstring s;
  for (const geditor::DisplayCell& c : r.cells) geditor::append_utf16(s, c.c);
  return s;
}

std::wstring trailing_flags(const DisplayRow& r) {
  std::wstring s;
  for (const geditor::DisplayCell& c : r.cells) s += c.trailing ? L'T' : L'.';
  return s;
}

void run_mx(Session& s, const wchar_t* command) {
  s.keys(L"M-x");
  s.type(command);
  s.keys(L"RET");
}

bool near(double a, double b) {
  return std::fabs(a - b) < 1e-6;
}

}  // namespace

// ---- [C4] a: each display item of PLAN 4.5 reaches the model ----

TEST(display_font_family_size_and_fallback_reach_the_model) {
  Session s;
  s.settings("{\"font\": {\"family\": \"MS Gothic\", \"size\": 15, \"fallback\": [\"BIZ UDGothic\", \"Segoe UI Emoji\"]}}");
  CHECK_EQ(s.ed.config().font_family, std::wstring(L"MS Gothic"));
  CHECK_EQ(s.ed.config().font_fallback.size(), size_t{2});
  CHECK(near(s.ed.font_pixels(96), 20.0));  // 15 pt at 96 DPI
  CHECK(near(s.ed.font_pixels(144), 30.0));
}

TEST(display_each_color_key_reaches_the_model) {
  const char* names[] = {"background", "foreground", "cursor", "selection", "line_number",
                         "line_number_current", "search_match", "search_current", "whitespace",
                         "eol", "eol_other", "mode_line_background", "mode_line_foreground"};
  std::string text = "{\"colors\": {";
  for (int i = 0; i < geditor::kColorCount; ++i) {
    char value[16];
    std::snprintf(value, sizeof(value), "#0000%02x", i + 1);
    text += std::string(i > 0 ? ", " : "") + "\"" + names[i] + "\": \"" + value + "\"";
  }
  text += "}}";
  Session s;
  s.settings(text);
  CHECK(s.ed.settings_errors().empty());
  for (int i = 0; i < geditor::kColorCount; ++i) {
    const uint32_t value = s.ed.config().colors[static_cast<size_t>(i)];
    CHECK_EQ(value, static_cast<uint32_t>(i + 1));
  }
}

TEST(display_tab_width_reaches_the_layout) {
  Session s;
  s.settings("{\"tab_width\": 8}");
  s.load(L"\tx");
  CHECK_EQ(s.ed.tab_width(), 8);
  CHECK_EQ(s.ed.layout().tab_width(), 8);
  s.set_point(1);
  CHECK_EQ(s.ed.point_column(), 8);
  CHECK_EQ(s.ed.point_view().column, 8);
}

TEST(display_line_numbers_setting_sets_the_gutter) {
  Session s;
  s.load(L"a\nb");
  CHECK_EQ(s.ed.gutter_columns(), 3);  // E2: at least 2 digits + 1 blank
  s.settings("{\"line_numbers\": false}");
  CHECK_EQ(s.ed.gutter_columns(), 0);
  s.settings("{\"line_numbers\": true}");
  CHECK_EQ(s.ed.gutter_columns(), 3);
  Session big;
  std::wstring many;
  for (int i = 0; i < 1000; ++i) many += L"x\n";
  big.load(many);  // 1001 lines: 4 digits
  CHECK_EQ(big.ed.gutter_columns(), 5);
  CHECK_EQ(geditor::gutter_columns(9, true), 3);
  CHECK_EQ(geditor::gutter_columns(99, true), 3);
  CHECK_EQ(geditor::gutter_columns(100, true), 4);
  // Only the first row of a wrapped line shows its number.
  Session w;
  w.host.columns = 13;  // gutter 3, text 10, window 9
  w.settings("{\"wrap\": {\"mode\": \"window\"}}");
  w.load(std::wstring(20, L'x'));
  CHECK(row_of(w, 0, 0).first_row);
  CHECK(!row_of(w, 0, 1).first_row);
}

TEST(display_wrap_mode_and_column_reach_the_layout) {
  Session s;
  s.host.columns = 104;  // gutter 3, text 101, window 100
  CHECK_EQ(s.ed.wrap_width(), 80);
  s.settings("{\"wrap\": {\"mode\": \"fixed\", \"column\": 120}}");
  CHECK(s.ed.wrap_mode() == WrapMode::kFixed);
  CHECK_EQ(s.ed.wrap_width(), 120);
  CHECK(s.ed.mode_line().find(L"wrap:fixed") != std::wstring::npos);
  s.settings("{\"wrap\": {\"mode\": \"window\"}}");
  CHECK_EQ(s.ed.wrap_width(), 100);
  s.settings("{\"wrap\": {\"mode\": \"none\"}}");
  CHECK_EQ(s.ed.wrap_width(), 0);
  CHECK_EQ(s.ed.layout().width(), 0);
}

TEST(display_whitespace_flags_reach_the_rows) {
  Session s;
  s.load(L"a b\tc\u3000d  ");
  // Defaults: tab, fullwidth space and trailing on; space off.
  DisplayRow r = row_of(s, 0);
  CHECK_EQ(drawn(r), std::wstring(L"a b\u2192c\u25A1d  "));
  CHECK_EQ(trailing_flags(r), std::wstring(L".......TT"));
  for (const geditor::DisplayCell& c : r.cells) CHECK_EQ(c.mark, c.c == U'\u2192' || c.c == U'\u25A1');
  s.settings("{\"whitespace\": {\"space\": true}}");
  CHECK_EQ(drawn(row_of(s, 0)), std::wstring(L"a\u00B7b\u2192c\u25A1d\u00B7\u00B7"));
  s.settings("{\"whitespace\": {\"tab\": false, \"fullwidth_space\": false, \"trailing\": false}}");
  r = row_of(s, 0);
  CHECK_EQ(drawn(r), std::wstring(L"a b\tc\u3000d  "));
  CHECK_EQ(trailing_flags(r), std::wstring(L"........."));
  s.settings("{\"whitespace\": {\"trailing\": true, \"tab\": false}}");
  CHECK_EQ(trailing_flags(row_of(s, 0)), std::wstring(L".......TT"));
  // Cells keep their columns: the TAB mark still spans to the tab stop.
  r = row_of(s, 0);
  CHECK_EQ(r.cells[3].column, 3);
  CHECK_EQ(r.cells[3].width, 1);
  CHECK_EQ(r.cells[4].column, 4);
}

TEST(display_eol_marks_show_and_symbols_reach_the_rows) {
  Session s;
  s.load(L"a\nb");
  CHECK(row_of(s, 0).eol.shown);
  CHECK_EQ(row_of(s, 0).eol.symbol, std::wstring(L"\u2193"));
  s.settings("{\"eol_marks\": {\"show\": false}}");
  CHECK(!row_of(s, 0).eol.shown);
  s.settings("{\"eol_marks\": {\"lf\": \"$\", \"crlf\": \"\xC2\xB6\"}}");
  CHECK_EQ(row_of(s, 0).eol.symbol, std::wstring(L"$"));
}

TEST(display_settings_reload_keeps_text_point_and_undo) {
  Session s;
  s.load(L"hello");
  s.keys(L"C-e");
  s.type(L"!!");
  s.settings("{\"tab_width\": 2, \"line_numbers\": false, \"wrap\": {\"mode\": \"none\"},"
             " \"colors\": {\"background\": \"#000000\"}, \"font\": {\"size\": 20}}");
  CHECK_EQ(s.text(), std::wstring(L"hello!!"));
  CHECK_EQ(s.point(), size_t{7});
  CHECK(s.modified());
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"hello"));
}

// ---- [M3-7] / [C7] end-of-line marks ----

TEST(eol_marks_lf_file) {
  Session s;
  s.load(L"ab\ncd\nef");
  for (size_t line = 0; line < 2; ++line) {
    const DisplayRow r = row_of(s, line);
    CHECK(r.eol.shown);
    CHECK_EQ(r.eol.symbol, std::wstring(L"\u2193"));
    CHECK(r.eol.color == geditor::kColorEol);
    CHECK_EQ(r.end_column, 2);  // drawn in the cell after the line end
  }
  CHECK(!row_of(s, 2).eol.shown);  // last line: no line end
}

TEST(eol_marks_crlf_file) {
  Session s;
  s.host.add_file(L"C:\\work\\crlf.txt", "ab\r\ncd\r\n");
  s.ed.request_open(L"C:\\work\\crlf.txt");
  const DisplayRow r = row_of(s, 0);
  CHECK(r.eol.shown);
  CHECK_EQ(r.eol.symbol, std::wstring(L"\u21B5"));
  CHECK(r.eol.color == geditor::kColorEol);
  CHECK(row_of(s, 1).eol.shown);
  CHECK(!row_of(s, 2).eol.shown);  // the empty last line after the final CRLF
}

TEST(eol_marks_last_line_without_newline) {
  Session s;
  s.load(L"one\ntwo");
  CHECK(row_of(s, 0).eol.shown);
  CHECK(!row_of(s, 1).eol.shown);
  CHECK(!row_of(s, 1).has_newline);
}

TEST(eol_marks_show_false) {
  Session s;
  s.settings("{\"eol_marks\": {\"show\": false}}");
  s.load(L"one\ntwo\n");
  CHECK(!row_of(s, 0).eol.shown);
  CHECK(!row_of(s, 1).eol.shown);
}

TEST(eol_marks_custom_symbols) {
  Session s;
  s.settings("{\"eol_marks\": {\"lf\": \"\xC2\xAC\", \"crlf\": \"\xE2\x86\xA9\"}}");
  s.load(L"x\ny");
  CHECK_EQ(row_of(s, 0).eol.symbol, std::wstring(L"\u00AC"));
  Session c;
  c.settings("{\"eol_marks\": {\"lf\": \"\xC2\xAC\", \"crlf\": \"\xE2\x86\xA9\"}}");
  c.host.add_file(L"C:\\work\\crlf.txt", "x\r\ny");
  c.ed.request_open(L"C:\\work\\crlf.txt");
  CHECK_EQ(row_of(c, 0).eol.symbol, std::wstring(L"\u21A9"));
}

TEST(eol_marks_do_not_change_cursor_or_wrap_columns) {
  // A line exactly as wide as the wrap width: one row, its mark in the
  // reserved cell (column = width), and the cursor at the line end is in the
  // same cell (a bar on the cell's left edge).
  Session s;
  s.host.columns = 13;  // gutter 3, text 10, window 9
  s.settings("{\"wrap\": {\"mode\": \"window\"}}");
  s.load(L"123456789\nx");
  CHECK_EQ(s.ed.layout().row_count(s.ed.buffer().text, 0), size_t{1});
  const DisplayRow r = row_of(s, 0, 0, 0, 10);
  CHECK(r.complete && r.last_row && r.eol.shown);
  CHECK_EQ(r.end_column, 9);
  s.keys(L"C-e");
  CHECK_EQ(s.ed.point_view().column, 9);
  CHECK_EQ(s.ed.point_column(), 9);
  // Same rows and columns with marks off.
  s.settings("{\"wrap\": {\"mode\": \"window\"}, \"eol_marks\": {\"show\": false}}");
  CHECK_EQ(s.ed.layout().row_count(s.ed.buffer().text, 0), size_t{1});
  CHECK_EQ(s.ed.point_view().column, 9);
  CHECK_EQ(row_of(s, 0, 0, 0, 10).end_column, 9);
}

// ---- [C8] View commands from M-x ----

TEST(view_toggle_line_numbers) {
  Session s;
  s.load(L"a");
  run_mx(s, L"toggle-line-numbers");
  CHECK(!s.ed.line_numbers());
  CHECK_EQ(s.ed.gutter_columns(), 0);
  CHECK_EQ(s.message(), std::wstring(L"Line numbers off"));
  run_mx(s, L"toggle-line-numbers");
  CHECK(s.ed.line_numbers());
}

TEST(view_toggle_eol_marks) {
  Session s;
  s.load(L"a\nb");
  run_mx(s, L"toggle-eol-marks");
  CHECK(!s.ed.eol_marks());
  CHECK(!row_of(s, 0).eol.shown);
  run_mx(s, L"toggle-eol-marks");
  CHECK(row_of(s, 0).eol.shown);
}

TEST(view_toggle_whitespace_marks) {
  Session s;
  s.load(L"a\tb ");
  run_mx(s, L"toggle-whitespace-marks");
  CHECK(!s.ed.whitespace_marks());
  CHECK_EQ(drawn(row_of(s, 0)), std::wstring(L"a\tb "));
  CHECK_EQ(trailing_flags(row_of(s, 0)), std::wstring(L"...."));
  run_mx(s, L"toggle-whitespace-marks");
  CHECK(s.ed.whitespace_marks());
  CHECK_EQ(drawn(row_of(s, 0)), std::wstring(L"a\u2192b "));  // the file's flags again
  // With every flag off in the file, turning marks on shows all kinds.
  s.settings("{\"whitespace\": {\"space\": false, \"tab\": false, \"fullwidth_space\": false, \"trailing\": false}}");
  CHECK(!s.ed.whitespace_marks());
  run_mx(s, L"toggle-whitespace-marks");
  CHECK_EQ(drawn(row_of(s, 0)), std::wstring(L"a\u2192b\u00B7"));
}

TEST(view_set_wrap_mode) {
  Session s;
  s.load(std::wstring(100, L'x'));
  run_mx(s, L"set-wrap-mode");
  CHECK(s.ed.minibuffer_active());  // asks for the mode
  s.type(L"none");
  s.keys(L"RET");
  CHECK(s.ed.wrap_mode() == WrapMode::kNone);
  CHECK(s.ed.mode_line().find(L"wrap:none") != std::wstring::npos);
  for (const wchar_t* mode : {L"window", L"fixed", L"max"}) {
    run_mx(s, L"set-wrap-mode");
    s.type(mode);
    s.keys(L"RET");
    CHECK(s.ed.mode_line().find(std::wstring(L"wrap:") + mode) != std::wstring::npos);
  }
  // A binding with a bad argument is a settings error; a good one works.
  s.settings("{\"keys\": {\"C-c w\": [\"set-wrap-mode\", \"zigzag\"], \"C-c n\": [\"set-wrap-mode\", \"none\"]}}");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{1});
  s.keys(L"C-c n");
  CHECK(s.ed.wrap_mode() == WrapMode::kNone);
  CHECK_EQ(s.message(), std::wstring(L"Wrap: none"));
}

// 2026-11-21: cycle-wrap-mode (C-x x t, M-z) steps through View > Wrap in
// order, after Max back to None.
TEST(view_cycle_wrap_mode) {
  Session s;
  s.load(std::wstring(100, L'x'));
  const std::string settings_before = s.host.file(test::kSettingsPath);
  CHECK(s.ed.wrap_mode() == WrapMode::kMax);  // the default
  const WrapMode order[] = {WrapMode::kNone, WrapMode::kWindow, WrapMode::kFixed, WrapMode::kMax};
  const wchar_t* const names[] = {L"none", L"window", L"fixed", L"max"};
  for (const wchar_t* key : {L"M-z", L"C-x x t"}) {
    for (size_t i = 0; i < 4; ++i) {
      s.keys(key);
      CHECK(s.ed.wrap_mode() == order[i]);
      CHECK_EQ(s.message(), std::wstring(L"Wrap: ") + names[i]);
      CHECK(s.ed.mode_line().find(std::wstring(L"wrap:") + names[i]) != std::wstring::npos);
    }
  }
  run_mx(s, L"cycle-wrap-mode");
  CHECK(s.ed.wrap_mode() == WrapMode::kNone);
  // The text, the point and the settings file do not change.
  CHECK_EQ(s.text(), std::wstring(100, L'x'));
  CHECK_EQ(s.host.file(test::kSettingsPath), settings_before);
  // Another key after C-x x is not bound and leaves the mode as it is.
  s.keys(L"C-x x g");
  CHECK(s.ed.wrap_mode() == WrapMode::kNone);
  // It goes on from the mode the settings file gives.
  s.settings("{\"wrap\": {\"mode\": \"window\"}}");
  CHECK(s.ed.wrap_mode() == WrapMode::kWindow);
  s.keys(L"M-z");
  CHECK(s.ed.wrap_mode() == WrapMode::kFixed);
  // It works in read-only-mode (it changes the view only).
  s.keys(L"C-x C-q");
  CHECK(s.ed.buffer().read_only);
  s.keys(L"M-z");
  CHECK(s.ed.wrap_mode() == WrapMode::kMax);
  CHECK_EQ(s.text(), std::wstring(100, L'x'));
}

TEST(view_commands_never_write_the_settings_file) {
  Session s;
  s.settings("{\"line_numbers\": true}");
  s.load(L"abc");
  const std::string before = s.host.file(test::kSettingsPath);
  run_mx(s, L"toggle-line-numbers");
  run_mx(s, L"toggle-eol-marks");
  run_mx(s, L"toggle-whitespace-marks");
  run_mx(s, L"set-wrap-mode");
  s.type(L"none");
  s.keys(L"RET");
  s.keys(L"C-x C-+");
  CHECK(s.host.written.empty());
  CHECK_EQ(s.host.file(test::kSettingsPath), before);
}

TEST(view_toggles_survive_a_reload_until_the_file_value_changes) {
  // E6.
  Session s;
  s.settings("{\"line_numbers\": true, \"wrap\": {\"mode\": \"max\"}, \"tab_width\": 4}");
  s.load(L"abc");
  run_mx(s, L"toggle-line-numbers");
  run_mx(s, L"toggle-eol-marks");
  run_mx(s, L"toggle-whitespace-marks");
  run_mx(s, L"set-wrap-mode");
  s.type(L"none");
  s.keys(L"RET");
  // Another item changes: the toggled values stay.
  s.settings("{\"line_numbers\": true, \"wrap\": {\"mode\": \"max\"}, \"tab_width\": 8}");
  CHECK(!s.ed.line_numbers());
  CHECK(!s.ed.eol_marks());
  CHECK(!s.ed.whitespace_marks());
  CHECK(s.ed.wrap_mode() == WrapMode::kNone);
  // The file's value for the item changes: the file wins.
  s.settings("{\"line_numbers\": false, \"wrap\": {\"mode\": \"window\"}, \"tab_width\": 8,"
             " \"eol_marks\": {\"show\": false}, \"whitespace\": {\"space\": true}}");
  CHECK(!s.ed.line_numbers());
  CHECK(s.ed.wrap_mode() == WrapMode::kWindow);
  CHECK(!s.ed.eol_marks());
  CHECK(s.ed.whitespace_marks());
  s.settings("{\"line_numbers\": true}");
  CHECK(s.ed.line_numbers());
  CHECK(s.ed.wrap_mode() == WrapMode::kMax);
}

// ---- [C9] text-scale ----

TEST(text_scale_increase_multiplies_by_1_2) {
  Session s;
  CHECK(near(s.ed.font_pixels(96), 16.0));
  s.keys(L"C-x C-+");
  CHECK_EQ(s.ed.text_scale(), 1);
  CHECK(near(s.ed.font_pixels(96), 19.2));
  s.keys(L"C-x C-=");
  CHECK(near(s.ed.font_pixels(96), 16.0 * 1.44));
  CHECK_EQ(s.message(), std::wstring(L"Text scale +2"));
}

TEST(text_scale_decrease_divides_by_1_2) {
  Session s;
  s.keys(L"C-x C--");
  CHECK_EQ(s.ed.text_scale(), -1);
  CHECK(near(s.ed.font_pixels(96), 16.0 / 1.2));
  CHECK_EQ(s.message(), std::wstring(L"Text scale -1"));
}

TEST(text_scale_reset_returns_to_the_settings_size) {
  Session s;
  s.keys(L"C-x C-+ C-x C-+ C-x C-+");
  s.keys(L"C-x C-0");
  CHECK_EQ(s.ed.text_scale(), 0);
  CHECK(near(s.ed.font_pixels(96), 16.0));
  run_mx(s, L"text-scale-increase");
  CHECK_EQ(s.ed.text_scale(), 1);
  run_mx(s, L"text-scale-reset");
  CHECK_EQ(s.ed.text_scale(), 0);
}

TEST(text_scale_clamps_at_6_and_200_pixels) {
  CHECK(near(geditor::text_scale_pixels(12, 100, 96), 200));
  CHECK(near(geditor::text_scale_pixels(12, -100, 96), 6));
  Session s;
  for (int i = 0; i < 40 && s.ed.font_pixels(96) < 200; ++i) s.keys(L"C-x C-+");
  CHECK(near(s.ed.font_pixels(96), 200));
  const int at_max = s.ed.text_scale();
  s.keys(L"C-x C-+");
  CHECK_EQ(s.ed.text_scale(), at_max);
  CHECK_EQ(s.message(), std::wstring(L"Text is at the largest size"));
  s.keys(L"C-x C-0");
  for (int i = 0; i < 40 && s.ed.font_pixels(96) > 6; ++i) s.keys(L"C-x C--");
  CHECK(near(s.ed.font_pixels(96), 6));
  const int at_min = s.ed.text_scale();
  s.keys(L"C-x C--");
  CHECK_EQ(s.ed.text_scale(), at_min);
  CHECK_EQ(s.message(), std::wstring(L"Text is at the smallest size"));
  // The limits are in pixels: at 192 DPI the same size is twice the pixels.
  s.host.display_dpi = 192;
  s.keys(L"C-x C-0");
  CHECK(near(s.ed.font_pixels(192), 32));
}

TEST(text_scale_is_kept_across_a_reload_on_the_new_size) {
  Session s;
  s.keys(L"C-x C-+");
  s.settings("{\"font\": {\"size\": 15}}");
  CHECK_EQ(s.ed.text_scale(), 1);
  CHECK(near(s.ed.font_pixels(96), 20.0 * 1.2));
}

TEST(text_scale_keeps_point_visible_when_rows_shrink) {
  // The window rebuilds the font and asks the core to keep point visible
  // with the new number of rows.
  Session s;
  std::wstring text;
  for (int i = 0; i < 40; ++i) text += L"line\n";
  s.host.lines = 30;
  s.load(text);
  s.set_point(25 * 5);  // line 25
  CHECK_EQ(s.ed.top_line(), size_t{0});
  s.keys(L"C-x C-+");
  s.host.lines = 10;  // bigger cells: fewer rows
  s.ed.ensure_point_visible();
  CHECK(s.ed.top_line() <= 25 && 25 < s.ed.top_line() + 10);
}

// ---- [C11] scroll bar ----

TEST(scroll_bar_follows_logical_lines) {
  Session s;
  std::wstring text;
  for (int i = 0; i < 100; ++i) text += L"line\n";
  s.host.lines = 10;
  s.load(text);  // 101 lines
  geditor::ScrollBarState bar = s.ed.scroll_bar();
  CHECK_EQ(bar.max, size_t{100});
  CHECK_EQ(bar.page, size_t{10});
  CHECK_EQ(bar.pos, size_t{0});
  s.keys(L"C-v");
  CHECK_EQ(s.ed.scroll_bar().pos, size_t{8});
}

TEST(scroll_bar_line_and_page_scroll_keep_point_visible) {
  Session s;
  std::wstring text;
  for (int i = 0; i < 100; ++i) text += L"line\n";
  s.host.lines = 10;
  s.load(text);
  s.ed.scroll_by_rows(1);  // SB_LINEDOWN
  CHECK_EQ(s.ed.top_line(), size_t{1});
  CHECK_EQ(s.ed.point_line(), size_t{1});  // point was on the line that left
  s.ed.scroll_by_rows(8);  // SB_PAGEDOWN (visible - 2)
  CHECK_EQ(s.ed.top_line(), size_t{9});
  CHECK_EQ(s.ed.point_line(), size_t{9});
  s.ed.scroll_by_rows(-3);  // SB_LINEUP x3: point stays (still visible)
  CHECK_EQ(s.ed.top_line(), size_t{6});
  CHECK_EQ(s.ed.point_line(), size_t{9});
  s.ed.scroll_by_rows(-100);
  CHECK_EQ(s.ed.top_line(), size_t{0});
  CHECK_EQ(s.ed.point_line(), size_t{9});
  s.ed.scroll_by_rows(1000);  // stops with the last line on the bottom row
  CHECK_EQ(s.ed.top_line(), size_t{91});
  CHECK_EQ(s.ed.point_line(), size_t{91});
}

TEST(scroll_bar_thumb_track_scrolls_to_a_line) {
  Session s;
  std::wstring text;
  for (int i = 0; i < 100; ++i) text += L"line\n";
  s.host.lines = 10;
  s.load(text);
  s.ed.scroll_to_line(50);  // SB_THUMBTRACK / SB_THUMBPOSITION
  CHECK_EQ(s.ed.top_line(), size_t{50});
  CHECK_EQ(s.ed.point_line(), size_t{50});
  CHECK_EQ(s.ed.scroll_bar().pos, size_t{50});
  s.ed.scroll_to_line(99);  // clamped like the line scrolls
  CHECK_EQ(s.ed.top_line(), size_t{91});
  s.ed.scroll_to_line(0);
  CHECK_EQ(s.ed.top_line(), size_t{0});
  CHECK_EQ(s.ed.point_line(), size_t{9});  // moved to the bottom row
}

TEST(scroll_bar_rows_of_wrapped_lines) {
  Session s;
  s.host.columns = 11;
  s.host.lines = 3;
  s.settings("{\"line_numbers\": false, \"wrap\": {\"mode\": \"window\"}}");
  s.load(std::wstring(50, L'x') + L"\ny");  // 5 + 1 rows
  s.ed.scroll_by_rows(2);
  CHECK(s.ed.top() == (RowPos{0, 2}));
  // B6: 2 logical lines in a 3-row window that do not fit: by character
  // offset (the start of the top row); with more logical lines than rows the
  // bar stays by logical line (scroll_bar_thumb_track_scrolls_to_a_line).
  CHECK(s.ed.scroll_bar().by_offset);
  CHECK_EQ(s.ed.scroll_bar().pos, size_t{20});
  s.ed.scroll_by_rows(10);
  CHECK(s.ed.top() == (RowPos{0, 3}));  // rows 3, 4 and line 1 fill the window
}

// ---- [C12] checkpoints of long lines ----

TEST(long_line_wrap_does_not_scan_from_the_line_start) {
  // A 10 MB line (5M UTF-16 units of ASCII and Japanese).
  std::wstring text;
  text.reserve(5u << 20);
  while (text.size() < (5u << 20)) text += L"editor \u65E5\u672C\u8A9E text ";
  geditor::PieceTree t(text);
  geditor::WrapLayout l;
  l.configure(80, 4);
  const size_t rows = l.row_count(t, 0);  // walks the line once
  CHECK(rows > 50000);
  CHECK(l.checkpoint_count(0) >= text.size() / geditor::WrapLayout::kCheckpointInterval);
  const size_t near_end = text.size() - 1000;
  l.reset_scanned();
  int column = 0;
  const RowPos p = l.row_of(t, near_end, &column);
  CHECK(l.scanned() <= geditor::WrapLayout::kCheckpointInterval);
  l.reset_scanned();
  CHECK_EQ(l.offset_at(t, 0, p.row, column), near_end);
  CHECK(l.row_start(t, 0, rows - 1) > near_end - 200);
  CHECK(l.scanned() <= 2 * geditor::WrapLayout::kCheckpointInterval);
  // An edit near the end keeps the states before it.
  t.insert(near_end, L"x");
  l.note_edit(t, near_end);
  l.reset_scanned();
  l.row_of(t, near_end + 1, &column);
  CHECK(l.scanned() <= geditor::WrapLayout::kCheckpointInterval);
  // A change the layout was not told about drops them (a full walk again).
  t.erase(0, 1);
  l.reset_scanned();
  l.row_of(t, near_end, &column);
  CHECK(l.scanned() > text.size() / 2);
}

TEST(long_line_typing_at_the_end_through_the_editor) {
  Session s;
  std::wstring text;
  while (text.size() < (1u << 20)) text += L"abc \u3042\u3044 ";
  s.load(text);
  s.keys(L"M->");  // walks the line once, recording states
  s.ed.layout().reset_scanned();
  s.type(L"xyz");
  s.ed.mode_line();
  // Each key: ensure_point_visible, point_view and the mode line column,
  // each from the nearest state before point.
  CHECK(s.ed.layout().scanned() < 20 * geditor::WrapLayout::kCheckpointInterval);
}

// ---- [C14] invalid display values are reported ----

TEST(display_invalid_values_are_reported_in_the_echo_area) {
  Session s;
  s.load(L"abc");
  s.settings(
      "{\n"
      "  \"font\": {\"size\": 0},\n"
      "  \"colors\": {\"background\": \"#12345\"},\n"
      "  \"wrap\": {\"mode\": \"zigzag\", \"column\": 0},\n"
      "  \"tab_width\": 99\n"
      "}\n");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{5});
  const std::wstring m = s.message();
  CHECK(m.find(L":2:") != std::wstring::npos);
  CHECK(m.find(L":3:") != std::wstring::npos);
  CHECK(m.find(L":4:") != std::wstring::npos);
  CHECK(m.find(L":5:") != std::wstring::npos);
  // The defaults stay for the dropped items.
  CHECK(near(s.ed.font_pixels(96), 16.0));
  CHECK_EQ(s.ed.config().colors[geditor::kColorBackground], uint32_t{0x1e1e1e});
  CHECK(s.ed.wrap_mode() == WrapMode::kMax);
  CHECK_EQ(s.ed.tab_width(), 4);
}

// ---- Review R1: missing font names once per family / fallback value ----

TEST(missing_font_report_once_per_family_and_fallback) {
  geditor::MissingFontReport r;
  const std::vector<std::wstring> none;
  const std::vector<std::wstring> missing = {L"NoSuchFontXYZ"};
  // Start: reported.
  CHECK_EQ(r.take(L"NoSuchFontXYZ", none, missing).size(), size_t{1});
  // Rebuilds for text-scale, DPI or font.size (same family / fallback): silent.
  CHECK(r.take(L"NoSuchFontXYZ", none, missing).empty());
  CHECK(r.take(L"NoSuchFontXYZ", none, missing).empty());
  // Another missing family: reported.
  const std::vector<std::wstring> other = {L"OtherMissing"};
  const std::vector<std::wstring> shown = r.take(L"OtherMissing", none, other);
  CHECK_EQ(shown.size(), size_t{1});
  if (!shown.empty()) CHECK_EQ(shown[0], std::wstring(L"OtherMissing"));
  // A fallback change reports again, also names reported before.
  const std::vector<std::wstring> fallback = {L"MissingFallback"};
  const std::vector<std::wstring> both = {L"OtherMissing", L"MissingFallback"};
  CHECK_EQ(r.take(L"OtherMissing", fallback, both).size(), size_t{2});
  CHECK(r.take(L"OtherMissing", fallback, both).empty());
  // Back to an installed font: nothing to report; then the missing one again.
  CHECK(r.take(L"Consolas", none, none).empty());
  CHECK_EQ(r.take(L"NoSuchFontXYZ", none, missing).size(), size_t{1});
}

// ---- Review R2: the whitespace tail of a long line is not walked per row ----

namespace {

// "a" + 2,000,000 spaces + "\nb": the tail is 2M spaces.
std::wstring padded_line() {
  return L"a" + std::wstring(2000000, L' ') + L"\nb";
}

// Draws `rows` rows from the top as the renderer does; returns the
// characters the layout walked.
size_t frame_work(const Session& s, int rows) {
  s.ed.layout().reset_scanned();
  RowPos pos = s.ed.top();
  DisplayRow r;
  for (int i = 0; i < rows; ++i) {
    s.ed.display_row(pos, 0, 81, &r);
    if (!s.ed.next_row(&pos)) break;
  }
  return s.ed.layout().scanned();
}

}  // namespace

TEST(trailing_whitespace_tail_is_not_walked_per_row) {
  Session s;
  s.host.lines = 30;
  s.load(padded_line());
  const auto t0 = std::chrono::steady_clock::now();
  const size_t first = frame_work(s, 30);  // finds the tail once
  const auto t1 = std::chrono::steady_clock::now();
  const size_t second = frame_work(s, 30);
  const auto t2 = std::chrono::steady_clock::now();
  // The old way: the whole tail walked for every row.
  for (int i = 0; i < 30; ++i) geditor::trailing_whitespace_start(s.ed.buffer().text, 0);
  const auto t3 = std::chrono::steady_clock::now();
  auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  std::printf("  30-row frame: first %.2f ms (%zu chars), next %.3f ms (%zu chars); "
              "old per-row tail walk x30: %.2f ms\n",
              ms(t0, t1), first, ms(t1, t2), second, ms(t2, t3));
  CHECK(first >= 2000000);  // the first frame finds the tail once
  CHECK(first < 2 * 2000000);
  CHECK(second < 10000);    // later frames do not walk it
  // The rows of the tail are trailing whitespace.
  DisplayRow r;
  s.ed.display_row(RowPos{0, 3}, 0, 81, &r);
  CHECK(!r.cells.empty() && r.cells.front().trailing);
  s.ed.display_row(RowPos{0, 0}, 0, 81, &r);
  CHECK(!r.cells.empty() && !r.cells.front().trailing && r.cells.back().trailing);
  // Typing at the end of the tail: the walk back stops at the edit (the
  // rows there start from a wrap checkpoint, at most one gap away).
  s.keys(L"C-e");
  s.type(L"x");
  CHECK(frame_work(s, 30) < geditor::WrapLayout::kCheckpointInterval + 10000);
  CHECK_EQ(s.ed.layout().trailing_start(s.ed.buffer().text, 0), size_t{2000002});
  s.keys(L"DEL");  // the tail is whitespace again
  frame_work(s, 30);
  CHECK_EQ(s.ed.layout().trailing_start(s.ed.buffer().text, 0), size_t{1});
  // Typing in the middle of the tail: the edit alone updates the value.
  const geditor::PieceTree& t = s.ed.buffer().text;
  s.set_point(1000000);
  s.type(L"y");
  s.ed.layout().reset_scanned();
  CHECK_EQ(s.ed.layout().trailing_start(t, 0), size_t{1000001});
  CHECK(s.ed.layout().scanned() < 100);
  s.type(L" ");
  s.keys(L"DEL DEL");  // erases the space and the 'y': walks back over the spaces before it
  CHECK_EQ(s.ed.layout().trailing_start(t, 0), size_t{1});
  // In the middle of the tail (the line's wrap states recorded once): no
  // tail walk, at most one checkpoint gap of wrapping.
  s.ed.layout().row_count(s.ed.buffer().text, 0);
  s.ed.set_top(RowPos{0, 12000});
  CHECK(frame_work(s, 30) < geditor::WrapLayout::kCheckpointInterval + 10000);
}

TEST(trailing_whitespace_cache_matches_a_full_scan_after_edits) {
  Session s;
  s.load(L"ab  \t 　\nxy  z   \n   \nq");
  const geditor::PieceTree& t = s.ed.buffer().text;
  auto agree = [&] {
    for (size_t line = 0; line < t.line_count(); ++line) {
      if (s.ed.layout().trailing_start(t, line) != geditor::trailing_whitespace_start(t, line)) return false;
    }
    return true;
  };
  CHECK(agree());
  // Edits before, inside and after the tails; a newline; deleting the last
  // character of text; joining lines.
  const wchar_t* steps[] = {L"C-e", L"M-<", L"C-f C-f", L"C-n C-e", L"C-b C-b C-b C-b", L"M->", L"M-<"};
  s.set_point(4);
  s.type(L"k");
  CHECK(agree());
  s.keys(L"DEL");
  CHECK(agree());
  s.set_point(1);
  s.keys(L"C-d");  // "a  \t ..." : last text 'a'
  CHECK(agree());
  s.set_point(0);
  s.keys(L"C-d");  // the line is whitespace only
  CHECK(agree());
  s.keys(L"C-e");
  s.type(L"z");
  CHECK(agree());
  s.keys(L"C-e C-d");  // join with the next line
  CHECK(agree());
  s.keys(L"RET");  // split it again
  CHECK(agree());
  for (const wchar_t* k : steps) {
    s.keys(k);
    s.type(L" ");
    CHECK(agree());
    s.keys(L"C-/");
    CHECK(agree());
  }
}
