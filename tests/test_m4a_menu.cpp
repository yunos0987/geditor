// M4-9 for M4a ([C7]): the File menu's Line Endings radio and Read Only
// check follow set-eol, reopening, mixed files, C-x C-q and the read-only
// attribute; these items are enabled (and, since M4b, the Encoding for
// Saving items).

#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "commands.h"
#include "fake_host.h"
#include "menu.h"

using geditor::MenuItem;
using geditor::MenuItemKind;
using test::Session;

namespace {

// The command items of File > Line Endings / Read Only / Encoding for
// Saving, by their text without the mnemonic ("LF", "Read Only", "utf-8").
const MenuItem* file_item(const wchar_t* submenu, const wchar_t* text) {
  for (const geditor::Menu& m : geditor::menu_bar_menus()) {
    if (geditor::menu_text_without_mnemonic(m.title) != L"File") continue;
    for (const MenuItem& item : m.items) {
      std::span<const MenuItem> items(&item, 1);
      if (item.kind == MenuItemKind::kSubmenu) {
        if (submenu == nullptr || geditor::menu_text_without_mnemonic(item.text) != submenu) continue;
        items = item.submenu;
      } else if (submenu != nullptr) {
        continue;
      }
      for (const MenuItem& i : items) {
        if (i.kind == MenuItemKind::kCommand && geditor::menu_text_without_mnemonic(i.text) == text) return &i;
      }
    }
  }
  std::printf("  no File item %s\n", test::to_display(text).c_str());
  return nullptr;
}

// "LF" / "CRLF" / "CR" for the one radio item that is checked ("" none,
// "+" more than one).
std::wstring line_endings_mark(const Session& s) {
  std::wstring mark;
  for (const wchar_t* name : {L"LF", L"CRLF", L"CR"}) {
    const MenuItem* item = file_item(L"Line Endings", name);
    if (item != nullptr && geditor::menu_item_checked(s.ed, *item)) mark = mark.empty() ? name : L"+";
  }
  return mark;
}

bool read_only_checked(const Session& s) {
  const MenuItem* item = file_item(nullptr, L"Read Only");
  return item != nullptr && geditor::menu_item_checked(s.ed, *item);
}

void open_bytes(Session& s, const std::string& bytes, const wchar_t* path) {
  s.host.add_file(path, bytes);
  if (!s.ed.buffer().path.empty()) s.host.open_answers.push_back(geditor::OpenChoice::kReplace);
  s.ed.request_open(path);
}

void run_item(Session& s, const wchar_t* submenu, const wchar_t* text) {
  const MenuItem* item = file_item(submenu, text);
  CHECK(item != nullptr);
  if (item != nullptr) CHECK(s.ed.execute_menu_item(*item));
}

}  // namespace

TEST(m4a_menu_line_endings_and_read_only_follow_the_buffer) {
  struct Row {
    const wchar_t* step;
    void (*action)(Session&);
    const wchar_t* line_end;  // the checked Line Endings item
    bool read_only;
  };
  const Row rows[] = {
      {L"new buffer (new_file.eol lf)", [](Session&) {}, L"LF", false},
      {L"open a CRLF file", [](Session& s) { open_bytes(s, "a\r\nb\r\n", L"C:\\work\\crlf.txt"); }, L"CRLF", false},
      {L"set-eol cr (menu)", [](Session& s) { run_item(s, L"Line Endings", L"CR"); }, L"CR", false},
      {L"undo", [](Session& s) { s.keys(L"C-/"); }, L"CRLF", false},
      {L"set-eol lf (C-x RET e)",
       [](Session& s) {
         s.keys(L"C-x RET e");
         s.type(L"lf");
         s.keys(L"RET");
       },
       L"LF", false},
      {L"reopen the CRLF file (after saving)",
       [](Session& s) {
         s.keys(L"C-x C-s");
         open_bytes(s, "x\r\n", L"C:\\work\\other.txt");
         open_bytes(s, "a\r\nb\r\n", L"C:\\work\\crlf.txt");
       },
       L"CRLF", false},
      {L"mixed file, default CR", [](Session& s) { open_bytes(s, "a\rb\rc\r\nd\n", L"C:\\work\\mixed.txt"); },
       L"CR", false},
      {L"C-x C-q", [](Session& s) { s.keys(L"C-x C-q"); }, L"CR", true},
      {L"Read Only item", [](Session& s) { run_item(s, nullptr, L"Read Only"); }, L"CR", false},
      {L"read-only attribute file",
       [](Session& s) {
         s.host.add_file(L"C:\\work\\ro.txt", "r\n");
         s.host.set_read_only(L"C:\\work\\ro.txt", true);
         s.host.open_answers.push_back(geditor::OpenChoice::kReplace);
         s.ed.request_open(L"C:\\work\\ro.txt");
       },
       L"LF", true},
  };
  Session s;
  for (const Row& r : rows) {
    r.action(s);
    if (line_endings_mark(s) != r.line_end || read_only_checked(s) != r.read_only) {
      std::printf("  step: %s\n", test::to_display(r.step).c_str());
    }
    CHECK_EQ(line_endings_mark(s), std::wstring(r.line_end));
    CHECK_EQ(read_only_checked(s), r.read_only);
  }
}

// M4b enabled the Encoding for Saving items too (test_m4b_encoding.cpp has
// their marks).
TEST(m4a_menu_line_endings_read_only_and_encoding_are_enabled) {
  Session s;
  open_bytes(s, "a\n", L"C:\\work\\a.txt");
  for (const wchar_t* name : {L"LF", L"CRLF", L"CR"}) {
    const MenuItem* item = file_item(L"Line Endings", name);
    CHECK(item != nullptr && geditor::menu_item_enabled(s.ed, *item));
  }
  const MenuItem* read_only = file_item(nullptr, L"Read Only");
  CHECK(read_only != nullptr && geditor::menu_item_enabled(s.ed, *read_only));
  for (const wchar_t* name : geditor::encoding_names()) {
    const MenuItem* item = file_item(L"Encoding for Saving", name);
    CHECK(item != nullptr && geditor::menu_item_enabled(s.ed, *item));
  }
  // The commands are no longer "not implemented yet".
  CHECK(geditor::find_command("set-eol")->fn != nullptr);
  CHECK(geditor::find_command("read-only-mode")->fn != nullptr);
  CHECK(geditor::find_command("set-buffer-file-coding-system")->fn != nullptr);
  s.keys(L"C-x RET e");
  s.type(L"crlf");
  s.keys(L"RET");
  CHECK_EQ(s.message(), std::wstring(L"Line endings: CRLF"));
}
