// M3c additions after the manual check: ESC ESC ESC (keyboard-escape-quit,
// decision G8, [C10]) and the nine encodings (decision G9, [C11]).

#include <string>
#include <vector>

#include "commands.h"
#include "fake_host.h"
#include "menu.h"

using test::Session;

namespace {

// PLAN 2 "encodings that can be named", in its order.
const std::vector<std::wstring> kNineEncodings = {
    L"utf-8",        L"utf-8-bom", L"utf-16le", L"utf-16le-bom", L"utf-16be",
    L"utf-16be-bom", L"shift_jis", L"euc-jp",   L"iso-2022-jp",
};

}  // namespace

// ---- [C10] ESC ESC ESC ----

TEST(escape_quit_aborts_m_x_like_c_g) {
  Session s;
  s.load(L"abc");
  s.keys(L"M-x");
  CHECK(s.ed.minibuffer_active());
  s.keys(L"ESC ESC ESC");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  // The same message as C-g.
  s.keys(L"M-x C-g");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
}

TEST(escape_quit_aborts_find_file) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-x C-f");
  CHECK(s.ed.minibuffer_active());
  s.type(L"x.txt");
  s.keys(L"ESC ESC ESC");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
}

TEST(escape_quit_deactivates_the_region) {
  Session s;
  s.load(L"abcdef");
  s.keys(L"C-SPC C-f C-f");
  CHECK(s.ed.buffer().mark_active);
  s.keys(L"ESC ESC ESC");
  CHECK(!s.ed.buffer().mark_active);
  CHECK_EQ(s.point(), size_t{2});
  CHECK_EQ(s.text(), std::wstring(L"abcdef"));
}

TEST(escape_quit_uses_up_a_prefix_argument) {
  Session s;
  s.load(L"abcdef");
  s.keys(L"C-u ESC ESC ESC C-f");
  CHECK_EQ(s.point(), size_t{1});  // the C-u did not reach C-f
}

TEST(escape_once_is_still_meta) {
  Session s;
  s.load(L"one two");
  s.keys(L"ESC f");
  CHECK_EQ(s.point(), size_t{3});
}

TEST(escape_twice_then_another_key_does_not_quit) {
  Session s;
  s.load(L"abc");
  s.keys(L"M-x");
  s.keys(L"ESC ESC");
  CHECK(s.ed.minibuffer_active());  // waits for the third key
  s.keys(L"f");
  CHECK(s.ed.minibuffer_active());
  CHECK(s.message() != std::wstring(L"Quit"));
}

TEST(escape_quit_binding_can_be_changed_or_removed) {
  Session s;
  s.load(L"abc");
  s.settings("{\"keys\": {\"M-ESC ESC\": null, \"C-c q\": \"keyboard-escape-quit\"}}");
  CHECK(s.ed.settings_errors().empty());
  s.keys(L"M-x");
  s.keys(L"ESC ESC ESC");
  CHECK(s.ed.minibuffer_active());
  s.keys(L"C-g");
  s.keys(L"M-x C-c q");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
}

TEST(escape_quit_is_a_registered_command) {
  const geditor::CommandInfo* info = geditor::find_command("keyboard-escape-quit");
  CHECK(info != nullptr && info->fn != nullptr);
}

// ---- [C11] The nine encodings ----

TEST(encodings_coding_names_are_the_nine) {
  std::vector<std::wstring> names;
  for (const wchar_t* n : geditor::encoding_names()) names.push_back(n);
  CHECK(names == kNineEncodings);
}

TEST(encodings_completion_candidates_are_the_nine) {
  for (const char* command : {"set-buffer-file-coding-system", "revert-buffer-with-coding-system",
                              "find-file-with-encoding"}) {
    const geditor::CommandInfo* info = geditor::find_command(command);
    CHECK(info != nullptr);
    if (info == nullptr) continue;
    const geditor::Param& p = info->params.back();
    CHECK(p.type == geditor::ParamType::kChoice);
    std::vector<std::wstring> choices;
    for (const wchar_t* c : p.choices) choices.push_back(c);
    CHECK(choices == kNineEncodings);
  }
}

TEST(encodings_menu_rows_are_the_nine) {
  std::vector<std::wstring> rows;
  for (const geditor::Menu& m : geditor::menu_bar_menus()) {
    for (const geditor::MenuItem& item : m.items) {
      if (item.kind != geditor::MenuItemKind::kSubmenu) continue;
      if (geditor::menu_text_without_mnemonic(item.text).find(L"Encoding for Saving") == std::wstring::npos) {
        continue;
      }
      for (const geditor::MenuItem& sub : item.submenu) rows.push_back(sub.argument != nullptr ? sub.argument : L"");
    }
  }
  CHECK(rows == kNineEncodings);
}

TEST(encodings_removed_name_is_an_invalid_setting) {
  Session s;
  s.settings("{\"new_file\": {\"encoding\": \"euc-kr\"}}");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{1});
  bool found = false;
  for (const geditor::Diagnostic& d : s.ed.settings_errors()) {
    if (d.message.find(L"\"new_file.encoding\" must be one of") != std::wstring::npos &&
        d.message.find(L"euc-kr") == std::wstring::npos && d.message.find(L"iso-2022-jp") != std::wstring::npos) {
      found = true;
    }
  }
  CHECK(found);
  CHECK_EQ(s.ed.config().new_file_encoding, std::wstring(L"utf-8"));
}
