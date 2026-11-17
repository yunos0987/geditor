// M4a saving and read-only through the fake host (docs/PLAN.md 4.8,
// decisions F2 - F6): which saves make "<file>~" ([C4]), the failure of each
// step ([C4], [C10]), the external-change question ([C4]), the read-only
// attribute and read-only-mode ([C5]) with the table of every editing
// command.

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "commands.h"
#include "encoding.h"
#include "fake_host.h"

using test::FakeHost;
using test::Session;

namespace {

constexpr wchar_t kPath[] = L"C:\\work\\s.txt";
constexpr wchar_t kBackup[] = L"C:\\work\\s.txt~";

void open_bytes(Session& s, const std::string& bytes, const std::wstring& path = kPath) {
  s.host.add_file(path, bytes);
  s.ed.request_open(path);
  CHECK(s.ed.buffer().path == path);
}

// Types `text` at the end and saves.
void edit_and_save(Session& s, const wchar_t* text) {
  s.keys(L"M->");
  s.type(text);
  s.keys(L"C-x C-s");
}

// Whether the last save_file call asked for a backup. No call at all is a
// failure (and false), never an out-of-range access (M4a fix F6).
bool last_save_asked_backup(const FakeHost& host) {
  CHECK(!host.saves.empty());
  return !host.saves.empty() && host.saves.back().second;
}

// The text of the last yes/no question; no question is a failure (and "").
std::wstring last_yes_no_question(const FakeHost& host) {
  CHECK_EQ(host.yes_no_questions.empty(), false);
  return host.yes_no_questions.empty() ? std::wstring() : host.yes_no_questions.back();
}

}  // namespace

// ---- [C4] / F3: "<file>~" on the first save of a file in this process ----

TEST(m4a_backup_only_on_the_first_save_of_a_file) {
  Session s;
  open_bytes(s, "v1");
  edit_and_save(s, L"2");
  CHECK_EQ(s.message(), std::wstring(L"Wrote C:\\work\\s.txt"));
  CHECK_EQ(s.host.saves.size(), size_t{1});
  CHECK(s.host.saves[0].second);  // backup asked
  CHECK_EQ(s.host.file(kBackup), std::string("v1"));
  edit_and_save(s, L"3");
  CHECK_EQ(s.host.saves.size(), size_t{2});
  CHECK(!s.host.saves[1].second);
  CHECK_EQ(s.host.file(kPath), std::string("v123"));
  CHECK_EQ(s.host.file(kBackup), std::string("v1"));  // untouched
  // Reopening the same file in this process: still no second backup.
  s.host.add_file(L"C:\\work\\other.txt", "o");
  s.host.open_answers.push_back(geditor::OpenChoice::kReplace);
  s.ed.request_open(L"C:\\work\\other.txt");
  s.host.open_answers.push_back(geditor::OpenChoice::kReplace);
  s.ed.request_open(kPath);
  edit_and_save(s, L"4");
  CHECK(!last_save_asked_backup(s.host));
  CHECK_EQ(s.host.file(kBackup), std::string("v1"));
}

TEST(m4a_new_files_get_no_backup) {
  Session s;
  s.ed.request_open(L"C:\\work\\new.txt");
  s.type(L"n");
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.file(L"C:\\work\\new.txt"), std::string("n"));
  CHECK(!last_save_asked_backup(s.host));
  CHECK(!s.host.has_file(L"C:\\work\\new.txt~"));
  // The second save of it in this process: no backup either.
  s.type(L"m");
  s.keys(L"C-x C-s");
  CHECK(!last_save_asked_backup(s.host));
  CHECK(!s.host.has_file(L"C:\\work\\new.txt~"));
  // write-file of an untitled buffer to a new name: none.
  Session u;
  u.type(L"x");
  u.keys(L"C-x C-w");
  u.type(L"u.txt");
  u.keys(L"RET");
  CHECK_EQ(u.host.file(L"C:\\work\\u.txt"), std::string("x"));
  CHECK(!last_save_asked_backup(u.host));
}

TEST(m4a_backup_setting_off_makes_none) {
  Session s;
  s.settings(R"({"backup": false})");
  open_bytes(s, "v1");
  edit_and_save(s, L"2");
  CHECK(!last_save_asked_backup(s.host));
  CHECK(!s.host.has_file(kBackup));
}

// F3: a failure to make the backup stops the save and says why.
TEST(m4a_backup_failure_stops_the_save) {
  Session s;
  open_bytes(s, "v1");
  s.host.fail_step = FakeHost::SaveStep::kBackup;
  edit_and_save(s, L"2");
  CHECK_EQ(s.message(),
           std::wstring(L"Cannot write C:\\work\\s.txt: making the backup C:\\work\\s.txt~: Access is denied"));
  CHECK_EQ(s.host.file(kPath), std::string("v1"));
  CHECK(!s.host.has_file(kBackup));
  CHECK(s.modified());
  // The next save is still the first successful one: it makes the backup.
  s.keys(L"C-x C-s");
  CHECK(last_save_asked_backup(s.host));
  CHECK_EQ(s.host.file(kBackup), std::string("v1"));
  CHECK(!s.modified());
}

// [C4] / [C10]: each step failing leaves the file and "<file>~" as they were,
// says which step failed, and keeps the buffer modified.
TEST(m4a_each_save_step_failure_is_reported_and_changes_nothing) {
  struct Row {
    FakeHost::SaveStep step;
    bool new_file;
    const wchar_t* text;
  };
  const Row rows[] = {
      {FakeHost::SaveStep::kTempWrite, false, L"writing the temporary file: The disk is full"},
      {FakeHost::SaveStep::kFlush, false, L"flushing the temporary file: The disk is full"},
      {FakeHost::SaveStep::kReplace, false, L"replacing the file: Access is denied"},
      {FakeHost::SaveStep::kMove, true, L"moving the temporary file: Access is denied"},
      {FakeHost::SaveStep::kBackup, false, L"making the backup C:\\work\\s.txt~: Access is denied"},
  };
  for (const Row& r : rows) {
    Session s;
    if (r.new_file) {
      s.ed.request_open(kPath);
    } else {
      s.host.add_file(kBackup, "older backup");
      open_bytes(s, "v1");
    }
    s.host.fail_step = r.step;
    edit_and_save(s, L"2");
    const std::wstring expected = std::wstring(L"Cannot write C:\\work\\s.txt: ") + r.text;
    if (s.message() != expected) std::printf("  step %d\n", static_cast<int>(r.step));
    CHECK_EQ(s.message(), expected);
    CHECK(s.modified());
    CHECK(s.ed.mode_line().find(L"-**") == 0);
    if (r.new_file) {
      CHECK(!s.host.has_file(kPath));
    } else {
      CHECK_EQ(s.host.file(kPath), std::string("v1"));
      CHECK_EQ(s.host.file(kBackup), std::string("older backup"));
    }
  }
}

// ---- [C4] / F2: the external-change question ----

TEST(m4a_external_change_asks_and_no_writes_nothing) {
  Session s;
  open_bytes(s, "v1");
  s.host.touch_file(kPath, "changed elsewhere");
  s.host.yes_no_answers.push_back(false);
  edit_and_save(s, L"2");
  CHECK_EQ(s.host.yes_no_dialogs, 1);
  CHECK_EQ(s.host.yes_no_questions.size(), size_t{1});
  CHECK_EQ(last_yes_no_question(s.host),
           std::wstring(L"s.txt has changed on disk since it was read; save anyway?"));
  CHECK_EQ(s.message(), std::wstring(L"Not saved"));
  CHECK_EQ(s.host.file(kPath), std::string("changed elsewhere"));
  CHECK(s.host.saves.empty());
  CHECK(s.modified());
  // Yes saves.
  s.host.yes_no_answers.push_back(true);
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.yes_no_dialogs, 2);
  CHECK_EQ(s.host.file(kPath), std::string("v12"));
  CHECK_EQ(s.host.file(kBackup), std::string("changed elsewhere"));
  // The saved state is the new reference: no question next time.
  edit_and_save(s, L"3");
  CHECK_EQ(s.host.yes_no_dialogs, 2);
  CHECK_EQ(s.host.file(kPath), std::string("v123"));
}

TEST(m4a_unchanged_file_saves_without_a_question) {
  Session s;
  open_bytes(s, "v1");
  edit_and_save(s, L"2");
  edit_and_save(s, L"3");
  CHECK_EQ(s.host.yes_no_dialogs, 0);
}

TEST(m4a_size_change_alone_counts_as_a_change) {
  Session s;
  open_bytes(s, "v1");
  // Same write time, other size (a tool that restores the time).
  s.host.files[L"c:\\work\\s.txt"].bytes = "v1 and more";
  s.host.yes_no_answers.push_back(false);
  edit_and_save(s, L"2");
  CHECK_EQ(s.host.yes_no_dialogs, 1);
  CHECK_EQ(s.message(), std::wstring(L"Not saved"));
}

TEST(m4a_deleted_file_is_saved_as_new_without_a_question) {
  Session s;
  open_bytes(s, "v1");
  s.host.remove_file(kPath);
  edit_and_save(s, L"2");
  CHECK_EQ(s.host.yes_no_dialogs, 0);
  CHECK_EQ(s.host.file(kPath), std::string("v12"));
  CHECK(!last_save_asked_backup(s.host));  // a new file: no backup
  CHECK(!s.host.has_file(kBackup));
}

TEST(m4a_file_created_after_opening_a_new_name_asks) {
  Session s;
  s.ed.request_open(kPath);  // new file
  s.host.add_file(kPath, "someone else");
  s.host.yes_no_answers.push_back(false);
  s.type(L"x");
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.yes_no_dialogs, 1);
  CHECK_EQ(s.host.file(kPath), std::string("someone else"));
}

// write-file to another existing file asks to overwrite, not about changes.
TEST(m4a_write_file_elsewhere_uses_the_overwrite_question) {
  Session s;
  open_bytes(s, "v1");
  s.host.touch_file(kPath, "changed");
  s.host.add_file(L"C:\\work\\b.txt", "b");
  s.host.yes_no_answers.push_back(true);
  s.type(L"x");
  s.keys(L"C-x C-w");
  s.type(L"b.txt");
  s.keys(L"RET");
  CHECK_EQ(s.host.yes_no_dialogs, 1);
  CHECK_EQ(last_yes_no_question(s.host), std::wstring(L"File C:\\work\\b.txt exists; overwrite?"));
  CHECK_EQ(s.host.file(L"C:\\work\\b.txt"), std::string("xv1"));
}

// ---- [C10]: failures to read the time and attributes ----

TEST(m4a_file_info_failure_at_save_stops_it) {
  Session s;
  open_bytes(s, "v1");
  s.host.info_error_paths.insert(L"c:\\work\\s.txt");
  edit_and_save(s, L"2");
  CHECK_EQ(s.message(), std::wstring(L"Cannot write C:\\work\\s.txt: reading the file time: Access is denied"));
  CHECK(s.host.saves.empty());
  CHECK_EQ(s.host.file(kPath), std::string("v1"));
  CHECK(s.modified());
}

TEST(m4a_file_info_failure_at_load_is_shown_and_the_file_opens) {
  Session s;
  s.host.add_file(kPath, "v1");
  s.host.info_error_paths.insert(L"c:\\work\\s.txt");
  s.ed.request_open(kPath);
  CHECK_EQ(s.text(), std::wstring(L"v1"));
  CHECK_EQ(s.message(), std::wstring(L"Cannot read the time and attributes of C:\\work\\s.txt: Access is denied"
                                     L" (changes on disk will not be noticed)"));
  s.host.info_error_paths.clear();
  s.host.touch_file(kPath, "changed");
  edit_and_save(s, L"2");
  CHECK_EQ(s.host.yes_no_dialogs, 0);  // nothing to compare with
  CHECK_EQ(s.host.file(kPath), std::string("v12"));
}

// ---- [C5] / F6: read-only ----

TEST(m4a_read_only_attribute_opens_read_only) {
  Session s;
  s.host.add_file(kPath, "locked");
  s.host.set_read_only(kPath, true);
  s.ed.request_open(kPath);
  CHECK(s.ed.buffer().read_only);
  CHECK(s.ed.mode_line().find(L"-%% s.txt") == 0);
  s.type(L"x");
  CHECK_EQ(s.message(), std::wstring(L"Buffer is read-only"));
  CHECK_EQ(s.text(), std::wstring(L"locked"));
  // C-x C-q turns it off; saving then fails with the reason.
  s.keys(L"C-x C-q");
  CHECK(!s.ed.buffer().read_only);
  CHECK_EQ(s.message(), std::wstring(L"Read-only mode disabled"));
  s.type(L"x");
  CHECK(s.ed.mode_line().find(L"-** s.txt") == 0);
  s.keys(L"C-x C-s");
  CHECK_EQ(s.message(), std::wstring(L"Cannot write C:\\work\\s.txt: Access is denied"));
  CHECK_EQ(s.host.file(kPath), std::string("locked"));
  CHECK(s.modified());
  // Back on: "%*" (read-only and modified).
  s.keys(L"C-x C-q");
  CHECK(s.ed.mode_line().find(L"-%* s.txt") == 0);
}

TEST(m4a_read_only_mode_toggles_on_a_normal_file) {
  Session s;
  open_bytes(s, "abc");
  CHECK(!s.ed.buffer().read_only);
  s.keys(L"C-x C-q");
  CHECK(s.ed.buffer().read_only);
  CHECK_EQ(s.message(), std::wstring(L"Read-only mode enabled"));
  CHECK(s.ed.mode_line().find(L"-%% s.txt") == 0);
  s.keys(L"C-x C-q");
  CHECK(!s.ed.buffer().read_only);
}

namespace {

// One editing command in a read-only buffer: the binding of <f12> (a
// command name or [name, arg]), the keys that run it, keys typed before
// read-only-mode is turned on (history for undo / redo), point and mark.
struct ReadOnlyRow {
  const char* command;
  const char* binding;  // JSON value for "<f12>"; nullptr: `keys` runs the command
  const wchar_t* keys;
  const wchar_t* prepare;
  size_t point;
  long mark;  // -1: none
};

// Text "ab CD\nef\n": every row would change it if the buffer were writable.
constexpr ReadOnlyRow kReadOnlyRows[] = {
    {"backward-kill-line", R"("backward-kill-line")", L"<f12>", L"", 2, -1},
    {"backward-kill-word", R"("backward-kill-word")", L"<f12>", L"", 2, -1},
    {"capitalize-word", R"("capitalize-word")", L"<f12>", L"", 0, -1},
    {"cut", R"("cut")", L"<f12>", L"", 0, 2},
    {"delete-backward-char", R"("delete-backward-char")", L"<f12>", L"", 1, -1},
    {"delete-char", R"("delete-char")", L"<f12>", L"", 0, -1},
    {"downcase-word", R"("downcase-word")", L"<f12>", L"", 2, -1},
    {"insert-tab", R"("insert-tab")", L"<f12>", L"", 0, -1},
    {"insert-text", R"(["insert-text", "zz"])", L"<f12>", L"", 0, -1},
    {"kill-line", R"("kill-line")", L"<f12>", L"", 0, -1},
    {"kill-region", R"("kill-region")", L"<f12>", L"", 0, 2},
    {"kill-word", R"("kill-word")", L"<f12>", L"", 0, -1},
    {"newline", R"("newline")", L"<f12>", L"", 0, -1},
    {"open-line", R"("open-line")", L"<f12>", L"", 0, -1},
    {"paste", R"("paste")", L"<f12>", L"", 0, -1},
    // M5: query-replace refuses to start (then "!" is a refused self-insert).
    {"query-replace", R"(["query-replace", "ab", "x"])", L"<f12> !", L"", 0, -1},
    {"query-replace-regexp", R"j(["query-replace-regexp", "a(b)", "$1"])j", L"<f12> !", L"", 0, -1},
    {"quoted-insert", R"("quoted-insert")", L"<f12> x", L"", 0, -1},
    {"redo", R"("redo")", L"<f12>", L"M-> z C-/", 0, -1},
    {"self-insert-command", nullptr, L"x", L"", 0, -1},
    {"set-eol", R"(["set-eol", "crlf"])", L"<f12>", L"", 0, -1},
    {"transpose-chars", R"("transpose-chars")", L"<f12>", L"", 1, -1},
    {"undo", R"("undo")", L"<f12>", L"M-> z", 0, -1},
    {"upcase-word", R"("upcase-word")", L"<f12>", L"", 0, -1},
    {"yank", R"("yank")", L"<f12>", L"", 0, -1},
};

// Implemented commands that do not edit the main buffer.
constexpr const char* kNonEditingCommands[] = {
    "backward-char", "backward-word", "beginning-of-buffer", "beginning-of-line", "copy", "digit-argument",
    "end-of-buffer", "end-of-line", "exchange-point-and-mark", "execute-extended-command", "exit-minibuffer",
    "find-file", "find-file-with-encoding", "find-user-init-file", "forward-char", "forward-word",
    // M5: isearch and goto-line move point only.
    "goto-line", "isearch-abort", "isearch-backward", "isearch-delete-char", "isearch-exit", "isearch-forward",
    "isearch-query-replace", "isearch-repeat-backward", "isearch-repeat-forward", "isearch-toggle-case-fold",
    "isearch-toggle-regexp", "query-replace-exit", "query-replace-skip",
    "isearch-backward-regexp", "isearch-forward-regexp",  // 2026-11-17
    "keyboard-escape-quit", "keyboard-quit",
    // M6: the macro commands edit nothing themselves; the keys a macro runs
    // are refused one by one (test_m6.cpp).
    "kmacro-end-and-call-macro", "kmacro-end-macro", "kmacro-start-macro",
    "kill-ring-save", "mark-whole-buffer", "menu-bar-open", "minibuffer-complete", "negative-argument",
    "next-history-element", "next-line", "previous-history-element", "previous-line", "quit",
    "read-only-mode", "recenter-top-bottom", "revert-buffer-with-coding-system", "save-buffer",
    "scroll-down-command", "scroll-up-command", "select-all",
    // M4b: changes only the encoding of the next save (refused in
    // read-only-mode all the same: test_m4b_encoding.cpp).
    "set-buffer-file-coding-system", "set-mark-command", "set-wrap-mode", "text-scale-decrease",
    "text-scale-increase",
    "text-scale-reset", "toggle-eol-marks", "toggle-line-numbers", "toggle-whitespace-marks",
    "universal-argument", "write-file",
};

// M5: the query-replace keys that replace. They edit only inside
// query-replace, which cannot start in read-only-mode;
// m4a_query_replace_actions_stop_when_read_only_mode_comes_on covers
// read-only-mode turned on during query-replace.
constexpr const char* kQueryReplaceActions[] = {
    "query-replace-act",
    "query-replace-act-and-exit",
    "query-replace-all",
};

}  // namespace

// The lists cover every implemented command exactly once, so a new
// editing command cannot be missed by the table below.
TEST(m4a_read_only_table_covers_every_registered_command) {
  std::set<std::string> listed;
  for (const ReadOnlyRow& r : kReadOnlyRows) CHECK(listed.insert(r.command).second);
  for (const char* c : kNonEditingCommands) CHECK(listed.insert(c).second);
  for (const char* c : kQueryReplaceActions) CHECK(listed.insert(c).second);
  for (const geditor::CommandInfo& info : geditor::all_commands()) {
    if (info.fn == nullptr) continue;  // later milestones: "not implemented yet"
    if (listed.count(info.name) == 0) std::printf("  not in a list: %s\n", info.name);
    CHECK(listed.count(info.name) == 1);
    listed.erase(info.name);
  }
  for (const std::string& rest : listed) std::printf("  not an implemented command: %s\n", rest.c_str());
  CHECK(listed.empty());
}

TEST(m4a_every_editing_command_is_refused_in_read_only_mode) {
  for (const ReadOnlyRow& r : kReadOnlyRows) {
    Session s;
    if (r.binding != nullptr) s.settings(std::string(R"({"keys": {"<f12>": )") + r.binding + "}}");
    CHECK(s.ed.settings_errors().empty());
    open_bytes(s, "ab CD\nef\n");
    s.host.clipboard = L"clip";
    s.host.clipboard_has_text = true;
    if (*r.prepare != 0) s.keys(r.prepare);
    s.set_point(r.point);
    if (r.mark >= 0) {
      s.keys(L"C-SPC");
      s.set_point(static_cast<size_t>(r.mark));
    }
    s.keys(L"C-x C-q");
    CHECK(s.ed.buffer().read_only);
    const std::wstring text = s.text();
    const bool modified = s.modified();
    const geditor::Eol eol = s.ed.buffer().format.eol;
    s.keys(r.keys);
    if (s.message() != L"Buffer is read-only") std::printf("  %s: %s\n", r.command, test::to_display(s.message()).c_str());
    CHECK_EQ(s.message(), std::wstring(L"Buffer is read-only"));
    CHECK_EQ(s.text(), text);
    CHECK_EQ(s.modified(), modified);
    CHECK(s.ed.buffer().format.eol == eol);
    // The same command with read-only-mode off does change the buffer (so
    // the row really exercises an edit).
    s.keys(L"C-x C-q");
    s.keys(r.keys);
    if (s.text() == text && s.ed.buffer().format.eol == eol) std::printf("  %s: no edit when writable\n", r.command);
    CHECK(s.text() != text || s.ed.buffer().format.eol != eol);
  }
}

// M5: C-x C-q during query-replace (through the global keymap) turns
// read-only-mode on; the next y, "." or "!" ends query-replace with "Buffer
// is read-only" and changes nothing more.
TEST(m4a_query_replace_actions_stop_when_read_only_mode_comes_on) {
  for (const wchar_t* key : {L"y", L".", L"!"}) {
    Session s;
    open_bytes(s, "ab ab ab\n");
    s.keys(L"M-%");
    s.type(L"ab");
    s.keys(L"RET");
    s.type(L"x");
    s.keys(L"RET");
    CHECK(s.ed.query_replace_active());
    s.keys(L"y");  // one replacement while writable
    CHECK_EQ(s.text(), std::wstring(L"x ab ab\n"));
    s.keys(L"C-x C-q");
    CHECK(s.ed.buffer().read_only);
    CHECK(s.ed.query_replace_active());
    s.keys(key);
    CHECK(!s.ed.query_replace_active());
    CHECK_EQ(s.message(), std::wstring(L"Buffer is read-only"));
    CHECK_EQ(s.text(), std::wstring(L"x ab ab\n"));
  }
}

// IME text is an edit too.
TEST(m4a_ime_text_is_refused_in_read_only_mode) {
  Session s;
  open_bytes(s, "abc");
  s.keys(L"C-x C-q");
  s.ed.insert_text(L"\x65E5");
  CHECK_EQ(s.message(), std::wstring(L"Buffer is read-only"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
}
