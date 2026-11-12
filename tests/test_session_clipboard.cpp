// Session end ([S5] of M1b retry 1) and clipboard read failures ([S6]).

#include "fake_host.h"

using geditor::SaveChoice;

// ---- Clipboard read ----

TEST(clipboard_read_failure_is_reported) {
  test::Session s;
  s.load(L"abc");
  s.host.clipboard = L"xyz";
  s.host.clipboard_has_text = true;
  s.host.fail_clipboard_read = true;
  s.keys(L"C-y");
  CHECK_EQ(s.message(), std::wstring(L"Cannot read the clipboard"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  CHECK(!s.modified());
}

TEST(clipboard_read_failure_paste) {
  test::Session s;
  s.load(L"abc");
  s.host.fail_clipboard_read = true;
  s.ed.execute_command("paste");
  CHECK_EQ(s.message(), std::wstring(L"Cannot read the clipboard"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
}

TEST(clipboard_no_text_is_empty) {
  test::Session s;
  s.load(L"abc");
  s.keys(L"C-y");
  CHECK_EQ(s.message(), std::wstring(L"Clipboard is empty"));
}

TEST(clipboard_read_ok_yanks) {
  test::Session s;
  s.load(L"abc");
  s.host.clipboard = L"x\r\ny";
  s.host.clipboard_has_text = true;
  s.keys(L"C-y");
  CHECK_EQ(s.text(), std::wstring(L"x\nyabc"));
}

// ---- Session end ----

TEST(session_end_unmodified_allowed) {
  test::Session s;
  s.load(L"abc");
  CHECK(s.ed.confirm_session_end());
  CHECK_EQ(s.host.save_dialogs, 0);
}

TEST(session_end_save) {
  test::Session s;
  s.load(L"abc");
  s.type(L"x");
  s.host.save_answers.push_back(SaveChoice::kSave);
  CHECK(s.ed.confirm_session_end());
  CHECK_EQ(s.host.file(L"C:\\work\\t.txt"), std::string("xabc"));
  CHECK(!s.modified());
  CHECK_EQ(s.host.quit_calls, 0);  // Windows ends the process, not the editor
}

TEST(session_end_dont_save) {
  test::Session s;
  s.load(L"abc");
  s.type(L"x");
  s.host.save_answers.push_back(SaveChoice::kDontSave);
  CHECK(s.ed.confirm_session_end());
  CHECK_EQ(s.host.file(L"C:\\work\\t.txt"), std::string("abc"));
}

TEST(session_end_cancel_blocks) {
  test::Session s;
  s.load(L"abc");
  s.type(L"x");
  s.host.save_answers.push_back(SaveChoice::kCancel);
  CHECK(!s.ed.confirm_session_end());
  CHECK(s.modified());
}

TEST(session_end_failed_save_blocks) {
  test::Session s;
  s.load(L"abc");
  s.type(L"x");
  s.host.fail_writes = true;
  s.host.save_answers.push_back(SaveChoice::kSave);
  CHECK(!s.ed.confirm_session_end());
  CHECK(s.modified());
  CHECK(s.message().find(L"Cannot write") == 0);
}

TEST(session_end_untitled_save_asks_name) {
  test::Session s;
  s.type(L"new text");
  s.host.save_answers.push_back(SaveChoice::kSave);
  CHECK(!s.ed.confirm_session_end());
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Write file: "));
  // Finishing the save then quits, as for C-x C-c.
  s.keys(L"C-a C-k");
  s.type(L"C:\\work\\n.txt");
  s.keys(L"RET");
  CHECK_EQ(s.host.file(L"C:\\work\\n.txt"), std::string("new text"));
  CHECK_EQ(s.host.quit_calls, 1);
}
