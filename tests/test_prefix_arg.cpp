// Prefix argument ([M2-6], decision D6) and command sequences (decision D7).

#include <string>

#include "fake_host.h"

// ---- Forms of the argument ----

// Since 2026-11-17 C-u is backward-kill-line by default and
// universal-argument has no default key: the tests of its forms bind it to
// C-u in the settings, which gives back the C-u of before.
constexpr char kUniversalArgumentOnCU[] = R"({"keys": {"C-u": "universal-argument"}})";

TEST(prefix_c_u_is_four) {
  test::Session s;
  s.load(L"0123456789abcdefghij");
  s.settings(kUniversalArgumentOnCU);
  CHECK(s.ed.settings_errors().empty());
  s.keys(L"C-u C-f");
  CHECK_EQ(s.point(), size_t{4});
}

TEST(prefix_c_u_c_u_is_sixteen) {
  test::Session s;
  s.load(L"0123456789abcdefghij");
  s.settings(kUniversalArgumentOnCU);
  s.keys(L"C-u C-u C-f");
  CHECK_EQ(s.point(), size_t{16});
}

TEST(prefix_c_u_digits) {
  test::Session s;
  s.load(L"0123456789abcdefghij");
  s.settings(kUniversalArgumentOnCU);
  s.keys(L"C-u 1 2 C-f");
  CHECK_EQ(s.point(), size_t{12});
}

TEST(prefix_c_u_minus_is_minus_one) {
  test::Session s;
  s.load(L"0123456789");
  s.settings(kUniversalArgumentOnCU);
  s.set_point(5);
  s.keys(L"C-u - C-f");
  CHECK_EQ(s.point(), size_t{4});
  s.keys(L"C-u - 3 C-f");
  CHECK_EQ(s.point(), size_t{1});
}

TEST(prefix_meta_digit) {
  test::Session s;
  s.load(L"0123456789");
  s.keys(L"M-3 C-f");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"M-1 2 C-b");  // digits continue after M-<digit>
  CHECK_EQ(s.point(), size_t{0});
  CHECK_EQ(s.message(), std::wstring(L"Beginning of buffer"));
}

TEST(prefix_meta_minus) {
  test::Session s;
  s.load(L"0123456789");
  s.set_point(5);
  s.keys(L"M-- C-f");
  CHECK_EQ(s.point(), size_t{4});
  s.keys(L"M-- 2 C-f");
  CHECK_EQ(s.point(), size_t{2});
}

TEST(prefix_esc_digit_is_meta_digit) {
  test::Session s;
  s.load(L"0123456789");
  s.keys(L"ESC 3 C-f");
  CHECK_EQ(s.point(), size_t{3});
}

// ---- Commands that use it ----

TEST(prefix_forward_char_stops_at_end_of_buffer) {
  test::Session s;
  s.load(L"abc");
  s.keys(L"M-5 C-f");
  CHECK_EQ(s.point(), size_t{3});
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
}

TEST(prefix_next_line) {
  test::Session s;
  s.load(L"a0\na1\na2\na3\na4\n");
  s.set_point(1);
  s.keys(L"M-3 C-n");
  CHECK_EQ(s.ed.point_line(), size_t{3});
  CHECK_EQ(s.ed.point_column(), 1);
  s.keys(L"M-- C-n");  // negative: up
  CHECK_EQ(s.ed.point_line(), size_t{2});
  s.keys(L"M-9 C-n");  // stops at the end with the message
  CHECK_EQ(s.point(), s.ed.buffer().text.length());
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
}

TEST(prefix_keeps_goal_column_across_c_u) {
  test::Session s;
  s.load(L"abcdef\nx\nabcdef\nabcdef\n");
  s.set_point(4);
  s.keys(L"C-n");  // line 1 is short: column 1
  s.keys(L"M-2 C-n");
  CHECK_EQ(s.ed.point_line(), size_t{3});
  CHECK_EQ(s.ed.point_column(), 4);  // the goal column survived M-2
}

TEST(prefix_delete_char_positive) {
  test::Session s;
  s.load(L"abcdef");
  s.keys(L"M-3 C-d");
  CHECK_EQ(s.text(), std::wstring(L"def"));
  s.keys(L"M-9 C-d");  // fewer characters left: nothing deleted
  CHECK_EQ(s.text(), std::wstring(L"def"));
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
}

TEST(prefix_delete_char_negative) {
  test::Session s;
  s.load(L"abcdef");
  s.set_point(4);
  s.keys(L"M-- 2 C-d");
  CHECK_EQ(s.text(), std::wstring(L"abef"));
  CHECK_EQ(s.point(), size_t{2});
  s.keys(L"M-- DEL");  // delete-backward-char with -1 deletes forward
  CHECK_EQ(s.text(), std::wstring(L"abf"));
  s.keys(L"M-2 DEL");
  CHECK_EQ(s.text(), std::wstring(L"f"));
}

TEST(prefix_kill_line) {
  test::Session s;
  s.load(L"one\ntwo\nthree\nfour\n");
  s.keys(L"M-2 C-k");
  CHECK_EQ(s.text(), std::wstring(L"three\nfour\n"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"one\ntwo\n"));
  s.keys(L"C-e M-0 C-k");  // back to the start of the line
  CHECK_EQ(s.text(), std::wstring(L"\nfour\n"));
  s.keys(L"M-1 C-k");  // with an argument the line end goes too
  CHECK_EQ(s.text(), std::wstring(L"four\n"));
}

TEST(prefix_kill_word_and_backward) {
  test::Session s;
  s.load(L"one two three four");
  s.keys(L"M-2 M-d");
  CHECK_EQ(s.text(), std::wstring(L" three four"));
  s.keys(L"C-e M-2 M-DEL");
  CHECK_EQ(s.text(), std::wstring(L" "));
}

TEST(prefix_self_insert) {
  test::Session s;
  s.load(L"x");
  s.keys(L"M-3");
  s.type(L"a");
  CHECK_EQ(s.text(), std::wstring(L"aaax"));
  s.keys(L"M--");
  s.type(L"b");
  CHECK_EQ(s.text(), std::wstring(L"aaax"));
  CHECK_EQ(s.message(), std::wstring(L"Repeat count must be positive"));
  s.keys(L"M-1 0");  // M-1 0 a: ten copies
  s.type(L"c");
  CHECK_EQ(s.text(), std::wstring(L"aaacccccccccc" L"x"));
}

TEST(prefix_newline_open_line_and_tab) {
  test::Session s;
  s.load(L"ab");
  s.set_point(1);
  s.keys(L"M-2 RET");
  CHECK_EQ(s.text(), std::wstring(L"a\n\nb"));
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"M-2 C-o");
  CHECK_EQ(s.text(), std::wstring(L"a\n\n\n\nb"));
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"M-3 TAB");
  CHECK_EQ(s.text(), std::wstring(L"a\n\n\t\t\t\n\nb"));
}

TEST(prefix_undo_and_redo_steps) {
  test::Session s;
  s.load(L"");
  s.type(L"a");
  s.keys(L"C-f");  // ends the typing group
  s.type(L"b");
  s.keys(L"C-f");
  s.type(L"c");
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  s.keys(L"M-2 C-/");
  CHECK_EQ(s.text(), std::wstring(L"a"));
  s.keys(L"M-2 C-?");
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  s.keys(L"M-5 C-/");  // more than the history: stops with the message
  CHECK_EQ(s.text(), std::wstring());
  CHECK_EQ(s.message(), std::wstring(L"No further undo information"));
}

TEST(prefix_transpose_chars) {
  test::Session s;
  s.load(L"abcde");
  s.set_point(1);
  s.keys(L"M-3 C-t");  // 'a' moves three characters forward
  CHECK_EQ(s.text(), std::wstring(L"bcdae"));
  CHECK_EQ(s.point(), size_t{4});
  s.keys(L"M-- C-t");  // and one back
  CHECK_EQ(s.text(), std::wstring(L"bcade"));
  CHECK_EQ(s.point(), size_t{3});
}

TEST(prefix_scroll_by_lines) {
  test::Session s;
  std::wstring text;
  for (int i = 0; i < 40; ++i) text += L"l" + std::to_wstring(i) + L"\n";
  s.load(text);
  s.keys(L"M-3 C-v");
  CHECK_EQ(s.ed.top_line(), size_t{3});
  s.keys(L"M-- C-v");  // negative: the other way
  CHECK_EQ(s.ed.top_line(), size_t{2});
  s.keys(L"M-2 M-v");
  CHECK_EQ(s.ed.top_line(), size_t{0});
}

TEST(prefix_is_ignored_by_other_commands) {
  test::Session s;
  s.load(L"abc def");
  s.keys(L"M-3 C-e");
  CHECK_EQ(s.point(), size_t{7});
  s.keys(L"C-f");  // the argument was used up
  CHECK_EQ(s.point(), size_t{7});
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
}

TEST(prefix_pending_echo) {
  test::Session s;
  s.load(L"abc");
  s.settings(kUniversalArgumentOnCU);
  s.keys(L"C-u");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring(L"C-u-"));
  s.keys(L"1 2");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring(L"C-u 1 2-"));
  s.keys(L"C-x");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring(L"C-u 1 2 C-x-"));
  s.keys(L"C-g");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring());
  s.keys(L"M-3");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring(L"M-3-"));
  s.keys(L"C-g");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring());
}

// ---- Command sequences (D7, [C11]) ----

TEST(sequence_prefix_repeats_the_whole_sequence_in_one_undo_group) {
  test::Session s;
  s.load(L"abcdef");
  s.settings("{\"keys\": {\"C-c l\": [[\"insert-text\", \"x\"], [\"forward-char\"]]}}");
  s.keys(L"M-3 C-c l");
  CHECK_EQ(s.text(), std::wstring(L"xaxbxcdef"));
  CHECK_EQ(s.point(), size_t{6});
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"abcdef"));
  CHECK_EQ(s.point(), size_t{0});
}

TEST(sequence_non_positive_count_runs_nothing) {
  test::Session s;
  s.load(L"abcdef");
  s.settings("{\"keys\": {\"C-c l\": [[\"insert-text\", \"x\"]]}}");
  s.keys(L"M-0 C-c l");
  CHECK_EQ(s.text(), std::wstring(L"abcdef"));
  CHECK_EQ(s.message(), std::wstring(L"Repeat count must be positive"));
  s.keys(L"M-- C-c l");
  CHECK_EQ(s.text(), std::wstring(L"abcdef"));
  CHECK_EQ(s.message(), std::wstring(L"Repeat count must be positive"));
  CHECK(!s.modified());
}

TEST(sequence_stops_at_first_failure_and_keeps_earlier_edits) {
  test::Session s;
  s.load(L"ab");
  s.set_point(2);
  s.settings(
      "{\"keys\": {\"C-c f\": [[\"insert-text\", \"X\"], [\"forward-char\"], [\"insert-text\", \"Y\"]]}}");
  s.keys(L"C-c f");
  CHECK_EQ(s.text(), std::wstring(L"abX"));  // Y never ran
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
  s.keys(L"C-/");  // the edits before the failure are one group
  CHECK_EQ(s.text(), std::wstring(L"ab"));
}

TEST(sequence_failure_in_a_repeat_stops_all_repeats) {
  test::Session s;
  s.load(L"abc");
  s.settings("{\"keys\": {\"C-c f\": [[\"forward-char\"], [\"insert-text\", \"-\"]]}}");
  s.keys(L"M-5 C-c f");
  CHECK_EQ(s.text(), std::wstring(L"a-b-c-"));
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"abc"));
}

TEST(sequence_read_only_buffer_fails_with_message) {
  test::Session s;
  s.load(L"abc");
  s.settings("{\"keys\": {\"C-c f\": [[\"end-of-line\"], [\"insert-text\", \"!\"], [\"beginning-of-line\"]]}}");
  s.keys(L"C-x C-q");
  s.keys(L"C-c f");
  CHECK_EQ(s.message(), std::wstring(L"Buffer is read-only"));
  CHECK_EQ(s.point(), size_t{3});  // beginning-of-line did not run
}
