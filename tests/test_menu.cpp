// The menu model of M3b (docs/PLAN.md 4.9): one row per item of the PLAN
// table ([M3-5] / [C2]), the key texts of decision B1, the enabled rules of
// decision B2, the View check and radio marks, the right-click menu from the
// same Edit definition ([M3-8] / [C4]) and menu-bar-open.

#include <cstdio>
#include <string>
#include <vector>

#include "commands.h"
#include "config.h"
#include "fake_host.h"
#include "menu.h"

using geditor::Menu;
using geditor::MenuCheck;
using geditor::MenuItem;
using geditor::MenuItemKind;
using geditor::MenuKeyTexts;
using test::Session;

namespace {

// One row per item of the PLAN 4.9 table, in menu order. `path` is the PLAN
// text ("─" rows are separators, "▶" rows are submenus); `key` is the key
// text that decision B1 picks from the built-in keymap layers (settings
// none): the shortest Windows compatible key, two spaces, the shortest Emacs
// key (ties to the first in the table); one key when only one slot has one.
// The PLAN item -> row cross-check is this table itself: every
// row names its PLAN text, and plan_table_covers_every_model_item checks
// that the model has exactly these items in this order.
struct PlanRow {
  const wchar_t* path;
  MenuItemKind kind;
  const char* command;
  const wchar_t* argument;
  MenuCheck check;
  bool implemented;  // in M3b (else greyed as not implemented yet)
  const wchar_t* key;
};

constexpr MenuItemKind kCmd = MenuItemKind::kCommand;
constexpr MenuItemKind kSep = MenuItemKind::kSeparator;
constexpr MenuItemKind kSub = MenuItemKind::kSubmenu;
constexpr MenuCheck kNo = MenuCheck::kNone;
constexpr MenuCheck kChk = MenuCheck::kCheck;
constexpr MenuCheck kRad = MenuCheck::kRadio;
constexpr const char* kCoding = "set-buffer-file-coding-system";

constexpr PlanRow kPlanRows[] = {
    // File
    {L"File > Open...", kCmd, "find-file", nullptr, kNo, true, L"C-x C-f"},
    // M4b: the encoding commands are implemented (the items are enabled).
    {L"File > Open with Encoding...", kCmd, "find-file-with-encoding", nullptr, kNo, true, L"C-x RET c"},
    {L"File > Reopen with Encoding...", kCmd, "revert-buffer-with-coding-system", nullptr, kNo, true, L"C-x RET r"},
    {L"File > ─", kSep, nullptr, nullptr, kNo, false, L""},
    {L"File > Save", kCmd, "save-buffer", nullptr, kNo, true, L"C-S-s  C-x C-s"},
    {L"File > Save As...", kCmd, "write-file", nullptr, kNo, true, L"C-x C-w"},
    {L"File > ─", kSep, nullptr, nullptr, kNo, false, L""},
    {L"File > Encoding for Saving ▶", kSub, nullptr, nullptr, kNo, true, L""},
    {L"File > Encoding for Saving > utf-8", kCmd, kCoding, L"utf-8", kRad, true, L""},
    {L"File > Encoding for Saving > utf-8-bom", kCmd, kCoding, L"utf-8-bom", kRad, true, L""},
    {L"File > Encoding for Saving > utf-16le", kCmd, kCoding, L"utf-16le", kRad, true, L""},
    {L"File > Encoding for Saving > utf-16le-bom", kCmd, kCoding, L"utf-16le-bom", kRad, true, L""},
    {L"File > Encoding for Saving > utf-16be", kCmd, kCoding, L"utf-16be", kRad, true, L""},
    {L"File > Encoding for Saving > utf-16be-bom", kCmd, kCoding, L"utf-16be-bom", kRad, true, L""},
    {L"File > Encoding for Saving > shift_jis", kCmd, kCoding, L"shift_jis", kRad, true, L""},
    {L"File > Encoding for Saving > euc-jp", kCmd, kCoding, L"euc-jp", kRad, true, L""},
    {L"File > Encoding for Saving > iso-2022-jp", kCmd, kCoding, L"iso-2022-jp", kRad, true, L""},
    // M3c (decision G9): windows-1252, gb18030, big5 and euc-kr were removed.
    {L"File > Line Endings ▶", kSub, nullptr, nullptr, kNo, true, L""},
    // M4a: set-eol is implemented (the items are enabled).
    {L"File > Line Endings > LF", kCmd, "set-eol", L"lf", kRad, true, L""},
    {L"File > Line Endings > CRLF", kCmd, "set-eol", L"crlf", kRad, true, L""},
    {L"File > Line Endings > CR", kCmd, "set-eol", L"cr", kRad, true, L""},
    {L"File > ─", kSep, nullptr, nullptr, kNo, false, L""},
    {L"File > Read Only", kCmd, "read-only-mode", nullptr, kChk, true, L"C-x C-q"},
    {L"File > ─", kSep, nullptr, nullptr, kNo, false, L""},
    {L"File > Settings", kCmd, "find-user-init-file", nullptr, kNo, true, L""},
    {L"File > Exit", kCmd, "quit", nullptr, kNo, true, L"C-S-w  C-x C-c"},
    // Edit
    {L"Edit > Undo", kCmd, "undo", nullptr, kNo, true, L"C-S-z  C-/"},
    {L"Edit > Redo", kCmd, "redo", nullptr, kNo, true, L"C-S-y  C-?"},
    {L"Edit > ─", kSep, nullptr, nullptr, kNo, false, L""},
    // The Emacs slot of these four comes from the same-action table
    // (kill-region / kill-ring-save / yank / mark-whole-buffer).
    {L"Edit > Cut", kCmd, "cut", nullptr, kNo, true, L"C-S-x  C-w"},
    {L"Edit > Copy", kCmd, "copy", nullptr, kNo, true, L"C-S-c  M-w"},
    {L"Edit > Paste", kCmd, "paste", nullptr, kNo, true, L"C-S-v  C-y"},
    {L"Edit > Select All", kCmd, "select-all", nullptr, kNo, true, L"C-S-a  C-x h"},
    {L"Edit > ─", kSep, nullptr, nullptr, kNo, false, L""},
    // M5: isearch-forward, query-replace and goto-line are implemented.
    {L"Edit > Find...", kCmd, "isearch-forward", nullptr, kNo, true, L"C-S-f  C-s"},
    {L"Edit > Replace...", kCmd, "query-replace", nullptr, kNo, true, L"C-S-h  M-%"},
    {L"Edit > Go to Line...", kCmd, "goto-line", nullptr, kNo, true, L"C-S-g  M-g g"},
    // View
    {L"View > Line Numbers", kCmd, "toggle-line-numbers", nullptr, kChk, true, L""},
    {L"View > Line Ending Marks", kCmd, "toggle-eol-marks", nullptr, kChk, true, L""},
    {L"View > Whitespace Marks", kCmd, "toggle-whitespace-marks", nullptr, kChk, true, L""},
    {L"View > Wrap ▶", kSub, nullptr, nullptr, kNo, true, L""},
    {L"View > Wrap > None", kCmd, "set-wrap-mode", L"none", kRad, true, L""},
    {L"View > Wrap > Window", kCmd, "set-wrap-mode", L"window", kRad, true, L""},
    {L"View > Wrap > Fixed", kCmd, "set-wrap-mode", L"fixed", kRad, true, L""},
    {L"View > Wrap > Max", kCmd, "set-wrap-mode", L"max", kRad, true, L""},
    {L"View > ─", kSep, nullptr, nullptr, kNo, false, L""},
    {L"View > Zoom In", kCmd, "text-scale-increase", nullptr, kNo, true, L"C-x C-+"},
    {L"View > Zoom Out", kCmd, "text-scale-decrease", nullptr, kNo, true, L"C-x C--"},
    {L"View > Reset Zoom", kCmd, "text-scale-reset", nullptr, kNo, true, L"C-x C-0"},
};

// The items of the model in menu order with their PLAN-style paths
// (separators "─", submenus with " ▶").
struct ModelRow {
  std::wstring path;
  const MenuItem* item;
};

void collect(std::span<const MenuItem> items, const std::wstring& prefix, std::vector<ModelRow>* out) {
  for (const MenuItem& item : items) {
    std::wstring name = item.kind == MenuItemKind::kSeparator
                            ? std::wstring(L"─")
                            : geditor::menu_text_without_mnemonic(item.text);
    if (item.kind == MenuItemKind::kSubmenu) {
      out->push_back(ModelRow{prefix + L" > " + name + L" ▶", &item});
      collect(item.submenu, prefix + L" > " + name, out);
    } else {
      out->push_back(ModelRow{prefix + L" > " + name, &item});
    }
  }
}

std::vector<ModelRow> model_rows() {
  std::vector<ModelRow> rows;
  for (const Menu& m : geditor::menu_bar_menus()) collect(m.items, geditor::menu_text_without_mnemonic(m.title), &rows);
  return rows;
}

const MenuItem* find_item(const std::wstring& path) {
  for (const ModelRow& r : model_rows()) {
    if (r.path == path) return r.item;
  }
  std::printf("  no menu item %s\n", test::to_display(path).c_str());
  return nullptr;
}

std::wstring key_text(const Session& s, const wchar_t* path) {
  const MenuItem* item = find_item(path);
  return item != nullptr ? MenuKeyTexts(s.ed).text_for(*item) : std::wstring(L"<missing>");
}

bool checked(const Session& s, const wchar_t* path) {
  const MenuItem* item = find_item(path);
  return item != nullptr && geditor::menu_item_checked(s.ed, *item);
}

bool enabled(const Session& s, const MenuItem& item) {
  return geditor::menu_item_enabled(s.ed, item);
}

void run_item(Session& s, const wchar_t* path) {
  const MenuItem* item = find_item(path);
  CHECK(item != nullptr);
  if (item != nullptr) CHECK(s.ed.execute_menu_item(*item));
}

}  // namespace

// ---- [C2] one row per PLAN 4.9 item ----

TEST(menu_plan_table_covers_every_model_item) {
  const std::vector<ModelRow> rows = model_rows();
  CHECK_EQ(rows.size(), std::size(kPlanRows));
  for (size_t i = 0; i < rows.size() && i < std::size(kPlanRows); ++i) {
    if (rows[i].path != kPlanRows[i].path) std::printf("  row %zu\n", i);
    CHECK_EQ(rows[i].path, std::wstring(kPlanRows[i].path));
  }
  // The titles: File, Edit, View with mnemonics.
  const auto menus = geditor::menu_bar_menus();
  CHECK_EQ(menus.size(), size_t{3});
  CHECK_EQ(std::wstring(menus[0].title), std::wstring(L"&File"));
  CHECK_EQ(std::wstring(menus[1].title), std::wstring(L"&Edit"));
  CHECK_EQ(std::wstring(menus[2].title), std::wstring(L"&View"));
}

TEST(menu_each_plan_row_has_its_command_argument_and_mark) {
  const std::vector<ModelRow> rows = model_rows();
  for (size_t i = 0; i < rows.size() && i < std::size(kPlanRows); ++i) {
    const PlanRow& want = kPlanRows[i];
    const MenuItem& item = *rows[i].item;
    CHECK_EQ(item.kind, want.kind);
    CHECK_EQ(item.check, want.check);
    if (want.kind == MenuItemKind::kCommand) {
      CHECK(item.command != nullptr && std::string(item.command) == want.command);
      const geditor::CommandInfo* info = geditor::find_command(want.command);
      if (info == nullptr) std::printf("  not registered: %s\n", want.command);
      CHECK(info != nullptr);
      if (info != nullptr) CHECK_EQ(info->fn != nullptr, want.implemented);
      CHECK_EQ(item.argument != nullptr, want.argument != nullptr);
      if (item.argument != nullptr && want.argument != nullptr) {
        CHECK_EQ(std::wstring(item.argument), std::wstring(want.argument));
        // An argument is one the command accepts.
        CHECK(info != nullptr && info->params.size() == 1);
        if (info != nullptr && info->params.size() == 1) {
          bool known = false;
          for (const wchar_t* c : info->params[0].choices) known = known || std::wstring(c) == want.argument;
          CHECK(known);
        }
      }
      // Items ending in "..." read their input in the minibuffer (PLAN 4.9).
      const std::wstring text = geditor::menu_text_without_mnemonic(item.text);
      if (text.ends_with(L"...")) {
        // find-file / write-file read a file name; isearch-forward and
        // query-replace (M5) read the search text; the others take
        // parameters.
        const std::string name = want.command;
        CHECK(info != nullptr && (!info->params.empty() || name == "find-file" || name == "write-file" ||
                                  name == "isearch-forward" || name == "query-replace"));
      }
    } else {
      CHECK(item.command == nullptr);
    }
    CHECK_EQ(geditor::menu_item_id(item) > 0, true);
    CHECK(geditor::menu_item_for_id(geditor::menu_item_id(item)) == &item);
  }
}

TEST(menu_mnemonics_are_unique_within_each_menu) {
  auto letters = [](std::span<const MenuItem> items) {
    std::wstring seen;
    bool ok = true;
    for (const MenuItem& item : items) {
      if (item.text == nullptr) continue;
      const std::wstring t = item.text;
      const size_t amp = t.find(L'&');
      if (amp == std::wstring::npos || amp + 1 >= t.size()) continue;
      const wchar_t c = static_cast<wchar_t>(towlower(t[amp + 1]));
      if (seen.find(c) != std::wstring::npos) ok = false;
      seen += c;
    }
    return ok;
  };
  for (const Menu& m : geditor::menu_bar_menus()) {
    CHECK(letters(m.items));
    for (const MenuItem& item : m.items) {
      if (item.kind == MenuItemKind::kSubmenu) CHECK(letters(item.submenu));
    }
  }
}

TEST(menu_key_texts_follow_decision_b1) {
  Session s;
  s.load(L"abc");
  const MenuKeyTexts keys(s.ed);
  const std::vector<ModelRow> rows = model_rows();
  for (size_t i = 0; i < rows.size() && i < std::size(kPlanRows); ++i) {
    const std::wstring got = keys.text_for(*rows[i].item);
    if (got != kPlanRows[i].key) std::printf("  %s\n", test::to_display(kPlanRows[i].path).c_str());
    CHECK_EQ(got, std::wstring(kPlanRows[i].key));
  }
}

TEST(menu_key_texts_cover_each_slot_case) {
  Session s;
  s.load(L"abc");
  // Both slots, only Emacs, neither (only Windows: below, with C-w unbound).
  CHECK_EQ(key_text(s, L"Edit > Undo"), std::wstring(L"C-S-z  C-/"));
  CHECK_EQ(key_text(s, L"File > Open..."), std::wstring(L"C-x C-f"));
  CHECK_EQ(key_text(s, L"File > Settings"), std::wstring(L""));
  s.settings(R"({"keys": {"C-w": null}})");
  CHECK(s.ed.settings_errors().empty());
  CHECK_EQ(key_text(s, L"Edit > Cut"), std::wstring(L"C-S-x"));
  // The same key in both slots shows once.
  s.settings(R"({"keys": {"C-/": "undo"}})");
  CHECK(s.ed.settings_errors().empty());
  CHECK_EQ(key_text(s, L"Edit > Undo"), std::wstring(L"C-/"));
  // A key containing '&' (the core text keeps one '&'; menu_bar escapes it).
  s.settings(R"({"keys": {"C-c &": "cut"}})");
  CHECK(s.ed.settings_errors().empty());
  CHECK_EQ(key_text(s, L"Edit > Cut"), std::wstring(L"C-S-x  C-w"));  // the shorter Windows key
  s.settings(R"({"keys": {"C-c &": "toggle-line-numbers"}})");
  CHECK_EQ(key_text(s, L"View > Line Numbers"), std::wstring(L"C-c &"));
}

TEST(menu_key_texts_change_with_a_settings_reload) {
  Session s;
  s.load(L"abc");
  CHECK_EQ(key_text(s, L"Edit > Undo"), std::wstring(L"C-S-z  C-/"));
  CHECK_EQ(key_text(s, L"File > Save"), std::wstring(L"C-S-s  C-x C-s"));
  // A settings key replaces the Windows slot (it wins the tie of one-key
  // sequences); unbind save-buffer everywhere; give Wrap > None a key with
  // its argument.
  s.settings(R"({"keys": {"C-z": "undo", "C-S-s": null, "C-x C-s": null, "<f5>": ["set-wrap-mode", "none"]}})");
  CHECK(s.ed.settings_errors().empty());
  CHECK_EQ(key_text(s, L"Edit > Undo"), std::wstring(L"C-z  C-/"));
  CHECK_EQ(key_text(s, L"File > Save"), std::wstring(L""));
  CHECK_EQ(key_text(s, L"View > Wrap > None"), std::wstring(L"<f5>"));
  CHECK_EQ(key_text(s, L"View > Wrap > Max"), std::wstring(L""));
  // Back to the defaults.
  s.settings("{}");
  CHECK_EQ(key_text(s, L"Edit > Undo"), std::wstring(L"C-S-z  C-/"));
  CHECK_EQ(key_text(s, L"File > Save"), std::wstring(L"C-S-s  C-x C-s"));
  CHECK_EQ(key_text(s, L"View > Wrap > None"), std::wstring(L""));
}

TEST(menu_key_text_skips_shadowed_and_sequence_bindings) {
  Session s;
  s.load(L"abc");
  // A settings binding overrides the Emacs key C-/: the Emacs slot takes the
  // next one-key Emacs key (C-_); C-S-z now runs redo, so the Windows slot of
  // undo is empty (the sequence binding does not count).
  s.settings(R"({"keys": {"C-/": "redo", "C-S-z": "redo", "C-c u": [["undo"], ["undo"]]}})");
  CHECK(s.ed.settings_errors().empty());
  CHECK_EQ(key_text(s, L"Edit > Undo"), std::wstring(L"C-_"));
  CHECK_EQ(key_text(s, L"Edit > Redo"), std::wstring(L"C-/  C-?"));
  // An overridden Emacs key with no other Emacs key: the slot disappears.
  s.settings(R"({"keys": {"C-x C-s": "end-of-line"}})");
  CHECK_EQ(key_text(s, L"File > Save"), std::wstring(L"C-S-s"));
  // A key bound with an argument does not show on the argument-less item.
  s.settings(R"({"keys": {"C-c i": ["insert-text", "x"], "C-c z": ["undo"]}})");
  CHECK_EQ(key_text(s, L"Edit > Undo"), std::wstring(L"C-S-z  C-/"));
}

// [C9] of v2: the same-action keys of the Emacs slot follow the v1 rules.
TEST(menu_key_texts_same_action_keys_follow_the_emacs_slot_rules) {
  Session s;
  s.load(L"abc");
  // A setting that rebinds C-w removes it from Cut's Emacs slot.
  s.settings(R"({"keys": {"C-w": "end-of-line"}})");
  CHECK(s.ed.settings_errors().empty());
  CHECK_EQ(key_text(s, L"Edit > Cut"), std::wstring(L"C-S-x"));
  CHECK_EQ(key_text(s, L"Edit > Copy"), std::wstring(L"C-S-c  M-w"));
  // A longer settings key of kill-region does not replace C-w (and settings
  // keys of the same-action command are not Emacs keys).
  s.settings(R"({"keys": {"C-c k": "kill-region"}})");
  CHECK(s.ed.settings_errors().empty());
  CHECK_EQ(key_text(s, L"Edit > Cut"), std::wstring(L"C-S-x  C-w"));
  // Same sequence in both slots: shown once.
  s.settings(R"({"keys": {"C-y": "paste"}})");
  CHECK_EQ(key_text(s, L"Edit > Paste"), std::wstring(L"C-y"));
  // Unbinding the Windows key leaves the same-action Emacs key alone.
  s.settings(R"({"keys": {"C-S-a": null}})");
  CHECK_EQ(key_text(s, L"Edit > Select All"), std::wstring(L"C-x h"));
  // No other item gains an Emacs key: the items without an own Emacs key
  // keep their v1 texts.
  s.settings("{}");
  CHECK_EQ(key_text(s, L"File > Settings"), std::wstring(L""));
  CHECK_EQ(key_text(s, L"Edit > Undo"), std::wstring(L"C-S-z  C-/"));
  CHECK_EQ(key_text(s, L"Edit > Redo"), std::wstring(L"C-S-y  C-?"));
  CHECK_EQ(key_text(s, L"Edit > Replace..."), std::wstring(L"C-S-h  M-%"));
  CHECK_EQ(key_text(s, L"View > Line Numbers"), std::wstring(L""));
}

// The rule of decision B1 written independently of menu.cpp, from the layer
// tables and the settings: checked against every PLAN 4.9 item under several
// settings.
namespace {

// The same-action table of the v2 addendum (two-way), written from the
// packet, independently of menu.cpp.
constexpr std::pair<const char*, const char*> kSameAction[] = {
    {"cut", "kill-region"}, {"copy", "kill-ring-save"}, {"paste", "yank"}, {"select-all", "mark-whole-buffer"}};

std::wstring rule_text(const Session& s, const MenuItem& item) {
  if (item.kind != MenuItemKind::kCommand) return std::wstring();
  const geditor::CommandCall call = geditor::menu_item_call(item);
  // The calls whose Emacs keys count: the item's own, then same-action ones.
  std::vector<geditor::CommandCall> emacs_calls = {call};
  if (call.args.empty()) {
    for (const auto& [a, b] : kSameAction) {
      if (call.name == a) emacs_calls.push_back(geditor::CommandCall{b, {}});
      if (call.name == b) emacs_calls.push_back(geditor::CommandCall{a, {}});
    }
  }
  const geditor::Keymap& map = s.ed.keymap(geditor::KeymapKind::kGlobal);
  auto runs_call = [&](const geditor::KeySequence& keys, const geditor::Binding& b, const geditor::CommandCall& c) {
    if (b.sequence || b.calls.size() != 1 || !(b.calls.front() == c)) return false;
    const geditor::Keymap::Lookup r = map.lookup(keys);
    return r.result == geditor::Keymap::Result::kCommand && r.binding != nullptr && *r.binding == b;
  };
  auto runs = [&](const geditor::KeySequence& keys, const geditor::Binding& b) { return runs_call(keys, b, call); };
  std::vector<geditor::KeySequence> windows;  // in tie order
  for (const geditor::KeySetting& k : s.ed.config().keys[static_cast<size_t>(geditor::KeymapKind::kGlobal)]) {
    if (!k.unbind && runs(k.keys, k.binding)) windows.push_back(k.keys);
  }
  std::vector<geditor::KeySequence> emacs;  // the item's own keys first (ties)
  for (const auto& b : geditor::default_compat_bindings()) {
    const geditor::KeySequence keys = *geditor::parse_key_sequence(b.keys);
    if (runs(keys, geditor::command_binding(b.command))) windows.push_back(keys);
  }
  for (const geditor::CommandCall& c : emacs_calls) {
    for (const auto& b : geditor::default_global_bindings()) {
      const geditor::KeySequence keys = *geditor::parse_key_sequence(b.keys);
      if (runs_call(keys, geditor::command_binding(b.command), c)) emacs.push_back(keys);
    }
  }
  auto shortest = [](const std::vector<geditor::KeySequence>& v) {
    const geditor::KeySequence* best = nullptr;
    for (const auto& k : v) {
      if (best == nullptr || k.size() < best->size()) best = &k;
    }
    return best == nullptr ? std::wstring() : geditor::key_sequence_to_string(*best);
  };
  const std::wstring w = shortest(windows);
  const std::wstring e = shortest(emacs);
  if (w.empty()) return e;
  if (e.empty() || e == w) return w;
  return w + L"  " + e;
}

}  // namespace

TEST(menu_key_texts_match_the_rule_for_every_item) {
  const char* settings[] = {
      "{}",
      R"({"keys": {"C-z": "undo", "C-S-s": null, "C-x C-s": null, "<f5>": ["set-wrap-mode", "none"]}})",
      R"({"keys": {"C-/": "redo", "C-S-z": "redo", "C-c &": "copy", "C-x C-q": null}})",
      R"({"keys": {"C-/": "undo", "C-c C-c": "quit", "<f6>": "toggle-whitespace-marks"}})",
      // v2: same-action keys rebound, a longer kill-region key, a shared key.
      R"({"keys": {"C-w": "end-of-line", "C-c k": "kill-region", "C-y": "paste", "C-x h": null}})",
      R"({"keys": {"M-w": "copy", "C-S-v": null, "C-S-a": "mark-whole-buffer"}})",
  };
  for (const char* text : settings) {
    Session s;
    s.load(L"abc");
    s.settings(text);
    CHECK(s.ed.settings_errors().empty());
    const MenuKeyTexts keys(s.ed);
    size_t checked_items = 0;
    for (const ModelRow& r : model_rows()) {
      const std::wstring got = keys.text_for(*r.item);
      const std::wstring want = rule_text(s, *r.item);
      if (got != want) std::printf("  %s: %s\n", text, test::to_display(r.path).c_str());
      CHECK_EQ(got, want);
      ++checked_items;
    }
    CHECK_EQ(checked_items, std::size(kPlanRows));
  }
}

// ---- [C2] View check and radio marks ----

TEST(menu_view_marks_follow_commands_and_settings) {
  Session s;
  s.load(L"abc\n");
  CHECK(checked(s, L"View > Line Numbers"));
  CHECK(checked(s, L"View > Line Ending Marks"));
  CHECK(checked(s, L"View > Whitespace Marks"));
  CHECK(checked(s, L"View > Wrap > Max"));
  CHECK(!checked(s, L"View > Wrap > None"));
  run_item(s, L"View > Line Numbers");
  run_item(s, L"View > Line Ending Marks");
  run_item(s, L"View > Whitespace Marks");
  run_item(s, L"View > Wrap > None");
  CHECK(!checked(s, L"View > Line Numbers"));
  CHECK(!checked(s, L"View > Line Ending Marks"));
  CHECK(!checked(s, L"View > Whitespace Marks"));
  CHECK(checked(s, L"View > Wrap > None"));
  CHECK(!checked(s, L"View > Wrap > Max"));
  CHECK(!checked(s, L"View > Wrap > Window"));
  CHECK(!checked(s, L"View > Wrap > Fixed"));
  CHECK_EQ(s.message(), std::wstring(L"Wrap: none"));
  // The same commands from M-x.
  s.keys(L"M-x");
  s.type(L"toggle-line-numbers");
  s.keys(L"RET");
  CHECK(checked(s, L"View > Line Numbers"));
  // A settings reload that changes the file's values ends the window's
  // values (decision E6).
  s.settings(R"({"line_numbers": false, "wrap": {"mode": "window"}, "eol_marks": {"show": true}})");
  CHECK(!checked(s, L"View > Line Numbers"));
  CHECK(checked(s, L"View > Wrap > Window"));
  CHECK(!checked(s, L"View > Wrap > None"));
  s.settings(R"({"wrap": {"mode": "fixed"}})");
  CHECK(checked(s, L"View > Wrap > Fixed"));
  CHECK(checked(s, L"View > Line Numbers"));
}

// M4a computes Read Only and Line Endings (M4-9, test_m4a_menu.cpp); the
// Encoding for Saving marks wait for M4b.
// M4b: Encoding for Saving marks the buffer's encoding (test_m4b_encoding.cpp
// has every command and encoding).
TEST(menu_file_marks_follow_the_buffer) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-x C-q");
  CHECK(s.ed.buffer().read_only);
  CHECK(checked(s, L"File > Read Only"));
  CHECK(checked(s, L"File > Line Endings > LF"));
  CHECK(checked(s, L"File > Encoding for Saving > utf-8"));
  CHECK(!checked(s, L"File > Encoding for Saving > utf-8-bom"));
}

// ---- [C2] enabled rules (decision B2) ----

TEST(menu_not_implemented_items_are_disabled) {
  Session s;
  s.load(L"abc");
  const std::vector<ModelRow> rows = model_rows();
  for (size_t i = 0; i < rows.size() && i < std::size(kPlanRows); ++i) {
    const PlanRow& want = kPlanRows[i];
    const bool expected = want.kind != MenuItemKind::kSeparator && want.implemented;
    if (enabled(s, *rows[i].item) != expected) std::printf("  %s\n", test::to_display(want.path).c_str());
    CHECK_EQ(enabled(s, *rows[i].item), expected);
  }
  // Since M5 every command of the menus is implemented. Running a disabled
  // item (here during isearch, which greys every item) does nothing.
  const MenuItem* goto_line = find_item(L"Edit > Go to Line...");
  s.keys(L"C-s");
  CHECK(s.ed.isearch_active());
  CHECK(goto_line != nullptr && !s.ed.execute_menu_item(*goto_line));
  CHECK(!s.ed.minibuffer_active());
  CHECK(s.ed.isearch_active());
}

TEST(menu_every_item_is_disabled_while_the_minibuffer_is_active) {
  Session s;
  s.load(L"abc");
  s.keys(L"M-x");
  CHECK(s.ed.minibuffer_active());
  for (const ModelRow& r : model_rows()) {
    if (enabled(s, *r.item)) std::printf("  %s\n", test::to_display(r.path).c_str());
    CHECK(!enabled(s, *r.item));
  }
  for (const MenuItem& item : geditor::context_menu_items()) CHECK(!enabled(s, item));
  const MenuItem* undo = find_item(L"Edit > Undo");
  CHECK(undo != nullptr && !s.ed.execute_menu_item(*undo));
  CHECK(s.ed.minibuffer_active());
  s.keys(L"C-g");
  CHECK(undo != nullptr && enabled(s, *undo));
  // C-x C-f's minibuffer greys the items too.
  s.keys(L"C-x C-f");
  CHECK(s.ed.minibuffer_active());
  CHECK(undo != nullptr && !enabled(s, *undo));
}

TEST(menu_cut_and_copy_are_enabled_without_a_region) {
  Session s;
  s.load(L"abc");
  CHECK(enabled(s, *find_item(L"Edit > Cut")));
  CHECK(enabled(s, *find_item(L"Edit > Copy")));
  // Emacs-style: the command reports the problem, as from M-x.
  run_item(s, L"Edit > Cut");
  const std::wstring from_menu = s.message();
  s.keys(L"M-x");
  s.type(L"cut");
  s.keys(L"RET");
  CHECK_EQ(from_menu, std::wstring(L"The mark is not set now, so there is no region"));
  CHECK_EQ(s.message(), from_menu);
  CHECK_EQ(s.text(), std::wstring(L"abc"));
}

// ---- running items ----

TEST(menu_items_run_their_commands_with_one_undo_group) {
  Session s;
  s.load(L"one two");
  s.set_point(0);
  run_item(s, L"Edit > Select All");
  CHECK(s.ed.buffer().mark_active);
  CHECK_EQ(s.point(), size_t{7});
  run_item(s, L"Edit > Copy");
  CHECK_EQ(s.host.clipboard, std::wstring(L"one two"));
  run_item(s, L"Edit > Select All");
  run_item(s, L"Edit > Cut");
  CHECK_EQ(s.text(), std::wstring(L""));
  run_item(s, L"Edit > Paste");
  run_item(s, L"Edit > Paste");
  CHECK_EQ(s.text(), std::wstring(L"one twoone two"));
  run_item(s, L"Edit > Undo");
  CHECK_EQ(s.text(), std::wstring(L"one two"));
  run_item(s, L"Edit > Redo");
  CHECK_EQ(s.text(), std::wstring(L"one twoone two"));
  CHECK_EQ(s.message(), std::wstring(L"Redo"));
}

TEST(menu_items_take_the_prefix_argument) {
  Session s;
  s.load(L"");
  s.type(L"a");
  s.keys(L"RET");
  s.type(L"b");
  s.keys(L"RET");
  s.type(L"c");
  CHECK_EQ(s.text(), std::wstring(L"a\nb\nc"));
  // C-u 2 then Edit > Undo undoes two groups.
  s.keys(L"C-u 2");
  run_item(s, L"Edit > Undo");
  CHECK_EQ(s.text(), std::wstring(L"a\nb"));
  CHECK(s.ed.pending_keys_text().empty());
  // A pending prefix key is dropped.
  s.keys(L"C-x");
  CHECK(!s.ed.pending_keys_text().empty());
  run_item(s, L"View > Zoom In");
  CHECK(s.ed.pending_keys_text().empty());
  CHECK_EQ(s.ed.text_scale(), 1);
  run_item(s, L"View > Reset Zoom");
  CHECK_EQ(s.ed.text_scale(), 0);
  run_item(s, L"View > Zoom Out");
  CHECK_EQ(s.ed.text_scale(), -1);
}

TEST(menu_settings_item_runs_find_user_init_file_and_exit_runs_quit) {
  Session s;  // an empty untitled buffer: replaced without a question
  s.ed.set_settings_path(test::kSettingsPath);
  run_item(s, L"File > Settings");
  // No settings file: a new buffer with the template (decision B10).
  CHECK_EQ(s.ed.buffer().path, std::wstring(test::kSettingsPath));
  CHECK(s.modified());
  CHECK_EQ(s.host.open_dialogs, 0);
  run_item(s, L"File > Exit");
  CHECK_EQ(s.host.save_dialogs, 1);  // the modified template asks first (Cancel)
  CHECK_EQ(s.host.quit_calls, 0);
  run_item(s, L"File > Open...");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Find file: "));
}

TEST(menu_save_and_read_only_items) {
  Session s;
  s.load(L"abc");
  s.type(L"x");
  run_item(s, L"File > Save");
  CHECK(!s.modified());
  CHECK_EQ(s.host.file(L"C:\\work\\t.txt"), std::string("xabc"));
  run_item(s, L"File > Read Only");
  CHECK(s.ed.buffer().read_only);
  run_item(s, L"File > Save As...");
  CHECK(s.ed.minibuffer_active());
}

// ---- [C4] the right-click menu comes from the Edit definition ----

TEST(menu_context_menu_is_the_start_of_the_edit_menu) {
  const auto context = geditor::context_menu_items();
  const Menu& edit = geditor::menu_bar_menus()[1];
  CHECK_EQ(context.size(), size_t{7});
  const wchar_t* expected[] = {L"Undo", L"Redo", L"─", L"Cut", L"Copy", L"Paste", L"Select All"};
  for (size_t i = 0; i < context.size(); ++i) {
    // The same objects: one definition for both menus.
    CHECK(&context[i] == &edit.items[i]);
    const std::wstring text = context[i].text == nullptr ? std::wstring(L"─")
                                                          : geditor::menu_text_without_mnemonic(context[i].text);
    CHECK_EQ(text, std::wstring(expected[i]));
    CHECK_EQ(geditor::menu_item_id(context[i]), geditor::menu_item_id(edit.items[i]));
  }
  Session s;
  s.load(L"abc");
  s.settings(R"({"keys": {"C-z": "undo"}})");
  const MenuKeyTexts keys(s.ed);
  for (size_t i = 0; i < context.size(); ++i) {
    const geditor::MenuItemState a = geditor::menu_item_state(s.ed, context[i], keys);
    const geditor::MenuItemState b = geditor::menu_item_state(s.ed, edit.items[i], keys);
    CHECK_EQ(a.key_text, b.key_text);
    CHECK_EQ(a.enabled, b.enabled);
  }
  CHECK_EQ(keys.text_for(context[0]), std::wstring(L"C-z  C-/"));
}

// ---- menu-bar-open ----

TEST(menu_bar_open_asks_the_host_or_says_it_is_hidden) {
  Session s;
  s.load(L"abc");
  s.keys(L"<f10>");
  CHECK_EQ(s.host.menu_bar_opens, 1);
  CHECK_EQ(s.message(), std::wstring(L""));
  s.settings(R"({"menu_bar": false})");
  s.keys(L"<f10>");
  CHECK_EQ(s.host.menu_bar_opens, 1);
  CHECK_EQ(s.message(), std::wstring(L"Menu bar is hidden"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  s.settings(R"({"menu_bar": true})");
  s.keys(L"M-x");
  s.type(L"menu-bar-open");
  s.keys(L"RET");
  CHECK_EQ(s.host.menu_bar_opens, 2);
}

TEST(menu_text_without_mnemonic_removes_the_ampersand) {
  CHECK_EQ(geditor::menu_text_without_mnemonic(L"&Undo"), std::wstring(L"Undo"));
  CHECK_EQ(geditor::menu_text_without_mnemonic(L"Save &As..."), std::wstring(L"Save As..."));
  CHECK_EQ(geditor::menu_text_without_mnemonic(L"A&&B"), std::wstring(L"A&B"));
  CHECK_EQ(geditor::menu_text_without_mnemonic(L"x&"), std::wstring(L"x&"));
}
