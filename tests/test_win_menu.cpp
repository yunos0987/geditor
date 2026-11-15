// The Win32 menus of M3b built from the core model ([C3] / [C4]): the HMENU
// read back with GetMenuItemInfo after the WM_INITMENUPOPUP refresh has the
// texts, key texts, grey states and check / radio marks of the core for all
// three menus; the right-click HMENU comes from the same Edit definition; the
// headless menu question (decision B7) lists the built items.

#include <windows.h>

#include <string>
#include <vector>

#include "fake_host.h"
#include "headless.h"
#include "menu.h"
#include "menu_bar.h"
#include "question.h"

using geditor::MenuItem;
using geditor::MenuItemKind;
using geditor::MenuKeyTexts;
using geditor::win::MenuEntry;
using test::Session;

namespace {

// The core's view of the menu bar, in the order read_menu reads the HMENU.
struct Expected {
  std::wstring path;
  std::wstring text;
  std::wstring key_text;
  bool separator;
  bool submenu;
  bool enabled;
  bool checked;
  bool radio;
  UINT id;
};

void expect_items(const Session& s, std::span<const MenuItem> items, const std::wstring& prefix,
                  const MenuKeyTexts& keys, std::vector<Expected>* out) {
  for (const MenuItem& item : items) {
    Expected e{};
    e.separator = item.kind == MenuItemKind::kSeparator;
    e.submenu = item.kind == MenuItemKind::kSubmenu;
    const std::wstring name = e.separator ? std::wstring(L"-") : geditor::menu_text_without_mnemonic(item.text);
    e.path = prefix.empty() ? name : prefix + L" > " + name;
    if (!e.separator) {
      const geditor::MenuItemState state = geditor::menu_item_state(s.ed, item, keys);
      e.text = item.text;
      e.key_text = state.key_text;
      e.enabled = state.enabled;
      e.checked = state.checked;
      e.radio = item.check == geditor::MenuCheck::kRadio;
      e.id = geditor::win::menu_command_id(item);
    }
    out->push_back(e);
    if (e.submenu) expect_items(s, item.submenu, e.path, keys, out);
  }
}

std::vector<Expected> expected_bar(const Session& s) {
  std::vector<Expected> out;
  const MenuKeyTexts keys(s.ed);
  for (const geditor::Menu& m : geditor::menu_bar_menus()) {
    Expected title{};
    title.path = geditor::menu_text_without_mnemonic(m.title);
    title.text = m.title;
    title.submenu = true;
    title.enabled = true;
    out.push_back(title);
    expect_items(s, m.items, title.path, keys, &out);
  }
  return out;
}

// Compares the HMENU read back with the core, field by field.
void check_same(const std::vector<MenuEntry>& got, const std::vector<Expected>& want) {
  CHECK_EQ(got.size(), want.size());
  for (size_t i = 0; i < got.size() && i < want.size(); ++i) {
    const MenuEntry& g = got[i];
    const Expected& w = want[i];
    if (g.path != w.path || g.key_text != w.key_text || g.enabled != w.enabled || g.checked != w.checked) {
      std::printf("  item %zu %s\n", i, test::to_display(w.path).c_str());
    }
    CHECK_EQ(g.path, w.path);
    CHECK_EQ(g.separator, w.separator);
    CHECK_EQ(g.submenu, w.submenu);
    if (w.separator) continue;
    CHECK_EQ(g.text, w.text);
    CHECK_EQ(g.key_text, w.key_text);
    CHECK_EQ(g.enabled, w.enabled);
    CHECK_EQ(g.checked, w.checked);
    CHECK_EQ(g.radio, w.radio);
    if (w.id != 0) CHECK_EQ(g.id, w.id);
  }
}

// What WM_INITMENUPOPUP does for each popup of the bar.
void init_popups(HMENU bar, const Session& s) {
  for (int i = 0; i < GetMenuItemCount(bar); ++i) geditor::win::update_menu(GetSubMenu(bar, i), s.ed);
}

const MenuEntry* entry(const std::vector<MenuEntry>& entries, const std::wstring& path) {
  for (const MenuEntry& e : entries) {
    if (e.path == path) return &e;
  }
  return nullptr;
}

}  // namespace

TEST(win_menu_bar_hmenu_matches_the_core_model) {
  Session s;
  s.load(L"abc\n");
  HMENU bar = geditor::win::create_menu_bar(s.ed);
  CHECK(bar != nullptr);
  if (bar == nullptr) return;
  CHECK_EQ(GetMenuItemCount(bar), 3);
  check_same(geditor::win::read_menu(bar), expected_bar(s));
  const std::vector<MenuEntry> entries = geditor::win::read_menu(bar);
  const MenuEntry* undo = entry(entries, L"Edit > Undo");
  CHECK(undo != nullptr && undo->text == L"&Undo" && undo->key_text == L"C-S-z  C-/" && undo->enabled);
  const MenuEntry* find = entry(entries, L"Edit > Find...");
  CHECK(find != nullptr && !find->enabled);
  const MenuEntry* max = entry(entries, L"View > Wrap > Max");
  CHECK(max != nullptr && max->radio && max->checked);
  DestroyMenu(bar);
}

TEST(win_menu_init_popup_refreshes_keys_grey_and_marks) {
  Session s;
  s.load(L"abc\n");
  HMENU bar = geditor::win::create_menu_bar(s.ed);
  CHECK(bar != nullptr);
  if (bar == nullptr) return;
  // Change everything the refresh shows: keys, View marks, the minibuffer.
  s.settings(R"({"keys": {"C-z": "undo", "C-S-s": null, "C-x C-s": null}})");
  s.keys(L"M-x");
  s.type(L"toggle-line-numbers");
  s.keys(L"RET");
  s.keys(L"M-x");
  s.type(L"set-wrap-mode");
  s.keys(L"RET");
  s.type(L"window");
  s.keys(L"RET");
  CHECK_EQ(s.ed.wrap_mode(), geditor::WrapMode::kWindow);
  init_popups(bar, s);
  check_same(geditor::win::read_menu(bar), expected_bar(s));
  std::vector<MenuEntry> entries = geditor::win::read_menu(bar);
  CHECK(entry(entries, L"Edit > Undo")->key_text == L"C-z  C-/");
  CHECK(entry(entries, L"File > Save")->key_text.empty());
  CHECK(!entry(entries, L"View > Line Numbers")->checked);
  CHECK(entry(entries, L"View > Wrap > Window")->checked);
  CHECK(!entry(entries, L"View > Wrap > Max")->checked);
  // While the minibuffer is active every item is grey.
  s.keys(L"M-x");
  init_popups(bar, s);
  entries = geditor::win::read_menu(bar);
  check_same(entries, expected_bar(s));
  size_t grey = 0;
  for (const MenuEntry& e : entries) {
    if (!e.separator && e.id != 0) {
      CHECK(!e.enabled);
      ++grey;
    }
  }
  CHECK(grey > 40);
  s.keys(L"C-g");
  init_popups(bar, s);
  check_same(geditor::win::read_menu(bar), expected_bar(s));
  DestroyMenu(bar);
}

TEST(win_context_menu_hmenu_is_built_from_the_edit_definition) {
  Session s;
  s.load(L"abc");
  s.settings(R"({"keys": {"C-c c": "copy"}})");
  HMENU popup = geditor::win::create_context_menu(s.ed);
  HMENU bar = geditor::win::create_menu_bar(s.ed);
  CHECK(popup != nullptr && bar != nullptr);
  if (popup == nullptr || bar == nullptr) return;
  const std::vector<MenuEntry> context = geditor::win::read_menu(popup);
  const std::vector<MenuEntry> all = geditor::win::read_menu(bar);
  CHECK_EQ(context.size(), size_t{7});
  for (const MenuEntry& c : context) {
    const MenuEntry* e = entry(all, L"Edit > " + c.path);
    CHECK(e != nullptr);
    if (e == nullptr) continue;
    // The same item: id (command), text, key text, enabled state.
    CHECK_EQ(c.id, e->id);
    CHECK_EQ(c.text, e->text);
    CHECK_EQ(c.key_text, e->key_text);
    CHECK_EQ(c.enabled, e->enabled);
    CHECK_EQ(c.separator, e->separator);
  }
  // The shorter Windows-side key, and M-w (kill-ring-save) from the
  // same-action table in the Emacs slot.
  CHECK_EQ(context[4].key_text, std::wstring(L"C-S-c  M-w"));
  // The ids map back to the core items of the Edit menu.
  const MenuItem* undo = geditor::win::menu_item_for_command(context[0].id);
  CHECK(undo != nullptr && undo == &geditor::context_menu_items()[0]);
  CHECK(geditor::win::menu_item_for_command(0) == nullptr);
  CHECK(geditor::win::menu_item_for_command(geditor::win::kMenuCommandBase) == nullptr);
  CHECK(geditor::win::menu_item_for_command(geditor::win::kMenuCommandBase + 9999) == nullptr);
  DestroyMenu(popup);
  DestroyMenu(bar);
}

TEST(win_menu_key_text_with_an_ampersand_is_escaped) {
  // R2: "C-c &" must not turn into a mnemonic in the menu text.
  Session s;
  s.load(L"abc");
  s.settings(R"({"keys": {"C-c &": "toggle-line-numbers"}})");
  CHECK(s.ed.settings_errors().empty());
  const MenuItem* item = nullptr;
  for (const MenuItem& i : geditor::menu_bar_menus()[2].items) {
    if (i.command != nullptr && std::string(i.command) == "toggle-line-numbers") item = &i;
  }
  CHECK(item != nullptr);
  if (item == nullptr) return;
  CHECK_EQ(MenuKeyTexts(s.ed).text_for(*item), std::wstring(L"C-c &"));
  CHECK_EQ(geditor::win::menu_item_text(*item, L"C-c &"), std::wstring(L"Line &Numbers\tC-c &&"));
  HMENU bar = geditor::win::create_menu_bar(s.ed);
  CHECK(bar != nullptr);
  if (bar == nullptr) return;
  // The raw text of the built HMENU.
  HMENU view = GetSubMenu(bar, 2);
  wchar_t buffer[128] = {};
  GetMenuStringW(view, geditor::win::menu_command_id(*item), buffer, 128, MF_BYCOMMAND);
  CHECK_EQ(std::wstring(buffer), std::wstring(L"Line &Numbers\tC-c &&"));
  // Read back, the key is the core's again.
  const std::vector<MenuEntry> entries = geditor::win::read_menu(bar);
  const MenuEntry* e = entry(entries, L"View > Line Numbers");
  CHECK(e != nullptr && e->key_text == L"C-c &");
  // The refresh of WM_INITMENUPOPUP writes the same text.
  geditor::win::update_menu(view, s.ed);
  GetMenuStringW(view, geditor::win::menu_command_id(*item), buffer, 128, MF_BYCOMMAND);
  CHECK_EQ(std::wstring(buffer), std::wstring(L"Line &Numbers\tC-c &&"));
  DestroyMenu(bar);
}

TEST(win_menu_question_lists_items_and_takes_enabled_ones) {
  Session s;
  s.load(L"abc");
  HMENU popup = geditor::win::create_context_menu(s.ed);
  CHECK(popup != nullptr);
  if (popup == nullptr) return;
  const geditor::win::Question q = geditor::win::menu_question(
      geditor::win::QuestionKind::kContextMenu, geditor::win::read_menu(popup), POINT{12, 34});
  const std::wstring state = geditor::win::question_state(q);
  CHECK(state.find(L"question=context_menu\n") != std::wstring::npos);
  CHECK(state.find(L"menu_x=12\nmenu_y=34\n") != std::wstring::npos);
  CHECK(state.find(L"menu_item=Undo\tC-S-z  C-/\tenabled\n") != std::wstring::npos);
  CHECK(state.find(L"menu_item=-\t\tseparator\n") != std::wstring::npos);
  CHECK(state.find(L"button=Select All\n") != std::wstring::npos);
  CHECK(state.find(L"button=Cancel\n") != std::wstring::npos);
  CHECK_EQ(geditor::win::question_button_id(q, L"Copy"), static_cast<int>(geditor::win::menu_command_id(
                                                             geditor::context_menu_items()[4])));
  CHECK_EQ(geditor::win::question_button_id(q, L"Cancel"), IDCANCEL);
  // No such item, and the separator, are no buttons (the answer is rejected).
  CHECK_EQ(geditor::win::question_button_id(q, L"Frobnicate"), 0);
  CHECK_EQ(geditor::win::question_button_id(q, L"-"), 0);
  DestroyMenu(popup);

  // Not implemented items are listed but are no buttons.
  HMENU bar = geditor::win::create_menu_bar(s.ed);
  CHECK(bar != nullptr);
  if (bar == nullptr) return;
  const geditor::win::Question mq =
      geditor::win::menu_question(geditor::win::QuestionKind::kMenuBar, geditor::win::read_menu(bar), POINT{});
  const std::wstring bar_state = geditor::win::question_state(mq);
  CHECK(bar_state.find(L"question=menu_bar\n") != std::wstring::npos);
  CHECK(bar_state.find(L"menu_item=Edit > Go to Line...\tC-S-g  M-g g\tdisabled\n") != std::wstring::npos);
  CHECK(bar_state.find(L"menu_item=View > Wrap > Max\t\tenabled checked radio\n") != std::wstring::npos);
  CHECK(bar_state.find(L"menu_item=View > Wrap\t\tsubmenu enabled\n") != std::wstring::npos);
  CHECK_EQ(geditor::win::question_button_id(mq, L"Edit > Go to Line..."), 0);
  CHECK(geditor::win::question_button_id(mq, L"View > Wrap > None") != 0);
  CHECK_EQ(geditor::win::question_button_id(mq, L"View > Wrap"), 0);
  DestroyMenu(bar);
}

TEST(win_menu_bar_wraps_at_its_ends_through_the_system_menu) {
  // M3d D6: WM_NEXTMENU from the bar or one of its drop-downs with Left /
  // Right queues one more key (through the system menu to the other end);
  // the system menu itself, other keys, other menus, no bar and a window
  // without a system menu do not.
  using geditor::win::menu_bar_wraps;
  Session s;
  HMENU bar = geditor::win::create_menu_bar(s.ed);
  CHECK(bar != nullptr);
  if (bar == nullptr) return;
  HMENU file = GetSubMenu(bar, 0);
  HMENU view = GetSubMenu(bar, GetMenuItemCount(bar) - 1);
  CHECK(file != nullptr && view != nullptr);
  CHECK(menu_bar_wraps(bar, bar, VK_LEFT, true));
  CHECK(menu_bar_wraps(bar, bar, VK_RIGHT, true));
  CHECK(menu_bar_wraps(bar, file, VK_LEFT, true));
  CHECK(menu_bar_wraps(bar, view, VK_RIGHT, true));
  CHECK(!menu_bar_wraps(bar, file, VK_UP, true));
  CHECK(!menu_bar_wraps(bar, file, VK_DOWN, true));
  CHECK(!menu_bar_wraps(bar, file, VK_LEFT, false));
  CHECK(!menu_bar_wraps(nullptr, file, VK_LEFT, true));
  CHECK(!menu_bar_wraps(bar, nullptr, VK_LEFT, true));
  HMENU other = CreatePopupMenu();
  CHECK(!menu_bar_wraps(bar, other, VK_LEFT, true));  // e.g. the system menu
  // A submenu of a drop-down (View > Wrap) is not the bar either.
  HMENU wrap = nullptr;
  for (int i = 0; i < GetMenuItemCount(view) && wrap == nullptr; ++i) wrap = GetSubMenu(view, i);
  if (wrap != nullptr) CHECK(!menu_bar_wraps(bar, wrap, VK_LEFT, true));
  DestroyMenu(other);
  DestroyMenu(bar);
}
