// Opening and saving files: encodings, find-file flow, save-buffer,
// write-file and quit (docs/PLAN.md 2 "open another file").

#include "fake_host.h"

using geditor::OpenChoice;
using geditor::SaveChoice;
using test::Session;

namespace {

// Opens `bytes`, makes and reverts an edit, saves with C-x C-s and returns
// the written bytes.
std::string open_edit_revert_save(const std::string& bytes) {
  Session s;
  s.host.add_file(L"C:\\work\\f.txt", bytes);
  s.ed.request_open(L"C:\\work\\f.txt");
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\f.txt"));
  s.keys(L"M->");
  s.type(L"x");
  s.keys(L"DEL");
  CHECK(s.modified());
  s.keys(L"C-x C-s");
  CHECK(!s.modified());
  // Also write a copy through write-file.
  s.keys(L"C-x C-w");
  s.keys(L"C-a C-k");
  s.type(L"C:\\work\\copy.txt");
  s.keys(L"RET");
  CHECK_EQ(s.host.file(L"C:\\work\\copy.txt"), bytes);
  return s.host.file(L"C:\\work\\f.txt");
}

void open_with_dialog(Session& s, const std::wstring& path) {
  s.host.open_answers.push_back(OpenChoice::kReplace);
  s.ed.request_open(path);
}

}  // namespace

// ---- Encodings ----

TEST(file_round_trip_utf8_lf) {
  const std::string bytes = "first\n\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\nlast\n";
  CHECK_EQ(open_edit_revert_save(bytes), bytes);
}

TEST(file_round_trip_utf8_bom_crlf) {
  const std::string bytes = "\xEF\xBB\xBF" "first\r\n\xF0\x9F\x98\x80\r\n\r\nlast";
  CHECK_EQ(open_edit_revert_save(bytes), bytes);
}

TEST(file_round_trip_empty_file) {
  CHECK_EQ(open_edit_revert_save(""), std::string());
}

TEST(file_round_trip_without_line_end) {
  CHECK_EQ(open_edit_revert_save("no line end"), std::string("no line end"));
}

// M4b (PLAN 2 "undetectable bytes"): bytes that are neither UTF-8 nor
// Shift_JIS open as UTF-8 with U+FFFD and "!" (M4a refused them).
TEST(file_invalid_utf8_opens_with_replacement_characters) {
  Session s;
  s.load(L"keep");
  s.host.add_file(L"C:\\work\\bad.txt", "ab\xFD(");
  open_with_dialog(s, L"C:\\work\\bad.txt");
  CHECK_EQ(s.text(), std::wstring(L"ab\xFFFD("));
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\bad.txt"));
  CHECK_EQ(s.ed.encoding_text(), std::wstring(L"UTF-8!"));
  CHECK(s.message().find(L"not valid UTF-8 or Shift_JIS") != std::wstring::npos);
}

// M1 refused mixed and CR-only line ends; M4a opens them and keeps each
// line's line end (test_m4a_eol.cpp has the rules).
TEST(file_mixed_eol_opens_in_m4a) {
  Session s;
  s.load(L"keep");
  s.host.add_file(L"C:\\work\\mixed.txt", "a\nb\r\nc");
  open_with_dialog(s, L"C:\\work\\mixed.txt");
  CHECK_EQ(s.text(), std::wstring(L"a\nb\nc"));
  CHECK_EQ(s.ed.eol_text(), std::wstring(L"Mixed(CRLF)"));
}

TEST(file_cr_only_opens_in_m4a) {
  Session s;
  s.host.add_file(L"C:\\work\\cr.txt", "a\rb\r");
  s.ed.request_open(L"C:\\work\\cr.txt");  // untitled empty buffer: no dialog
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\cr.txt"));
  CHECK_EQ(s.text(), std::wstring(L"a\nb\n"));
  CHECK_EQ(s.ed.eol_text(), std::wstring(L"CR"));
}

// ---- find-file ----

TEST(find_file_prompt_starts_in_directory_of_current_file) {
  Session s;
  s.keys(L"C-x C-f");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Find file: "));
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\"));  // current directory
  s.keys(L"C-g");
  s.host.add_dir(L"C:\\docs");
  s.load(L"x", L"C:\\docs\\n.txt");
  s.keys(L"C-x C-f");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\docs\\"));
}

TEST(find_file_ret_opens_typed_path) {
  Session s;
  s.host.add_file(L"C:\\work\\a.txt", "alpha");
  s.keys(L"C-x C-f");
  s.type(L"a.txt");
  s.keys(L"RET");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\a.txt"));
  CHECK_EQ(s.text(), std::wstring(L"alpha"));
  CHECK_EQ(s.host.open_dialogs, 0);  // untitled, unmodified, empty: no dialog
}

TEST(find_file_c_g_aborts) {
  Session s;
  s.load(L"keep");
  s.keys(L"C-x C-f");
  s.type(L"other.txt");
  s.keys(L"C-g");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  CHECK_EQ(s.text(), std::wstring(L"keep"));
  CHECK_EQ(s.host.open_dialogs, 0);
}

TEST(find_file_same_path_only_shows_message) {
  Session s;
  s.load(L"keep");
  s.ed.request_open(L"c:\\WORK\\t.txt");
  CHECK_EQ(s.host.open_dialogs, 0);
  CHECK(s.message().find(L"already open") != std::wstring::npos);
}

TEST(find_file_directory_is_an_error) {
  Session s;
  s.load(L"keep");
  s.keys(L"C-x C-f RET");  // the prompt's initial text is the directory
  CHECK_EQ(s.message(), std::wstring(L"C:\\work is a directory"));
  CHECK_EQ(s.host.open_dialogs, 0);
  CHECK_EQ(s.text(), std::wstring(L"keep"));
}

TEST(find_file_untitled_empty_buffer_replaced_without_dialog) {
  Session s;
  s.host.add_file(L"C:\\work\\a.txt", "alpha");
  s.ed.request_open(L"C:\\work\\a.txt");
  CHECK_EQ(s.host.open_dialogs, 0);
  CHECK_EQ(s.text(), std::wstring(L"alpha"));
}

TEST(find_file_untitled_modified_buffer_asks) {
  Session s;
  s.type(L"draft");
  s.host.add_file(L"C:\\work\\a.txt", "alpha");
  s.ed.request_open(L"C:\\work\\a.txt");  // cancelled by default
  CHECK_EQ(s.host.open_dialogs, 1);
  CHECK_EQ(s.text(), std::wstring(L"draft"));
}

TEST(find_file_replace_unmodified_discards_undo_history) {
  Session s;
  s.load(L"one");
  s.type(L"x");
  s.keys(L"C-x C-s");
  s.host.add_file(L"C:\\work\\a.txt", "alpha");
  open_with_dialog(s, L"C:\\work\\a.txt");
  CHECK_EQ(s.host.open_dialogs, 1);
  CHECK_EQ(s.host.save_dialogs, 0);
  CHECK_EQ(s.text(), std::wstring(L"alpha"));
  s.keys(L"C-/");
  CHECK_EQ(s.message(), std::wstring(L"No further undo information"));
  CHECK_EQ(s.text(), std::wstring(L"alpha"));
}

TEST(find_file_replace_modified_save_then_open) {
  Session s;
  s.load(L"one");
  s.type(L"x");
  s.host.add_file(L"C:\\work\\a.txt", "alpha");
  s.host.save_answers.push_back(SaveChoice::kSave);
  open_with_dialog(s, L"C:\\work\\a.txt");
  CHECK_EQ(s.host.save_dialogs, 1);
  CHECK_EQ(s.host.file(L"C:\\work\\t.txt"), std::string("xone"));
  CHECK_EQ(s.text(), std::wstring(L"alpha"));
  CHECK(!s.modified());
}

TEST(find_file_replace_modified_save_fails_aborts) {
  Session s;
  s.load(L"one");
  s.type(L"x");
  s.host.add_file(L"C:\\work\\a.txt", "alpha");
  s.host.fail_writes = true;
  s.host.save_answers.push_back(SaveChoice::kSave);
  open_with_dialog(s, L"C:\\work\\a.txt");
  CHECK_EQ(s.text(), std::wstring(L"xone"));
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\t.txt"));
  CHECK(s.modified());
  CHECK(s.message().find(L"Cannot write") == 0);
}

TEST(find_file_replace_modified_dont_save_opens) {
  Session s;
  s.load(L"one");
  s.type(L"x");
  s.host.add_file(L"C:\\work\\a.txt", "alpha");
  s.host.save_answers.push_back(SaveChoice::kDontSave);
  open_with_dialog(s, L"C:\\work\\a.txt");
  CHECK_EQ(s.text(), std::wstring(L"alpha"));
  CHECK_EQ(s.host.file(L"C:\\work\\t.txt"), std::string("one"));
  CHECK(s.host.written.empty());
}

TEST(find_file_replace_modified_cancel_stops) {
  Session s;
  s.load(L"one");
  s.type(L"x");
  s.host.add_file(L"C:\\work\\a.txt", "alpha");
  s.host.save_answers.push_back(SaveChoice::kCancel);
  open_with_dialog(s, L"C:\\work\\a.txt");
  CHECK_EQ(s.text(), std::wstring(L"xone"));
  CHECK(s.modified());
}

TEST(find_file_replace_untitled_modified_save_asks_name_then_opens) {
  Session s;
  s.type(L"draft");
  s.host.add_file(L"C:\\work\\a.txt", "alpha");
  s.host.save_answers.push_back(SaveChoice::kSave);
  open_with_dialog(s, L"C:\\work\\a.txt");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Write file: "));
  s.type(L"draft.txt");
  s.keys(L"RET");
  CHECK_EQ(s.host.file(L"C:\\work\\draft.txt"), std::string("draft"));
  CHECK_EQ(s.text(), std::wstring(L"alpha"));
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\a.txt"));
}

TEST(find_file_new_window_launches_and_keeps_buffer) {
  Session s;
  s.load(L"one");
  s.host.add_file(L"C:\\work\\a.txt", "alpha");
  s.host.open_answers.push_back(OpenChoice::kNewWindow);
  s.keys(L"C-x C-f");
  s.type(L"a.txt");
  s.keys(L"RET");
  CHECK_EQ(s.host.launched.size(), size_t{1});
  if (!s.host.launched.empty()) CHECK_EQ(s.host.launched[0], std::wstring(L"C:\\work\\a.txt"));
  CHECK_EQ(s.text(), std::wstring(L"one"));
  CHECK_EQ(s.host.save_dialogs, 0);
}

TEST(find_file_new_window_failure_shows_error) {
  Session s;
  s.load(L"one");
  s.host.fail_launch = true;
  s.host.open_answers.push_back(OpenChoice::kNewWindow);
  s.ed.request_open(L"C:\\work\\a.txt");
  CHECK_EQ(s.message(), std::wstring(L"launch failed"));
}

TEST(find_file_cancel_keeps_buffer) {
  Session s;
  s.load(L"one");
  s.host.add_file(L"C:\\work\\a.txt", "alpha");
  s.host.open_answers.push_back(OpenChoice::kCancel);
  s.ed.request_open(L"C:\\work\\a.txt");
  CHECK_EQ(s.host.open_dialogs, 1);
  CHECK_EQ(s.text(), std::wstring(L"one"));
  CHECK(s.host.launched.empty());
}

TEST(find_file_nonexistent_path_opens_new_file) {
  Session s;
  s.load(L"one");
  open_with_dialog(s, L"C:\\work\\new.txt");
  CHECK_EQ(s.text(), std::wstring());
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\new.txt"));
  CHECK_EQ(s.message(), std::wstring(L"(New file)"));
  CHECK(!s.ed.buffer().format.bom);
  CHECK(s.ed.buffer().format.eol == geditor::Eol::kLf);
  s.type(L"hi");
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.file(L"C:\\work\\new.txt"), std::string("hi"));
}

TEST(find_file_request_open_is_drag_and_drop_entry) {
  // The host calls request_open for a dropped file; a relative path is
  // resolved against the current directory and an active prompt is aborted.
  Session s;
  s.load(L"one");
  s.keys(L"C-x C-f");
  s.host.add_file(L"C:\\work\\dropped.txt", "dropped");
  s.host.open_answers.push_back(OpenChoice::kReplace);
  s.ed.request_open(L"dropped.txt");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.text(), std::wstring(L"dropped"));
}

TEST(find_file_inside_minibuffer_is_refused) {
  Session s;
  s.keys(L"C-x C-f");
  s.keys(L"C-x C-f");
  CHECK_EQ(s.message(), std::wstring(L"Command attempted to use minibuffer while in minibuffer"));
}

// ---- save-buffer and write-file ----

TEST(save_buffer_unmodified_says_no_changes) {
  Session s;
  s.load(L"one");
  s.keys(L"C-x C-s");
  CHECK_EQ(s.message(), std::wstring(L"(No changes need to be saved)"));
  CHECK(s.host.written.empty());
}

TEST(save_buffer_writes_modified_buffer) {
  Session s;
  s.load(L"one");
  s.type(L"x");
  s.keys(L"C-x C-s");
  CHECK_EQ(s.message(), std::wstring(L"Wrote C:\\work\\t.txt"));
  CHECK_EQ(s.host.file(L"C:\\work\\t.txt"), std::string("xone"));
  CHECK(!s.modified());
}

TEST(save_buffer_untitled_behaves_like_write_file) {
  Session s;
  s.type(L"draft");
  s.keys(L"C-x C-s");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Write file: "));
  s.type(L"d.txt");
  s.keys(L"RET");
  CHECK_EQ(s.host.file(L"C:\\work\\d.txt"), std::string("draft"));
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\d.txt"));
  CHECK(!s.modified());
}

TEST(save_buffer_write_error_keeps_modified) {
  Session s;
  s.load(L"one");
  s.type(L"x");
  s.host.fail_writes = true;
  s.keys(L"C-x C-s");
  CHECK_EQ(s.message(), std::wstring(L"Cannot write C:\\work\\t.txt: disk full"));
  CHECK(s.modified());
}

TEST(write_file_new_target_renames_buffer) {
  Session s;
  s.load(L"one");
  s.keys(L"C-x C-w");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\"));
  s.type(L"two.txt");
  s.keys(L"RET");
  CHECK_EQ(s.host.file(L"C:\\work\\two.txt"), std::string("one"));
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\two.txt"));
  CHECK_EQ(s.host.yes_no_dialogs, 0);
}

TEST(write_file_existing_target_yes_overwrites) {
  Session s;
  s.load(L"one");
  s.host.add_file(L"C:\\work\\x.txt", "old");
  s.host.yes_no_answers.push_back(true);
  s.keys(L"C-x C-w");
  s.type(L"x.txt");
  s.keys(L"RET");
  CHECK_EQ(s.host.yes_no_dialogs, 1);
  CHECK_EQ(s.host.file(L"C:\\work\\x.txt"), std::string("one"));
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\x.txt"));
}

TEST(write_file_existing_target_no_keeps_file) {
  Session s;
  s.load(L"one");
  s.host.add_file(L"C:\\work\\x.txt", "old");
  s.host.yes_no_answers.push_back(false);
  s.keys(L"C-x C-w");
  s.type(L"x.txt");
  s.keys(L"RET");
  CHECK_EQ(s.host.file(L"C:\\work\\x.txt"), std::string("old"));
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\t.txt"));
}

TEST(write_file_error_keeps_name_and_modified) {
  Session s;
  s.load(L"one");
  s.type(L"x");
  s.host.fail_writes = true;
  s.keys(L"C-x C-w");
  s.type(L"y.txt");
  s.keys(L"RET");
  CHECK(s.message().find(L"Cannot write") == 0);
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\t.txt"));
  CHECK(s.modified());
}

TEST(write_file_to_directory_uses_buffer_file_name) {
  Session s;
  s.load(L"one");
  s.host.add_dir(L"C:\\work\\sub");
  s.keys(L"C-x C-w");
  s.type(L"sub");
  s.keys(L"RET");
  CHECK_EQ(s.host.file(L"C:\\work\\sub\\t.txt"), std::string("one"));
}

// ---- read-only-mode ----

TEST(read_only_mode_toggles_and_refuses_edits) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-x C-q");
  CHECK(s.ed.buffer().read_only);
  s.type(L"x");
  s.keys(L"RET C-d");
  CHECK_EQ(s.message(), std::wstring(L"Buffer is read-only"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  s.keys(L"C-x C-q");
  CHECK(!s.ed.buffer().read_only);
  s.type(L"x");
  CHECK_EQ(s.text(), std::wstring(L"xabc"));
}

// ---- quit ----

TEST(quit_unmodified_quits) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-x C-c");
  CHECK_EQ(s.host.quit_calls, 1);
  CHECK_EQ(s.host.save_dialogs, 0);
}

TEST(quit_modified_save_saves_then_quits) {
  Session s;
  s.load(L"abc");
  s.type(L"x");
  s.host.save_answers.push_back(SaveChoice::kSave);
  s.keys(L"C-x C-c");
  CHECK_EQ(s.host.file(L"C:\\work\\t.txt"), std::string("xabc"));
  CHECK_EQ(s.host.quit_calls, 1);
}

TEST(quit_modified_save_failure_stays) {
  Session s;
  s.load(L"abc");
  s.type(L"x");
  s.host.fail_writes = true;
  s.host.save_answers.push_back(SaveChoice::kSave);
  s.keys(L"C-x C-c");
  CHECK_EQ(s.host.quit_calls, 0);
  CHECK(s.modified());
}

TEST(quit_modified_dont_save_quits_without_writing) {
  Session s;
  s.load(L"abc");
  s.type(L"x");
  s.host.save_answers.push_back(SaveChoice::kDontSave);
  s.keys(L"C-x C-c");
  CHECK_EQ(s.host.quit_calls, 1);
  CHECK(s.host.written.empty());
}

TEST(quit_modified_cancel_stays) {
  Session s;
  s.load(L"abc");
  s.type(L"x");
  s.host.save_answers.push_back(SaveChoice::kCancel);
  s.keys(L"C-x C-c");
  CHECK_EQ(s.host.quit_calls, 0);
  CHECK_EQ(s.text(), std::wstring(L"xabc"));
}

TEST(quit_untitled_modified_save_asks_name_then_quits) {
  Session s;
  s.type(L"draft");
  s.host.save_answers.push_back(SaveChoice::kSave);
  s.keys(L"C-x C-c");
  CHECK_EQ(s.host.quit_calls, 0);
  CHECK(s.ed.minibuffer_active());
  s.type(L"q.txt");
  s.keys(L"RET");
  CHECK_EQ(s.host.file(L"C:\\work\\q.txt"), std::string("draft"));
  CHECK_EQ(s.host.quit_calls, 1);
}

TEST(quit_untitled_modified_save_aborted_does_not_quit) {
  Session s;
  s.type(L"draft");
  s.host.save_answers.push_back(SaveChoice::kSave);
  s.keys(L"C-x C-c C-g");
  CHECK_EQ(s.host.quit_calls, 0);
  // A later write-file must not quit.
  s.keys(L"C-x C-w");
  s.type(L"r.txt");
  s.keys(L"RET");
  CHECK_EQ(s.host.quit_calls, 0);
}
