// Regression tests for the code-review findings of M1a ([R1]-[R13]).

#include <string>

#include "fake_host.h"
#include "unicode.h"

using test::Session;

// ---- [R1] host entry points end the typing group and the goal column ----

TEST(r1_set_point_ends_typing_group) {
  Session s;
  s.load(L"0123456789\nabcdefghij\n");
  s.type(L"XYZ");
  s.set_point(15);
  s.type(L"PQR");
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"XYZ0123456789\nabcdefghij\n"));
  CHECK_EQ(s.point(), size_t{15});
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"0123456789\nabcdefghij\n"));
}

TEST(r1_set_point_resets_goal_column) {
  Session s;
  s.load(L"0123456789\nab\n0123456789\n");
  s.set_point(8);
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{13});
  s.set_point(0);
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{11});
}

TEST(r1_insert_text_does_not_merge_with_typing_before_or_after) {
  Session s;
  s.load(L"");
  s.type(L"ab");
  s.ed.insert_text(L"X");
  s.type(L"cd");
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"abX"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring());
}

TEST(r1_insert_text_resets_goal_column) {
  Session s;
  s.load(L"0123456789\nab\n0123456789\n");
  s.set_point(8);
  s.keys(L"C-n");  // goal column 8, point at the end of "ab"
  s.ed.insert_text(L"c");
  s.keys(L"C-n");  // column 3 now
  CHECK_EQ(s.point(), size_t{18});
}

TEST(r1_execute_command_ends_typing_group_and_goal_column) {
  Session s;
  s.load(L"0123456789\nab\n0123456789\n");
  s.type(L"ab");
  s.ed.execute_command("backward-char");
  s.type(L"c");
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"ab0123456789\nab\n0123456789\n"));
  s.keys(L"C-/");
  s.set_point(8);
  s.keys(L"C-n");
  s.ed.execute_command("backward-char");
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{15});  // column 1, not the old goal 8
}

// ---- [R2] scroll-up stops with the last line visible ----

TEST(r2_scroll_up_stops_with_last_line_visible) {
  Session s;
  s.host.lines = 10;
  std::wstring text;
  for (int i = 0; i < 31; ++i) text += (i > 0 ? L"\n" : L"") + std::to_wstring(i);
  s.load(text);
  s.keys(L"C-v");
  CHECK_EQ(s.ed.top_line(), size_t{8});
  s.keys(L"C-v");
  CHECK_EQ(s.ed.top_line(), size_t{16});
  s.keys(L"C-v");
  CHECK_EQ(s.ed.top_line(), size_t{21});  // last line (30) at the bottom
  const size_t point_before = s.point();
  s.keys(L"C-v");
  CHECK_EQ(s.ed.top_line(), size_t{21});
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
  CHECK_EQ(s.point(), point_before);
  s.keys(L"M-v");
  CHECK_EQ(s.ed.top_line(), size_t{13});
  s.keys(L"M-v M-v");
  CHECK_EQ(s.ed.top_line(), size_t{0});
  s.keys(L"M-v");
  CHECK_EQ(s.message(), std::wstring(L"Beginning of buffer"));
}

TEST(r2_scroll_up_in_short_file_keeps_top) {
  Session s;
  s.host.lines = 24;
  s.load(L"a\nb\nc");
  s.keys(L"<next>");
  CHECK_EQ(s.ed.top_line(), size_t{0});
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
  CHECK_EQ(s.point(), size_t{0});
}

// ---- [R3] inserted text is normalised ----

TEST(r3_insert_text_normalizes_cr) {
  Session s;
  s.load(L"");
  s.ed.insert_text(L"x\r\ny\rz");
  CHECK_EQ(s.text(), std::wstring(L"x\ny\nz"));
}

TEST(r3_yank_normalizes_cr) {
  Session s;
  s.load(L"");
  s.host.clipboard = L"x\r\ny\r";
  s.host.clipboard_has_text = true;
  s.keys(L"C-y");
  CHECK_EQ(s.text(), std::wstring(L"x\ny\n"));
}

TEST(r3_minibuffer_input_stops_at_first_line_break) {
  Session s;
  s.load(L"main");
  s.keys(L"C-x C-f C-a C-k");
  s.host.clipboard = L"C:\\work\\a.txt\r\nsecond line";
  s.host.clipboard_has_text = true;
  s.keys(L"C-y");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\a.txt"));
  s.keys(L"C-a C-k");
  s.ed.insert_text(L"ab\ncd");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"ab"));
  s.keys(L"C-o C-q C-j");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"ab"));
}

TEST(r3_quoted_insert_of_carriage_return_is_refused) {
  Session s;
  s.load(L"ab");
  s.keys(L"C-q RET");
  CHECK_EQ(s.message(), std::wstring(L"Cannot insert a carriage return"));
  s.keys(L"C-q C-m");
  CHECK_EQ(s.message(), std::wstring(L"Cannot insert a carriage return"));
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  CHECK_EQ(s.ed.buffer().undo.undo_count(), size_t{0});
  CHECK(!s.modified());
}

// ---- [R4] empty regions do not touch the clipboard ----

TEST(r4_empty_region_keeps_clipboard_and_undo) {
  Session s;
  s.load(L"abc");
  s.host.clipboard = L"precious";
  s.host.clipboard_has_text = true;
  s.keys(L"C-f C-SPC");
  s.ed.execute_command("copy");
  CHECK_EQ(s.message(), std::wstring(L"Region is empty"));
  s.keys(L"M-w");
  CHECK_EQ(s.message(), std::wstring(L"Region is empty"));
  s.keys(L"C-SPC C-w");
  CHECK_EQ(s.message(), std::wstring(L"Region is empty"));
  s.ed.execute_command("cut");
  CHECK_EQ(s.message(), std::wstring(L"Region is empty"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"precious"));
  CHECK_EQ(s.ed.buffer().undo.undo_count(), size_t{0});
  CHECK_EQ(s.text(), std::wstring(L"abc"));
}

// ---- [R5] clipboard failures ----

TEST(r5_kill_with_failing_clipboard_keeps_text) {
  Session s;
  s.load(L"abc def");
  s.host.fail_clipboard = true;
  s.keys(L"C-k");
  CHECK_EQ(s.message(), std::wstring(L"Cannot write to the clipboard"));
  s.keys(L"M-d");
  CHECK_EQ(s.message(), std::wstring(L"Cannot write to the clipboard"));
  s.keys(L"C-SPC C-e C-w");
  CHECK_EQ(s.message(), std::wstring(L"Cannot write to the clipboard"));
  CHECK_EQ(s.text(), std::wstring(L"abc def"));
  CHECK_EQ(s.ed.buffer().undo.undo_count(), size_t{0});
  CHECK(!s.modified());
}

TEST(r5_copy_with_failing_clipboard_shows_message) {
  Session s;
  s.load(L"abc");
  s.host.fail_clipboard = true;
  s.keys(L"C-SPC C-e M-w");
  CHECK_EQ(s.message(), std::wstring(L"Cannot write to the clipboard"));
  s.ed.execute_command("copy");
  CHECK_EQ(s.message(), std::wstring(L"Cannot write to the clipboard"));
  CHECK(!s.host.clipboard_has_text);
}

// ---- [R6] file_kind errors ----

TEST(r6_find_file_on_inaccessible_path_is_an_error) {
  Session s;
  s.host.add_error_path(L"C:\\work\\locked.txt");
  s.ed.request_open(L"C:\\work\\locked.txt");
  CHECK_EQ(s.message(), std::wstring(L"Cannot access C:\\work\\locked.txt"));
  CHECK_EQ(s.ed.buffer().path, std::wstring());
  s.load(L"keep");
  s.ed.request_open(L"C:\\work\\locked.txt");
  CHECK_EQ(s.message(), std::wstring(L"Cannot access C:\\work\\locked.txt"));
  CHECK_EQ(s.host.open_dialogs, 0);
  CHECK_EQ(s.text(), std::wstring(L"keep"));
}

TEST(r6_write_file_to_inaccessible_path_does_not_write) {
  Session s;
  s.load(L"keep");
  s.type(L"x");
  s.host.add_error_path(L"C:\\work\\locked.txt");
  s.keys(L"C-x C-w");
  s.type(L"locked.txt");
  s.keys(L"RET");
  CHECK_EQ(s.message(), std::wstring(L"Cannot access C:\\work\\locked.txt"));
  CHECK(s.host.written.empty());
  CHECK_EQ(s.host.yes_no_dialogs, 0);
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\t.txt"));
  CHECK(s.modified());
}

TEST(r6_save_buffer_to_inaccessible_path_does_not_write) {
  Session s;
  s.load(L"keep");
  s.type(L"x");
  s.host.add_error_path(L"C:\\work\\t.txt");
  s.keys(L"C-x C-s");
  CHECK_EQ(s.message(), std::wstring(L"Cannot access C:\\work\\t.txt"));
  CHECK(s.host.written.empty());
  CHECK(s.modified());
  s.host.save_answers.push_back(geditor::SaveChoice::kSave);
  s.keys(L"C-x C-c");
  CHECK_EQ(s.host.quit_calls, 0);  // the save failed, so quit stops
}

// ---- [R13] fake host fidelity ----

TEST(r13_fake_host_write_fails_without_parent_folder) {
  Session s;
  s.load(L"keep");
  s.type(L"x");
  s.keys(L"C-x C-w C-a C-k");
  s.type(L"C:\\missing\\a.txt");
  s.keys(L"RET");
  CHECK_EQ(s.message(), std::wstring(L"Cannot write C:\\missing\\a.txt: the folder does not exist"));
  CHECK(!s.host.has_file(L"C:\\missing\\a.txt"));
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\t.txt"));
  CHECK(s.modified());
}

TEST(r13_fake_host_scripted_failures) {
  test::FakeHost host;
  host.add_error_path(L"C:\\Work\\X.txt");
  CHECK(host.file_kind(L"c:\\work\\x.txt") == geditor::FileKind::kError);
  host.fail_clipboard = true;
  CHECK(!host.set_clipboard(L"a"));
  std::wstring error;
  CHECK(!host.write_file(L"C:\\nowhere\\a.txt", "x", error));
  CHECK(host.write_file(L"C:\\work\\a.txt", "x", error));
}

// ---- [R7] the mark keeps its logical position ----

TEST(r7_case_conversion_keeps_mark_after_range) {
  Session s;
  s.load(L"foo bar");
  s.set_point(3);
  s.keys(L"C-SPC M-b M-u");
  CHECK_EQ(s.text(), std::wstring(L"FOO bar"));
  CHECK_EQ(s.ed.buffer().mark, size_t{3});
}

TEST(r7_case_conversion_keeps_mark_inside_and_before_range) {
  Session s;
  s.load(L"x foo bar");
  s.set_point(4);
  s.keys(L"C-SPC C-SPC");  // inactive mark inside "foo"
  s.set_point(2);
  s.keys(L"M-c");
  CHECK_EQ(s.text(), std::wstring(L"x Foo bar"));
  CHECK_EQ(s.ed.buffer().mark, size_t{4});
  s.set_point(0);
  s.keys(L"C-SPC C-SPC");  // mark before the range
  s.set_point(6);
  s.keys(L"M-u");
  CHECK_EQ(s.ed.buffer().mark, size_t{0});
}

TEST(r7_transpose_chars_keeps_mark) {
  Session s;
  s.load(L"abcd");
  s.set_point(2);
  s.keys(L"C-SPC C-SPC");
  s.set_point(1);
  s.keys(L"C-t");
  CHECK_EQ(s.text(), std::wstring(L"bacd"));
  CHECK_EQ(s.ed.buffer().mark, size_t{2});
}

// ---- [R8] a trailing separator names a directory ----

TEST(r8_find_file_nonexistent_directory_is_an_error) {
  Session s;
  s.ed.request_open(L"C:\\newdir\\");
  CHECK_EQ(s.message(), std::wstring(L"No such directory: C:\\newdir"));
  CHECK_EQ(s.ed.buffer().path, std::wstring());
  s.load(L"keep");
  s.ed.request_open(L"C:\\newdir/");
  CHECK_EQ(s.message(), std::wstring(L"No such directory: C:\\newdir"));
  CHECK_EQ(s.host.open_dialogs, 0);
  CHECK_EQ(s.text(), std::wstring(L"keep"));
}

TEST(r8_write_file_nonexistent_directory_is_an_error) {
  Session s;
  s.load(L"keep");
  s.keys(L"C-x C-w");
  s.type(L"newdir\\");
  s.keys(L"RET");
  CHECK_EQ(s.message(), std::wstring(L"No such directory: C:\\work\\newdir"));
  CHECK(s.host.written.empty());
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\t.txt"));
}

// ---- [R9] completion keeps surrogate pairs whole ----

TEST(r9_completion_never_splits_surrogate_pair) {
  Session s;
  s.host.add_file(L"C:\\work\\\U00020BB7" L"a", "1");
  s.host.add_file(L"C:\\work\\\U00020B9F" L"b", "2");
  s.keys(L"C-x C-f TAB");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\"));
  CHECK_EQ(s.message(), std::wstring(L"{\U00020B9F" L"b | \U00020BB7" L"a}"));
}

// ---- [R10] the candidate list is capped ----

TEST(r10_candidate_list_is_capped) {
  Session s;
  for (int i = 0; i < 5000; ++i) {
    wchar_t name[32];
    swprintf(name, 32, L"C:\\work\\f%04d.txt", i);
    s.host.add_file(name, "");
  }
  s.keys(L"C-x C-f");
  s.type(L"f");
  s.keys(L"TAB");
  CHECK(s.message().size() <= 1100);
  CHECK(s.message().find(L"f0000.txt") != std::wstring::npos);
  CHECK(s.message().find(L"5000 candidates") != std::wstring::npos);
}

// ---- [R11] prolonged sound mark and voiced marks continue a word ----

TEST(r11_prolonged_sound_mark_continues_word) {
  Session s;
  s.load(L"\x3089\x30FC\x3081\x3093 \x30E9\x30FC\x30E1\x30F3");  // "ramen" in hiragana and katakana
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{4});
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{9});
  s.keys(L"M-b");
  CHECK_EQ(s.point(), size_t{5});
  s.keys(L"M-<");
  s.keys(L"M-d");
  CHECK_EQ(s.text(), std::wstring(L" \x30E9\x30FC\x30E1\x30F3"));
}

TEST(r11_voiced_sound_marks_continue_word) {
  Session s;
  s.load(L"\x304B\x3099\x304D\x309A x");  // combining voiced / semi-voiced marks
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{4});
  s.host.open_answers.push_back(geditor::OpenChoice::kReplace);
  s.load(L"\x30AB\x309B\x30AD\x309C y", L"C:\\work\\k.txt");  // spacing marks after katakana
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{4});
}

// ---- [R12] case map for Greek and Cyrillic ----

TEST(r12_greek_dialytika_case_pairs) {
  CHECK_EQ(geditor::to_upper(0x03CA), char32_t{0x03AA});
  CHECK_EQ(geditor::to_upper(0x03CB), char32_t{0x03AB});
  CHECK_EQ(geditor::to_lower(0x03AA), char32_t{0x03CA});
  CHECK_EQ(geditor::to_lower(0x03AB), char32_t{0x03CB});
}

TEST(r12_greek_and_cyrillic_extended_pairs) {
  CHECK_EQ(geditor::to_upper(0x03C2), char32_t{0x03A3});  // final sigma
  CHECK_EQ(geditor::to_upper(0x03D9), char32_t{0x03D8});
  CHECK_EQ(geditor::to_lower(0x03FD), char32_t{0x037B});
  CHECK_EQ(geditor::to_upper(0x04CF), char32_t{0x04C0});
  CHECK_EQ(geditor::to_lower(0x04C1), char32_t{0x04C2});
  CHECK_EQ(geditor::to_upper(0x04FF), char32_t{0x04FE});
  CHECK_EQ(geditor::to_upper(0x00B5), char32_t{0x039C});  // micro sign
}
