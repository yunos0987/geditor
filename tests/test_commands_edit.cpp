// Delete / kill / yank, input and undo-key commands, driven by keys ([M1-2]).

#include "fake_host.h"

using test::Session;

// ---- Delete and kill ----

TEST(cmd_delete_char) {
  Session s;
  s.load(L"a\U0001F600b");
  s.keys(L"C-d");
  CHECK_EQ(s.text(), std::wstring(L"\U0001F600b"));
  s.keys(L"<delete>");
  CHECK_EQ(s.text(), std::wstring(L"b"));  // the pair is deleted as one
  s.keys(L"C-e C-d");
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
}

TEST(cmd_delete_char_deletes_active_region) {
  Session s;
  s.load(L"abcdef");
  s.set_point(1);
  s.keys(L"C-SPC C-f C-f C-d");
  CHECK_EQ(s.text(), std::wstring(L"adef"));
  CHECK(!s.ed.buffer().mark_active);
  CHECK(!s.host.clipboard_has_text);  // deleting is not killing
}

TEST(cmd_delete_backward_char) {
  Session s;
  s.load(L"ab\U0001F600");
  s.keys(L"M->");
  s.keys(L"DEL");
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  s.keys(L"DEL");
  CHECK_EQ(s.text(), std::wstring(L"a"));
  s.keys(L"DEL DEL");
  CHECK_EQ(s.text(), std::wstring());
  CHECK_EQ(s.message(), std::wstring(L"Beginning of buffer"));
}

TEST(cmd_kill_line_kills_rest_then_newline) {
  Session s;
  s.load(L"abc\ndef");
  s.set_point(1);
  s.keys(L"C-k");
  CHECK_EQ(s.text(), std::wstring(L"a\ndef"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"bc"));
  s.keys(L"C-k");
  CHECK_EQ(s.text(), std::wstring(L"adef"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"\n"));
  s.keys(L"C-e C-k");
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
}

TEST(cmd_kill_word) {
  Session s;
  s.load(L"foo bar baz");
  s.set_point(3);
  s.keys(L"M-d");
  CHECK_EQ(s.text(), std::wstring(L"foo baz"));
  CHECK_EQ(s.host.clipboard, std::wstring(L" bar"));
}

TEST(cmd_backward_kill_word) {
  Session s;
  s.load(L"foo bar baz");
  s.set_point(7);
  s.keys(L"M-DEL");
  CHECK_EQ(s.text(), std::wstring(L"foo  baz"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"bar"));
  CHECK_EQ(s.point(), size_t{4});
}

TEST(cmd_kill_region_uses_mark_even_if_inactive) {
  Session s;
  s.load(L"abcdef");
  s.keys(L"C-w");
  CHECK_EQ(s.message(), std::wstring(L"The mark is not set now, so there is no region"));
  CHECK_EQ(s.text(), std::wstring(L"abcdef"));
  s.set_point(1);
  s.keys(L"C-SPC C-SPC");  // mark at 1, inactive
  s.set_point(4);
  s.keys(L"C-w");
  CHECK_EQ(s.text(), std::wstring(L"aef"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"bcd"));
  CHECK_EQ(s.point(), size_t{1});
}

TEST(cmd_kill_ring_save_copies_and_deactivates) {
  Session s;
  s.load(L"abcdef");
  s.keys(L"M-w");
  CHECK_EQ(s.message(), std::wstring(L"The mark is not set now, so there is no region"));
  s.keys(L"C-SPC C-f C-f C-f M-w");
  CHECK_EQ(s.text(), std::wstring(L"abcdef"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"abc"));
  CHECK(!s.ed.buffer().mark_active);
  CHECK(!s.modified());
}

TEST(cmd_yank_inserts_clipboard_and_sets_mark) {
  Session s;
  s.load(L"xy");
  s.keys(L"C-y");
  CHECK_EQ(s.message(), std::wstring(L"Clipboard is empty"));
  s.host.set_clipboard(L"one\ntwo");
  s.set_point(1);
  s.keys(L"C-y");
  CHECK_EQ(s.text(), std::wstring(L"xone\ntwoy"));
  CHECK_EQ(s.point(), size_t{8});
  CHECK_EQ(s.ed.buffer().mark, size_t{1});
}

TEST(cmd_copy_cut_paste_aliases) {
  Session s;
  s.load(L"hello world");
  s.ed.execute_command("select-all");
  s.ed.execute_command("copy");
  CHECK_EQ(s.host.clipboard, std::wstring(L"hello world"));
  CHECK_EQ(s.text(), std::wstring(L"hello world"));
  s.set_point(5);
  s.keys(L"C-SPC M-f");
  s.ed.execute_command("cut");
  CHECK_EQ(s.text(), std::wstring(L"hello"));
  CHECK_EQ(s.host.clipboard, std::wstring(L" world"));
  s.keys(L"C-a");
  s.ed.execute_command("paste");
  CHECK_EQ(s.text(), std::wstring(L" worldhello"));
}

TEST(cmd_kill_in_read_only_buffer_copies_but_keeps_text) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-x C-q C-k");
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"abc"));
  CHECK_EQ(s.message(), std::wstring(L"Buffer is read-only"));
}

// ---- Input ----

TEST(cmd_self_insert_printable_characters) {
  Session s;
  s.load(L"");
  s.type(L"a \x3042\U0001F600Z");
  CHECK_EQ(s.text(), std::wstring(L"a \x3042\U0001F600Z"));
  CHECK_EQ(s.point(), size_t{6});
  s.keys(L"SPC");
  CHECK_EQ(s.text(), std::wstring(L"a \x3042\U0001F600Z "));
}

TEST(cmd_newline_ret_and_c_j) {
  Session s;
  s.load(L"ab");
  s.set_point(1);
  s.keys(L"RET");
  CHECK_EQ(s.text(), std::wstring(L"a\nb"));
  s.keys(L"C-j");
  CHECK_EQ(s.text(), std::wstring(L"a\n\nb"));
  CHECK_EQ(s.point(), size_t{3});
}

TEST(cmd_open_line_keeps_point) {
  Session s;
  s.load(L"ab");
  s.set_point(1);
  s.keys(L"C-o");
  CHECK_EQ(s.text(), std::wstring(L"a\nb"));
  CHECK_EQ(s.point(), size_t{1});
}

TEST(cmd_insert_tab) {
  Session s;
  s.load(L"ab");
  s.set_point(1);
  s.keys(L"TAB");
  CHECK_EQ(s.text(), std::wstring(L"a\tb"));
  CHECK_EQ(s.ed.point_column(), 4);
}

TEST(cmd_quoted_insert) {
  Session s;
  s.load(L"");
  s.keys(L"C-q");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring(L"C-q-"));
  s.keys(L"C-l");
  CHECK_EQ(s.text(), std::wstring(L"\x000C"));
  s.keys(L"C-q TAB C-q C-g C-q DEL C-q a C-q C-x");
  CHECK_EQ(s.text(), std::wstring(L"\x000C\t\x0007\x007F" L"a\x0018"));
  s.keys(L"C-q <f1>");
  CHECK_EQ(s.message(), std::wstring(L"<f1> cannot be inserted"));
}

TEST(cmd_transpose_chars_emacs_semantics) {
  Session s;
  s.load(L"abc\nde");
  s.set_point(1);
  s.keys(L"C-t");  // middle: swap around point, move forward
  CHECK_EQ(s.text(), std::wstring(L"bac\nde"));
  CHECK_EQ(s.point(), size_t{2});
  s.keys(L"C-e C-t");  // end of line: swap the two chars before point
  CHECK_EQ(s.text(), std::wstring(L"bca\nde"));
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"C-n C-a C-t");  // beginning of a line: swaps the newline
  CHECK_EQ(s.text(), std::wstring(L"bcad\ne"));
  CHECK_EQ(s.point(), size_t{5});
  s.keys(L"M-< C-t");
  CHECK_EQ(s.message(), std::wstring(L"Beginning of buffer"));
}

TEST(cmd_transpose_chars_keeps_surrogate_pairs) {
  Session s;
  s.load(L"a\U0001F600");
  s.keys(L"C-e C-t");
  CHECK_EQ(s.text(), std::wstring(L"\U0001F600" L"a"));
}

TEST(cmd_upcase_word) {
  Session s;
  s.load(L"hello caf\x00E9 \xFF41\xFF42 x");
  s.keys(L"M-u");
  CHECK_EQ(s.text(), std::wstring(L"HELLO caf\x00E9 \xFF41\xFF42 x"));
  CHECK_EQ(s.point(), size_t{5});
  s.keys(L"M-u M-u");
  CHECK_EQ(s.text(), std::wstring(L"HELLO CAF\x00C9 \xFF21\xFF22 x"));
}

TEST(cmd_downcase_word) {
  Session s;
  s.load(L"HELLO \x00C9" L"COLE \xFF21\xFF22");
  s.keys(L"M-l M-l M-l");
  CHECK_EQ(s.text(), std::wstring(L"hello \x00E9" L"cole \xFF41\xFF42"));
}

TEST(cmd_capitalize_word) {
  Session s;
  s.load(L"hELLO \x00E9" L"COLE \xFF41\xFF42\xFF43");
  s.keys(L"M-c M-c M-c");
  CHECK_EQ(s.text(), std::wstring(L"Hello \x00C9" L"cole \xFF21\xFF42\xFF43"));
}

// ---- Undo keys ----

TEST(cmd_undo_keys_c_slash_c_underscore_c_x_u) {
  Session s;
  s.load(L"");
  s.type(L"a");
  s.keys(L"C-f");  // breaks the typing group
  s.type(L"b");
  s.keys(L"C-f");
  s.type(L"c");
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  s.keys(L"C-_");
  CHECK_EQ(s.text(), std::wstring(L"a"));
  s.keys(L"C-x u");
  CHECK_EQ(s.text(), std::wstring());
}

TEST(cmd_redo_keys_c_question_and_c_m_underscore) {
  Session s;
  s.load(L"");
  s.type(L"a");
  s.keys(L"C-f");
  s.type(L"b");
  s.keys(L"C-/ C-/");
  CHECK_EQ(s.text(), std::wstring());
  s.keys(L"C-?");
  CHECK_EQ(s.text(), std::wstring(L"a"));
  CHECK_EQ(s.message(), std::wstring(L"Redo"));
  s.keys(L"C-M-_");
  CHECK_EQ(s.text(), std::wstring(L"ab"));
}

TEST(cmd_edit_deactivates_mark) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-SPC C-f");
  s.type(L"x");
  CHECK(!s.ed.buffer().mark_active);
}
