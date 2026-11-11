// Movement and mark commands, driven by keys through the fake host ([M1-2]).

#include "fake_host.h"

using test::Session;

namespace {

std::wstring numbered_lines(int count) {
  std::wstring s;
  for (int i = 0; i < count; ++i) {
    if (i > 0) s += L'\n';
    s += L"line" + std::to_wstring(i);
  }
  return s;
}

}  // namespace

TEST(cmd_forward_char_moves_by_code_point) {
  Session s;
  s.load(L"ab\U0001F600c");
  s.keys(L"C-f");
  CHECK_EQ(s.point(), size_t{1});
  s.keys(L"C-f C-f");
  CHECK_EQ(s.point(), size_t{4});  // the surrogate pair is one character
  s.keys(L"<right>");
  CHECK_EQ(s.point(), size_t{5});
  s.keys(L"C-f");
  CHECK_EQ(s.point(), size_t{5});
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
}

TEST(cmd_backward_char_moves_by_code_point) {
  Session s;
  s.load(L"a\U0001F600b");
  s.keys(L"M->");
  s.keys(L"C-b");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"<left>");
  CHECK_EQ(s.point(), size_t{1});
  s.keys(L"C-b C-b");
  CHECK_EQ(s.point(), size_t{0});
  CHECK_EQ(s.message(), std::wstring(L"Beginning of buffer"));
}

TEST(cmd_next_line_keeps_goal_column) {
  Session s;
  s.load(L"abcdef\nab\nabcdef");
  s.set_point(4);
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{9});   // end of the short line
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{14});  // back to column 4
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{16});  // last line: end of buffer
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
}

TEST(cmd_previous_line_keeps_goal_column) {
  Session s;
  s.load(L"abcdef\nab\nabcdef");
  s.set_point(14);
  s.keys(L"C-p");
  CHECK_EQ(s.point(), size_t{9});
  s.keys(L"<up>");
  CHECK_EQ(s.point(), size_t{4});
  s.keys(L"C-p");
  CHECK_EQ(s.point(), size_t{0});
  CHECK_EQ(s.message(), std::wstring(L"Beginning of buffer"));
}

TEST(cmd_vertical_motion_counts_cells) {
  Session s;
  s.load(L"\x3042\x3044\x3046\nabcdef");
  s.set_point(2);  // column 4 (two wide characters)
  s.keys(L"<down>");
  CHECK_EQ(s.point(), size_t{8});  // "abcd|ef"
  s.keys(L"C-f C-p");
  CHECK_EQ(s.point(), size_t{2});  // column 5 falls inside the 3rd wide char
}

TEST(cmd_beginning_and_end_of_line) {
  Session s;
  s.load(L"first\nsecond line\nthird");
  s.set_point(9);
  s.keys(L"C-e");
  CHECK_EQ(s.point(), size_t{17});
  s.keys(L"C-a");
  CHECK_EQ(s.point(), size_t{6});
  s.keys(L"<end>");
  CHECK_EQ(s.point(), size_t{17});
  s.keys(L"<home>");
  CHECK_EQ(s.point(), size_t{6});
}

TEST(cmd_forward_word_and_backward_word) {
  Session s;
  s.load(L"hello, world foo");
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{5});
  s.keys(L"C-<right>");
  CHECK_EQ(s.point(), size_t{12});
  s.keys(L"M-b");
  CHECK_EQ(s.point(), size_t{7});
  s.keys(L"C-<left>");
  CHECK_EQ(s.point(), size_t{0});
}

TEST(cmd_word_motion_stops_at_script_change) {
  Session s;
  s.load(L"\x6F22\x5B57\x304B\x306A\x30AB\x30CA" L"abc1 \x3002x");
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{2});  // Han
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{4});  // Hiragana
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{6});  // Katakana
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{10});  // Latin letters and digits
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{13});  // skips space and ideographic full stop
  s.keys(L"M-b M-b");
  CHECK_EQ(s.point(), size_t{6});
}

TEST(cmd_beginning_and_end_of_buffer_push_mark) {
  Session s;
  s.load(L"one\ntwo\nthree");
  s.set_point(5);
  s.keys(L"M->");
  CHECK_EQ(s.point(), size_t{13});
  CHECK_EQ(s.message(), std::wstring(L"Mark set"));
  CHECK(s.ed.buffer().has_mark && s.ed.buffer().mark == 5);
  s.keys(L"M-<");
  CHECK_EQ(s.point(), size_t{0});
  s.keys(L"C-<end>");
  CHECK_EQ(s.point(), size_t{13});
  s.keys(L"C-<home>");
  CHECK_EQ(s.point(), size_t{0});
}

TEST(cmd_scroll_up_command_scrolls_visible_minus_two) {
  Session s;
  s.host.lines = 10;
  s.load(numbered_lines(30));
  s.keys(L"C-v");
  CHECK_EQ(s.ed.top_line(), size_t{8});
  CHECK_EQ(s.ed.point_line(), size_t{8});
  s.keys(L"<next>");
  CHECK_EQ(s.ed.top_line(), size_t{16});
  s.keys(L"C-v");
  CHECK_EQ(s.ed.top_line(), size_t{20});  // last line (29) on the bottom row
  s.keys(L"C-v");
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
  CHECK_EQ(s.ed.top_line(), size_t{20});
}

TEST(cmd_scroll_down_command_scrolls_back) {
  Session s;
  s.host.lines = 10;
  s.load(numbered_lines(30));
  s.keys(L"C-v C-v");
  CHECK_EQ(s.ed.top_line(), size_t{16});
  s.keys(L"M-v");
  CHECK_EQ(s.ed.top_line(), size_t{8});
  CHECK(s.ed.point_line() <= 17);
  s.keys(L"<prior>");
  CHECK_EQ(s.ed.top_line(), size_t{0});
  CHECK(s.ed.point_line() <= 9);
  s.keys(L"M-v");
  CHECK_EQ(s.message(), std::wstring(L"Beginning of buffer"));
}

TEST(cmd_recenter_top_bottom_cycles) {
  Session s;
  s.host.lines = 10;
  s.load(numbered_lines(40));
  s.set_point(s.ed.buffer().text.line_start(20));
  s.keys(L"C-l");
  CHECK_EQ(s.ed.top_line(), size_t{15});  // center
  s.keys(L"C-l");
  CHECK_EQ(s.ed.top_line(), size_t{20});  // top
  s.keys(L"C-l");
  CHECK_EQ(s.ed.top_line(), size_t{11});  // bottom
  s.keys(L"C-l");
  CHECK_EQ(s.ed.top_line(), size_t{15});  // center again
}

TEST(cmd_set_mark_command_and_immediate_deactivate) {
  Session s;
  s.load(L"abcdef");
  s.set_point(2);
  s.keys(L"C-SPC");
  CHECK_EQ(s.message(), std::wstring(L"Mark set"));
  CHECK(s.ed.buffer().mark_active);
  CHECK_EQ(s.ed.buffer().mark, size_t{2});
  s.keys(L"C-SPC");
  CHECK_EQ(s.message(), std::wstring(L"Mark deactivated"));
  CHECK(!s.ed.buffer().mark_active);
  s.keys(L"C-SPC C-f C-f");
  CHECK(s.ed.buffer().mark_active);  // movement keeps the region
}

TEST(cmd_exchange_point_and_mark) {
  Session s;
  s.load(L"abcdef");
  s.keys(L"C-x C-x");
  CHECK_EQ(s.message(), std::wstring(L"No mark set in this buffer"));
  s.set_point(1);
  s.keys(L"C-SPC C-SPC");  // mark set, then deactivated
  s.keys(L"C-e");
  s.keys(L"C-x C-x");
  CHECK_EQ(s.point(), size_t{1});
  CHECK_EQ(s.ed.buffer().mark, size_t{6});
  CHECK(s.ed.buffer().mark_active);
}

TEST(cmd_mark_whole_buffer) {
  Session s;
  s.load(L"abc\ndef");
  s.set_point(3);
  s.keys(L"C-x h");
  CHECK_EQ(s.point(), size_t{0});
  CHECK_EQ(s.ed.buffer().mark, size_t{7});
  CHECK(s.ed.buffer().mark_active);
}

TEST(cmd_select_all_has_no_key_and_selects_everything) {
  Session s;
  s.load(L"abc\ndef");
  s.ed.execute_command("select-all");
  CHECK_EQ(s.point(), size_t{7});
  CHECK_EQ(s.ed.buffer().mark, size_t{0});
  CHECK(s.ed.buffer().mark_active);
}

TEST(cmd_keyboard_quit_deactivates_mark) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-SPC C-f");
  s.keys(L"C-g");
  CHECK(!s.ed.buffer().mark_active);
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
}

TEST(mode_line_shows_state) {
  Session s;
  CHECK_EQ(s.ed.mode_line(), std::wstring(L"--- untitled  UTF-8 LF  L1 C0"));
  s.load(L"ab\n\x3042x");
  CHECK_EQ(s.ed.mode_line(), std::wstring(L"--- t.txt  UTF-8 LF  L1 C0"));
  s.keys(L"C-n C-e");
  CHECK_EQ(s.ed.mode_line(), std::wstring(L"--- t.txt  UTF-8 LF  L2 C3"));
  s.type(L"y");
  CHECK_EQ(s.ed.mode_line(), std::wstring(L"-** t.txt  UTF-8 LF  L2 C4"));
  s.keys(L"C-x C-s C-x C-q");
  CHECK_EQ(s.ed.mode_line(), std::wstring(L"-%% t.txt  UTF-8 LF  L2 C4"));

  Session b;
  b.host.add_file(L"C:\\work\\b.txt", "\xEF\xBB\xBFx\r\ny\r\n");
  b.ed.request_open(L"C:\\work\\b.txt");
  CHECK_EQ(b.ed.mode_line(), std::wstring(L"--- b.txt  UTF-8 BOM CRLF  L1 C0"));
}

TEST(echo_area_shows_messages_and_minibuffer) {
  Session s;
  s.load(L"x");
  s.keys(L"C-b");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"Beginning of buffer"));
  s.keys(L"C-f");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring());
  s.keys(L"C-x C-f");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"Find file: C:\\work\\"));
}
