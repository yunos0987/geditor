// M4b encoding commands (docs/PLAN.md 2 "open with an encoding", "read again
// with an encoding", 4.9 File menu): revert-buffer-with-coding-system
// ([M4-7], [C4]), find-file-with-encoding and --encoding ([C4]), the
// Encoding for Saving radio and the enabled File items ([M4-9], [C5]),
// set-buffer-file-coding-system ([C6]) and the nine names on every surface
// ([C8]).

#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "command_line.h"
#include "commands.h"
#include "encoding.h"
#include "fake_host.h"
#include "menu.h"
#include "process.h"

using geditor::MenuItem;
using geditor::MenuItemKind;
using test::Session;

namespace {

constexpr wchar_t kPath[] = L"C:\\work\\r.txt";
// "日本語\r\nテキスト\r\n" in Shift_JIS.
const char kShiftJis[] = "\x93\xFA\x96\x7B\x8C\xEA\r\n\x83\x65\x83\x4C\x83\x58\x83\x67\r\n";
const wchar_t kText[] = L"\x65E5\x672C\x8A9E\n\x30C6\x30AD\x30B9\x30C8\n";

void open_as(Session& s, const std::string& bytes, const std::wstring& encoding, const std::wstring& path = kPath) {
  s.host.add_file(path, bytes);
  if (!s.ed.buffer().path.empty() || s.ed.modified()) s.host.open_answers.push_back(geditor::OpenChoice::kReplace);
  s.ed.request_open(path, encoding);
  CHECK(s.ed.buffer().path == path);
}

std::wstring setting_name(const Session& s) {
  return geditor::encoding_setting_name(s.ed.buffer().format);
}

// The items of File > Encoding for Saving.
std::vector<const MenuItem*> encoding_items() {
  std::vector<const MenuItem*> items;
  for (const geditor::Menu& m : geditor::menu_bar_menus()) {
    if (geditor::menu_text_without_mnemonic(m.title) != L"File") continue;
    for (const MenuItem& item : m.items) {
      if (item.kind == MenuItemKind::kSubmenu &&
          geditor::menu_text_without_mnemonic(item.text) == L"Encoding for Saving") {
        for (const MenuItem& i : item.submenu) items.push_back(&i);
      }
    }
  }
  return items;
}

const MenuItem* file_item(const wchar_t* text) {
  for (const geditor::Menu& m : geditor::menu_bar_menus()) {
    if (geditor::menu_text_without_mnemonic(m.title) != L"File") continue;
    for (const MenuItem& item : m.items) {
      if (item.kind != MenuItemKind::kSeparator && geditor::menu_text_without_mnemonic(item.text) == text) {
        return &item;
      }
    }
  }
  return nullptr;
}

// The name of the one checked Encoding for Saving item ("" none, "+" more).
std::wstring encoding_mark(const Session& s) {
  std::wstring mark;
  for (const MenuItem* item : encoding_items()) {
    if (geditor::menu_item_checked(s.ed, *item)) mark = mark.empty() ? std::wstring(item->argument) : L"+";
  }
  return mark;
}

// Runs a command with its arguments through M-x and the minibuffer.
void mx(Session& s, const wchar_t* command, std::initializer_list<const wchar_t*> args) {
  s.keys(L"M-x");
  s.type(command);
  s.keys(L"RET");
  for (const wchar_t* a : args) {
    s.keys(L"C-a C-k");
    s.type(a);
    s.keys(L"RET");
  }
}

}  // namespace

// ---- [C4] revert-buffer-with-coding-system ----

TEST(m4b_revert_reads_again_from_disk_in_the_named_encoding) {
  Session s;
  // Read wrongly (UTF-8 named), then reverted as Shift_JIS: right text.
  open_as(s, kShiftJis, L"utf-8");
  CHECK(s.ed.buffer().format.undecodable);
  CHECK(s.text() != std::wstring(kText));
  s.keys(L"C-x RET r");
  CHECK(s.ed.minibuffer_active());
  s.type(L"shift_jis");
  s.keys(L"RET");
  CHECK_EQ(s.text(), std::wstring(kText));
  CHECK_EQ(setting_name(s), std::wstring(L"shift_jis"));
  CHECK(!s.ed.buffer().format.undecodable);
  CHECK(!s.modified());
  CHECK_EQ(s.message(), std::wstring(L"Read C:\\work\\r.txt again as Shift_JIS"));
}

TEST(m4b_revert_keeps_the_line_number_and_drops_undo) {
  Session s;
  open_as(s, kShiftJis, L"shift_jis");
  s.keys(L"C-n C-e");
  s.type(L"z");
  CHECK_EQ(s.ed.point_line(), size_t{1});
  CHECK(s.ed.buffer().undo.undo_count() > 0);
  s.host.yes_no_answers.push_back(true);
  s.ed.revert_buffer_with_coding_system(L"shift_jis");
  CHECK_EQ(s.host.yes_no_dialogs, 1);
  CHECK_EQ(s.text(), std::wstring(kText));
  CHECK_EQ(s.ed.point_line(), size_t{1});
  CHECK_EQ(s.point(), s.ed.buffer().text.line_start(1));
  CHECK_EQ(s.ed.buffer().undo.undo_count(), size_t{0});
  CHECK(!s.modified());
  // A shorter file: point goes to its last line.
  s.keys(L"M->");
  s.host.touch_file(kPath, "one line");
  s.ed.revert_buffer_with_coding_system(L"utf-8");
  CHECK_EQ(s.text(), std::wstring(L"one line"));
  CHECK_EQ(s.point(), size_t{0});
}

TEST(m4b_revert_with_changes_asks_and_no_changes_nothing) {
  Session s;
  open_as(s, kShiftJis, L"shift_jis");
  s.keys(L"M->");
  s.type(L"abc");
  s.keys(L"C-b");
  const std::wstring text = s.text();
  const size_t point = s.point();
  const size_t undo = s.ed.buffer().undo.undo_count();
  s.host.yes_no_answers.push_back(false);
  s.keys(L"C-x RET r");
  s.type(L"euc-jp");
  s.keys(L"RET");
  CHECK_EQ(s.host.yes_no_dialogs, 1);
  CHECK_EQ(s.host.yes_no_questions.back(), std::wstring(L"Discard the changes to r.txt and read it again as EUC-JP?"));
  CHECK_EQ(s.text(), text);
  CHECK_EQ(s.point(), point);
  CHECK(s.modified());
  CHECK_EQ(s.ed.buffer().undo.undo_count(), undo);
  CHECK_EQ(setting_name(s), std::wstring(L"shift_jis"));
  CHECK_EQ(s.message(), std::wstring(L"Cancelled"));
  // Undo still works.
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(kText));
}

TEST(m4b_revert_without_a_file_does_nothing) {
  Session s;
  s.type(L"untitled text");
  s.ed.revert_buffer_with_coding_system(L"utf-8");
  CHECK_EQ(s.message(), std::wstring(L"Buffer is not visiting a file"));
  CHECK_EQ(s.text(), std::wstring(L"untitled text"));
  CHECK_EQ(s.host.yes_no_dialogs, 0);
  // A file deleted on disk.
  s.host.save_answers.push_back(geditor::SaveChoice::kDontSave);
  open_as(s, kShiftJis, L"shift_jis", L"C:\\work\\gone.txt");
  s.type(L"x");
  s.host.remove_file(L"C:\\work\\gone.txt");
  const std::wstring text = s.text();
  s.ed.revert_buffer_with_coding_system(L"shift_jis");
  CHECK_EQ(s.message(), std::wstring(L"File C:\\work\\gone.txt is not on disk; nothing to read again"));
  CHECK_EQ(s.text(), text);
  CHECK(s.modified());
  CHECK_EQ(s.host.yes_no_dialogs, 0);
}

// ---- [C4] find-file-with-encoding and --encoding ----

TEST(m4b_find_file_with_encoding_reads_both_arguments_with_completion) {
  Session s;
  s.host.add_file(L"C:\\work\\sjis-file.txt", kShiftJis);
  s.keys(L"C-x RET c");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Find file: "));
  // It starts in the default directory and completes file names.
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\"));
  s.type(L"sji");
  s.keys(L"TAB");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\sjis-file.txt"));
  s.keys(L"RET");
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Coding system: "));
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring());
  s.type(L"shi");
  s.keys(L"TAB");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"shift_jis"));
  s.keys(L"RET");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\sjis-file.txt"));
  CHECK_EQ(s.text(), std::wstring(kText));
  CHECK_EQ(setting_name(s), std::wstring(L"shift_jis"));
}

TEST(m4b_find_file_with_encoding_new_window_gets_the_encoding) {
  Session s;
  s.load(L"current");
  s.host.add_file(L"C:\\work\\e.txt", "\xA4\xA2");
  s.host.open_answers.push_back(geditor::OpenChoice::kNewWindow);
  mx(s, L"find-file-with-encoding", {L"C:\\work\\e.txt", L"euc-jp"});
  CHECK_EQ(s.host.launched.size(), size_t{1});
  CHECK_EQ(s.host.launched_encodings.size(), size_t{1});
  if (!s.host.launched_encodings.empty()) CHECK_EQ(s.host.launched_encodings[0], std::wstring(L"euc-jp"));
  CHECK_EQ(s.text(), std::wstring(L"current"));
  // Plain find-file passes none.
  s.host.open_answers.push_back(geditor::OpenChoice::kNewWindow);
  s.ed.request_open(L"C:\\work\\e.txt");
  CHECK_EQ(s.host.launched_encodings.size(), size_t{2});
  if (s.host.launched_encodings.size() == 2) CHECK(s.host.launched_encodings[1].empty());
  // Replacing with unsaved changes saves first, then reads in the encoding.
  s.type(L"!");
  s.host.open_answers.push_back(geditor::OpenChoice::kReplace);
  s.host.save_answers.push_back(geditor::SaveChoice::kSave);
  s.ed.request_open(L"C:\\work\\e.txt", L"euc-jp");
  CHECK_EQ(s.text(), std::wstring(L"\x3042"));
  CHECK_EQ(setting_name(s), std::wstring(L"euc-jp"));
}

TEST(m4b_find_file_with_encoding_of_a_new_file_saves_in_it) {
  Session s;
  s.ed.request_open(L"C:\\work\\new.txt", L"utf-16le-bom");
  CHECK_EQ(setting_name(s), std::wstring(L"utf-16le-bom"));
  s.type(L"a");
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.file(L"C:\\work\\new.txt"), std::string("\xFF\xFE\x61\x00", 4));
}

TEST(m4b_encoding_option_is_parsed_and_passed_to_new_windows) {
  using geditor::win::CommandLine;
  const CommandLine c = geditor::win::parse_command_line(L"geditor.exe --encoding=shift_jis +2 a.txt");
  CHECK_EQ(c.encoding, std::wstring(L"shift_jis"));
  CHECK_EQ(c.file, std::wstring(L"a.txt"));
  CHECK(c.errors.empty());
  const std::wstring line = geditor::win::editor_command_line(L"C:\\bin\\geditor.exe", L"C:\\a b\\f.txt",
                                                               L"C:\\s\\s.json", true, L"euc-jp");
  CHECK_EQ(line, std::wstring(L"\"C:\\bin\\geditor.exe\" \"--headless\" \"--settings=C:\\s\\s.json\" "
                              L"\"--encoding=euc-jp\" \"C:\\a b\\f.txt\""));
  const CommandLine parsed = geditor::win::parse_command_line(line);
  CHECK_EQ(parsed.encoding, std::wstring(L"euc-jp"));
  CHECK_EQ(parsed.settings, std::wstring(L"C:\\s\\s.json"));
  CHECK_EQ(parsed.file, std::wstring(L"C:\\a b\\f.txt"));
  CHECK(parsed.errors.empty());
  // Without an encoding nothing is added.
  CHECK_EQ(geditor::win::editor_command_line(L"C:\\g.exe", L"C:\\f.txt", L"", false, L""),
           std::wstring(L"\"C:\\g.exe\" \"C:\\f.txt\""));
  // The start-up path: the command line's encoding reaches request_open.
  Session s;
  s.host.add_file(L"C:\\work\\a.txt", kShiftJis);
  s.ed.request_open(c.file, c.encoding);
  CHECK_EQ(s.text(), std::wstring(kText));
  CHECK_EQ(setting_name(s), std::wstring(L"shift_jis"));
}

// [C8] --encoding accepts only the nine names.
TEST(m4b_unknown_encoding_option_is_reported) {
  using geditor::win::CommandLine;
  for (const wchar_t* bad : {L"latin-1", L"windows-1252", L"gb18030", L"big5", L"euc-kr", L"UTF-8", L"sjis", L""}) {
    const CommandLine c = geditor::win::parse_command_line(std::wstring(L"geditor.exe --encoding=") + bad + L" a.txt");
    CHECK(c.encoding.empty());
    CHECK_EQ(c.file, std::wstring(L"a.txt"));
    CHECK_EQ(c.errors.size(), size_t{1});
    if (!c.errors.empty() && *bad != 0) {
      CHECK(c.errors[0].find(std::wstring(L"Unknown encoding: ") + bad + L" (use utf-8, utf-8-bom, ") == 0);
    }
  }
  const CommandLine bare = geditor::win::parse_command_line(L"geditor.exe --encoding a.txt");
  CHECK_EQ(bare.errors.size(), size_t{1});
  if (!bare.errors.empty()) CHECK_EQ(bare.errors[0], std::wstring(L"Invalid option: --encoding (use --encoding=NAME)"));
  for (const wchar_t* good : geditor::encoding_names()) {
    CHECK_EQ(geditor::win::parse_command_line(std::wstring(L"geditor.exe --encoding=") + good).encoding,
             std::wstring(good));
  }
}

// ---- [C5] [M4-9] the File menu ----

TEST(m4b_set_buffer_file_coding_system_for_each_encoding_moves_the_radio) {
  size_t rows = 0;
  for (const wchar_t* name : geditor::encoding_names()) {
    Session s;
    s.load(L"abc\n");
    CHECK_EQ(encoding_mark(s), std::wstring(L"utf-8"));
    if (std::wstring(name) == L"utf-8") s.ed.set_buffer_file_coding_system(L"shift_jis");
    s.keys(L"C-x RET f");
    s.type(name);
    s.keys(L"RET");
    CHECK_EQ(encoding_mark(s), std::wstring(name));
    CHECK_EQ(setting_name(s), std::wstring(name));
    CHECK(s.modified());
    // The menu item runs the same command.
    s.ed.set_buffer_file_coding_system(name == std::wstring(L"euc-jp") ? L"utf-8" : L"euc-jp");
    for (const MenuItem* item : encoding_items()) {
      if (std::wstring(item->argument) == name) CHECK(s.ed.execute_menu_item(*item));
    }
    CHECK_EQ(encoding_mark(s), std::wstring(name));
    ++rows;
  }
  CHECK_EQ(rows, size_t{9});
}

TEST(m4b_encoding_radio_follows_detection_and_revert) {
  Session s;
  struct Row {
    std::string bytes;
    const wchar_t* mark;
  };
  const Row rows[] = {
      {std::string("\xEF\xBB\xBF" "a"), L"utf-8-bom"},
      {std::string("\xFF\xFE" "a\0", 4), L"utf-16le-bom"},
      {std::string("\xFE\xFF\0" "a", 4), L"utf-16be-bom"},
      {std::string("a\0b\0", 4), L"utf-16le"},
      {std::string("\0a\0b", 4), L"utf-16be"},
      {std::string("\x93\xFA"), L"shift_jis"},
      {std::string("ascii"), L"utf-8"},
  };
  int i = 0;
  for (const Row& r : rows) {
    open_as(s, r.bytes, L"", L"C:\\work\\d" + std::to_wstring(i++) + L".txt");
    if (encoding_mark(s) != r.mark) std::printf("  %s\n", test::to_display(std::wstring(r.mark)).c_str());
    CHECK_EQ(encoding_mark(s), std::wstring(r.mark));
  }
  s.ed.revert_buffer_with_coding_system(L"euc-jp");
  CHECK_EQ(encoding_mark(s), std::wstring(L"euc-jp"));
  s.ed.revert_buffer_with_coding_system(L"iso-2022-jp");
  CHECK_EQ(encoding_mark(s), std::wstring(L"iso-2022-jp"));
}

TEST(m4b_every_m4_file_item_is_enabled) {
  Session s;
  s.load(L"abc");
  for (const wchar_t* text : {L"Open with Encoding...", L"Reopen with Encoding...", L"Encoding for Saving",
                              L"Line Endings", L"Read Only"}) {
    const MenuItem* item = file_item(text);
    if (item == nullptr || !geditor::menu_item_enabled(s.ed, *item)) {
      std::printf("  %s\n", test::to_display(text).c_str());
    }
    CHECK(item != nullptr && geditor::menu_item_enabled(s.ed, *item));
    if (item != nullptr && item->kind == MenuItemKind::kSubmenu) {
      for (const MenuItem& sub : item->submenu) CHECK(geditor::menu_item_enabled(s.ed, sub));
    }
  }
  // Open with Encoding... starts reading the file name.
  const MenuItem* open = file_item(L"Open with Encoding...");
  CHECK(open != nullptr && s.ed.execute_menu_item(*open));
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Find file: "));
  s.keys(L"C-g");
  const MenuItem* reopen = file_item(L"Reopen with Encoding...");
  CHECK(reopen != nullptr && s.ed.execute_menu_item(*reopen));
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Coding system: "));
}

// ---- [C6] set-buffer-file-coding-system ----

TEST(m4b_set_coding_system_changes_only_the_next_save) {
  Session s;
  open_as(s, kShiftJis, L"shift_jis");
  const std::wstring text = s.text();
  CHECK(!s.modified());
  s.ed.set_buffer_file_coding_system(L"utf-8");
  CHECK_EQ(s.text(), text);
  CHECK(s.modified());  // as in Emacs
  CHECK(s.ed.mode_line().find(L"-** r.txt  UTF-8 CRLF") == 0);
  CHECK_EQ(s.message(), std::wstring(L"Encoding for saving: UTF-8"));
  CHECK_EQ(s.host.file(kPath), std::string(kShiftJis));  // nothing written yet
  s.keys(L"C-x C-s");
  CHECK(!s.modified());
  CHECK_EQ(s.host.file(kPath), test::utf8(L"\x65E5\x672C\x8A9E\r\n\x30C6\x30AD\x30B9\x30C8\r\n"));
  // The same encoding again changes nothing.
  s.ed.set_buffer_file_coding_system(L"utf-8");
  CHECK(!s.modified());
  CHECK_EQ(s.message(), std::wstring(L"The encoding for saving is already UTF-8"));
}

TEST(m4b_set_coding_system_switches_the_bom) {
  Session s;
  open_as(s, "x\n", L"");
  s.ed.set_buffer_file_coding_system(L"utf-8-bom");
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.file(kPath), std::string("\xEF\xBB\xBFx\n"));
  CHECK_EQ(setting_name(s), std::wstring(L"utf-8-bom"));
  s.ed.set_buffer_file_coding_system(L"utf-8");
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.file(kPath), std::string("x\n"));
}

TEST(m4b_set_coding_system_to_one_that_cannot_write_the_text_fails_at_save) {
  Session s;
  open_as(s, test::utf8(L"ok\n\xD55C\n"), L"utf-8");
  s.ed.set_buffer_file_coding_system(L"euc-jp");
  CHECK(s.modified());
  s.keys(L"C-x C-s");
  CHECK_EQ(s.message(), std::wstring(L"Cannot save as EUC-JP: U+D55C at line 2, column 0 cannot be written in "
                                     L"EUC-JP; choose another encoding with set-buffer-file-coding-system"));
  CHECK_EQ(s.host.file(kPath), test::utf8(L"ok\n\xD55C\n"));
  CHECK(s.modified());
  CHECK_EQ(s.point(), size_t{3});
}

TEST(m4b_set_coding_system_is_refused_in_read_only_mode) {
  Session s;
  open_as(s, "x", L"");
  s.keys(L"C-x C-q");
  s.ed.set_buffer_file_coding_system(L"shift_jis");
  CHECK_EQ(s.message(), std::wstring(L"Buffer is read-only"));
  CHECK_EQ(setting_name(s), std::wstring(L"utf-8"));
  CHECK(!s.modified());
}

TEST(m4b_new_file_encoding_setting_is_applied) {
  Session s;
  s.settings(R"({"new_file": {"encoding": "utf-16le-bom"}})");
  CHECK_EQ(setting_name(s), std::wstring(L"utf-16le-bom"));
  s.ed.request_open(L"C:\\work\\fresh.txt");
  CHECK_EQ(setting_name(s), std::wstring(L"utf-16le-bom"));
  s.type(L"z");
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.file(L"C:\\work\\fresh.txt"), std::string("\xFF\xFE" "z\0", 4));
  // A chosen encoding is not replaced by a settings reload.
  Session t;
  t.ed.request_open(L"C:\\work\\n2.txt", L"euc-jp");
  t.settings(R"({"new_file": {"encoding": "utf-8"}})");
  CHECK_EQ(setting_name(t), std::wstring(L"euc-jp"));
}

// ---- [C8] the nine names on every surface ----

TEST(m4b_only_the_nine_names_are_accepted) {
  // The list itself.
  const std::vector<std::wstring> nine = {L"utf-8",        L"utf-8-bom", L"utf-16le", L"utf-16le-bom", L"utf-16be",
                                          L"utf-16be-bom", L"shift_jis", L"euc-jp",   L"iso-2022-jp"};
  std::vector<std::wstring> names;
  for (const wchar_t* n : geditor::encoding_names()) names.emplace_back(n);
  CHECK(names == nine);
  // parse_encoding and its round trip through encoding_setting_name.
  for (const std::wstring& n : nine) {
    geditor::FileFormat f;
    CHECK(geditor::parse_encoding(n, &f));
    CHECK_EQ(std::wstring(geditor::encoding_setting_name(f)), n);
  }
  const wchar_t* const others[] = {L"windows-1252", L"gb18030", L"big5", L"euc-kr", L"latin-1", L"UTF-8",
                                   L"cp932", L"utf8", L""};
  for (const wchar_t* n : others) {
    geditor::FileFormat f;
    CHECK(!geditor::parse_encoding(n, &f));
  }
  // The minibuffer: completion lists the nine; another name is no match.
  Session s;
  s.load(L"abc");
  s.keys(L"C-x RET f");
  s.keys(L"TAB");
  for (const std::wstring& n : nine) CHECK(s.message().find(n) != std::wstring::npos);
  s.type(L"windows-1252");
  s.keys(L"RET");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.message(), std::wstring(L"[No match]"));
  s.keys(L"C-g");
  CHECK_EQ(setting_name(s), std::wstring(L"utf-8"));
  // The menu: exactly the nine, in order.
  std::vector<std::wstring> items;
  for (const MenuItem* item : encoding_items()) items.emplace_back(item->argument);
  CHECK(items == nine);
  // A key binding in the settings with another name: an error of the
  // settings file, the binding dropped.
  s.settings(R"({"keys": {"<f12>": ["set-buffer-file-coding-system", "big5"],
                          "<f11>": ["revert-buffer-with-coding-system", "gb18030"],
                          "<f9>": ["find-file-with-encoding", "C:\\work\\t.txt", "euc-kr"]}})");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{3});
  for (const geditor::Diagnostic& d : s.ed.settings_errors()) {
    CHECK(d.message.find(L"must be one of utf-8, utf-8-bom, utf-16le, utf-16le-bom, utf-16be, utf-16be-bom, "
                         L"shift_jis, euc-jp, iso-2022-jp") != std::wstring::npos);
  }
  // The core refuses another name too (the commands and request_open).
  s.ed.set_buffer_file_coding_system(L"big5");
  CHECK(s.message().find(L"Unknown encoding: big5 (use utf-8, ") == 0);
  s.ed.revert_buffer_with_coding_system(L"gb18030");
  CHECK(s.message().find(L"Unknown encoding: gb18030") == 0);
  s.ed.find_file_with_encoding(L"C:\\work\\t.txt", L"euc-kr");
  CHECK(s.message().find(L"Unknown encoding: euc-kr") == 0);
  CHECK_EQ(setting_name(s), std::wstring(L"utf-8"));
  CHECK(!s.modified());
}
