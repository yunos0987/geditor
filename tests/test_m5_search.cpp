// M5 search and replace (docs/PLAN.md 4.4 "during isearch", "during
// query-replace", 2 "unit of search" / "case", 5 [M5-1] - [M5-7]): every
// isearch key ([C1]), every query-replace key and the one undo unit ([C2]),
// regexps ([C3]), search.case_fold and M-c ([C4]), the Windows keys ([C6]),
// the menus ([C7]), keys of other layers inside the modes ([C8]), the
// highlights the core gives the renderer ([C9]), goto-line ([C10]) and
// PCRE2 not running at start-up ([C11]).

#include <cstdio>
#include <string>
#include <vector>

#include "commands.h"
#include "fake_host.h"
#include "menu.h"
#include "search.h"

using geditor::MenuItem;
using geditor::MenuItemKind;
using geditor::SearchMatch;
using test::Session;

namespace {

// "M-% FROM RET TO RET" (query-replace reads both in the minibuffer).
void start_query_replace(Session& s, const wchar_t* keys, std::wstring_view from, std::wstring_view to) {
  s.keys(keys);
  s.type(from);
  s.keys(L"RET");
  s.type(to);
  s.keys(L"RET");
}

std::wstring echo(const Session& s) {
  return s.ed.echo_area_text();
}

const MenuItem* find_item(std::span<const MenuItem> items, const std::wstring& path, const std::wstring& prefix) {
  for (const MenuItem& item : items) {
    if (item.kind == MenuItemKind::kSeparator) continue;
    const std::wstring name = prefix + geditor::menu_text_without_mnemonic(item.text);
    if (name == path) return &item;
    if (item.kind == MenuItemKind::kSubmenu) {
      if (const MenuItem* found = find_item(item.submenu, path, name + L" > ")) return found;
    }
  }
  return nullptr;
}

const MenuItem* menu_item(const std::wstring& path) {
  for (const geditor::Menu& m : geditor::menu_bar_menus()) {
    const std::wstring title = geditor::menu_text_without_mnemonic(m.title);
    if (const MenuItem* found = find_item(m.items, path, title + L" > ")) return found;
  }
  return nullptr;
}

// Every item of the three menus (and the right-click menu): enabled or not.
void each_item(std::span<const MenuItem> items, std::vector<const MenuItem*>* out) {
  for (const MenuItem& item : items) {
    if (item.kind == MenuItemKind::kSeparator) continue;
    out->push_back(&item);
    if (item.kind == MenuItemKind::kSubmenu) each_item(item.submenu, out);
  }
}

size_t enabled_items(const Session& s) {
  std::vector<const MenuItem*> items;
  for (const geditor::Menu& m : geditor::menu_bar_menus()) each_item(m.items, &items);
  each_item(geditor::context_menu_items(), &items);
  size_t n = 0;
  for (const MenuItem* item : items) n += geditor::menu_item_enabled(s.ed, *item) ? 1 : 0;
  return n;
}

size_t all_items() {
  std::vector<const MenuItem*> items;
  for (const geditor::Menu& m : geditor::menu_bar_menus()) each_item(m.items, &items);
  each_item(geditor::context_menu_items(), &items);
  return items.size();
}

}  // namespace

// ---- [C1] isearch keys (PLAN 4.4 "during isearch") ----

TEST(m5_isearch_c_s_finds_the_next_match_and_point_goes_to_its_end) {
  Session s;
  s.load(L"one foo two foo\n");
  s.keys(L"C-s");
  CHECK(s.ed.isearch_active());
  CHECK_EQ(echo(s), std::wstring(L"I-search: "));
  s.type(L"fo");
  CHECK_EQ(s.point(), size_t{6});
  s.type(L"o");
  CHECK_EQ(s.point(), size_t{7});
  CHECK_EQ(echo(s), std::wstring(L"I-search: foo"));
  CHECK_EQ(s.text(), std::wstring(L"one foo two foo\n"));  // typing goes to the pattern
}

TEST(m5_isearch_c_s_repeats_forward) {
  Session s;
  s.load(L"foo foo foo\n");
  s.keys(L"C-s");
  s.type(L"foo");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"C-s");
  CHECK_EQ(s.point(), size_t{7});
  s.keys(L"C-s");
  CHECK_EQ(s.point(), size_t{11});
}

TEST(m5_isearch_c_r_searches_backward_with_point_at_the_match_start) {
  Session s;
  s.load(L"foo foo foo\n");
  s.keys(L"M->");
  s.keys(L"C-r");
  CHECK_EQ(echo(s), std::wstring(L"I-search backward: "));
  s.type(L"foo");
  CHECK_EQ(s.point(), size_t{8});
  s.keys(L"C-r");
  CHECK_EQ(s.point(), size_t{4});
  s.keys(L"C-r");
  CHECK_EQ(s.point(), size_t{0});
}

TEST(m5_isearch_c_r_in_a_forward_search_turns_around_on_the_match) {
  Session s;
  s.load(L"ab foo cd foo\n");
  s.keys(L"C-s");
  s.type(L"foo");
  CHECK_EQ(s.point(), size_t{6});
  s.keys(L"C-r");  // to the start of the same match
  CHECK_EQ(s.point(), size_t{3});
  CHECK_EQ(echo(s), std::wstring(L"I-search backward: foo"));
  s.keys(L"C-s");  // back to its end
  CHECK_EQ(s.point(), size_t{6});
  s.keys(L"C-s");
  CHECK_EQ(s.point(), size_t{13});
}

// Retry R2: after turning around, a longer pattern is searched from the
// current match in the new direction (both directions).
TEST(m5_isearch_typing_after_turning_backward) {
  Session s;
  s.load(L"foo bar foo baz\n");
  s.keys(L"C-s");
  s.type(L"fo");
  s.keys(L"C-s");
  CHECK_EQ(s.point(), size_t{10});
  s.keys(L"C-r");
  CHECK_EQ(s.point(), size_t{8});
  s.type(L"o");
  CHECK_EQ(echo(s), std::wstring(L"I-search backward: foo"));
  CHECK_EQ(s.point(), size_t{8});
  CHECK(s.ed.search_highlights(10).current == (SearchMatch{8, 11}));
}

TEST(m5_isearch_typing_after_turning_forward) {
  Session s;
  s.load(L"foo bar foo baz\n");
  s.keys(L"M->");
  s.keys(L"C-r");
  s.type(L"fo");
  s.keys(L"C-r");
  CHECK_EQ(s.point(), size_t{0});
  s.keys(L"C-s");
  CHECK_EQ(s.point(), size_t{2});
  s.type(L"o");
  CHECK_EQ(echo(s), std::wstring(L"I-search: foo"));
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"C-s");
  CHECK_EQ(s.point(), size_t{11});
}

TEST(m5_isearch_del_goes_back_one_input) {
  Session s;
  s.load(L"fa fo fox\n");
  s.keys(L"C-s");
  s.type(L"fo");
  CHECK_EQ(s.point(), size_t{5});
  s.keys(L"C-s");  // the next "fo"
  CHECK_EQ(s.point(), size_t{8});
  s.keys(L"DEL");  // back to the first "fo" (the repeat is undone)
  CHECK_EQ(s.point(), size_t{5});
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring(L"fo"));
  s.keys(L"DEL");  // "f": the first match again
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring(L"f"));
  CHECK_EQ(s.point(), size_t{1});
  CHECK_EQ(echo(s), std::wstring(L"I-search: f"));
  s.keys(L"DEL");
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring());
  CHECK_EQ(s.point(), size_t{0});
  CHECK(s.ed.isearch_active());
  CHECK_EQ(s.text(), std::wstring(L"fa fo fox\n"));
}

TEST(m5_isearch_ret_ends_at_the_match_and_saves_the_mark) {
  Session s;
  s.load(L"abc foo\n");
  s.keys(L"C-s");
  s.type(L"foo");
  s.keys(L"RET");
  CHECK(!s.ed.isearch_active());
  CHECK_EQ(s.point(), size_t{7});
  CHECK(s.ed.buffer().has_mark);
  CHECK_EQ(s.ed.buffer().mark, size_t{0});
  CHECK(!s.ed.buffer().mark_active);
  CHECK_EQ(s.message(), std::wstring(L"Mark saved where search started"));
  CHECK_EQ(s.text(), std::wstring(L"abc foo\n"));
  s.keys(L"C-x C-x");  // back where the search started
  CHECK_EQ(s.point(), size_t{0});
}

TEST(m5_isearch_c_g_goes_back_to_the_start) {
  Session s;
  s.load(L"abc foo foo\n");
  s.set_point(1);
  s.keys(L"C-s");
  s.type(L"foo");
  s.keys(L"C-s");
  CHECK_EQ(s.point(), size_t{11});
  s.keys(L"C-g");
  CHECK(!s.ed.isearch_active());
  CHECK_EQ(s.point(), size_t{1});
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  CHECK(!s.ed.buffer().has_mark);
}

TEST(m5_isearch_c_g_while_failing_goes_back_in_one_step) {
  Session s;
  s.load(L"abc abd\n");
  s.set_point(1);
  s.keys(L"C-s");
  s.type(L"abx");
  CHECK(echo(s).find(L"Failing") == 0);
  s.keys(L"C-g");
  CHECK(!s.ed.isearch_active());
  CHECK_EQ(s.point(), size_t{1});
}

TEST(m5_isearch_ret_keeps_an_active_region) {
  Session s;
  s.load(L"abc foo\n");
  s.keys(L"C-SPC");
  s.keys(L"C-s");
  s.type(L"foo");
  s.keys(L"RET");
  CHECK(s.ed.buffer().mark_active);
  CHECK_EQ(s.ed.buffer().mark, size_t{0});
  CHECK_EQ(s.point(), size_t{7});
  CHECK(s.message() != L"Mark saved where search started");
}

TEST(m5_host_commands_end_the_modes_first) {
  Session s;
  s.load(L"a a\n");
  s.keys(L"C-s");
  s.type(L"a");
  s.ed.execute_command("toggle-line-numbers");  // as the window's close button runs quit
  CHECK(!s.ed.isearch_active());
  start_query_replace(s, L"M-%", L"a", L"b");
  s.keys(L"y");
  s.ed.end_search_modes();  // a dropped file, the session end
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Replaced 1 occurrence"));
}

TEST(m5_settings_keys_still_work_inside_the_modes) {
  Session s;
  s.settings(R"({"keys": {"C-c x": ["insert-text", "Z"], "C-S-q": ["insert-text", "Q"]}})");
  CHECK(s.ed.settings_errors().empty());
  s.load(L"abc\n");
  s.keys(L"M-x");
  s.keys(L"C-c x");  // the settings' global key, through the minibuffer's fallback
  s.keys(L"C-S-q");  // a C-S- key of the settings (not of the Windows compatible layer)
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"ZQ"));
  s.keys(L"C-g");
  CHECK_EQ(s.text(), std::wstring(L"abc\n"));
}

TEST(m5_isearch_m_r_toggles_regexp) {
  Session s;
  s.load(L"a.c abc\n");
  s.keys(L"M-<");
  s.keys(L"C-s");
  s.type(L"b.");
  CHECK(s.ed.isearch_active());
  // Literal "b." does not occur.
  CHECK_EQ(echo(s), std::wstring(L"Failing I-search: b."));
  s.keys(L"M-r");
  CHECK_EQ(echo(s), std::wstring(L"Regexp I-search: b."));
  CHECK_EQ(s.point(), size_t{7});  // "bc"
  s.keys(L"M-r");
  CHECK_EQ(echo(s), std::wstring(L"Failing I-search: b."));
}

TEST(m5_isearch_m_c_toggles_the_case) {
  Session s;
  s.load(L"Foo foo\n");
  s.keys(L"C-s");
  s.type(L"foo");  // smart case: lower case ignores case
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"M-c");  // now case-sensitive: "Foo" no longer matches
  CHECK_EQ(echo(s), std::wstring(L"Case-sensitive I-search: foo"));
  CHECK_EQ(s.point(), size_t{7});
  s.keys(L"M-c");
  CHECK_EQ(echo(s), std::wstring(L"Case-insensitive I-search: foo"));
  s.keys(L"DEL");
  s.keys(L"DEL");  // both toggles undone
  CHECK_EQ(echo(s), std::wstring(L"I-search: foo"));
  CHECK_EQ(s.point(), size_t{3});
}

TEST(m5_isearch_m_percent_starts_query_replace_with_the_pattern) {
  Session s;
  s.load(L"a foo b foo\n");
  s.keys(L"C-s");
  s.type(L"foo");
  s.keys(L"M-%");
  CHECK(!s.ed.isearch_active());
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Query replace foo with: "));
  CHECK_EQ(s.point(), size_t{2});  // the start of the match
  s.type(L"X");
  s.keys(L"RET");
  CHECK(s.ed.query_replace_active());
  CHECK_EQ(echo(s), std::wstring(L"Query replacing foo with X: "));
  s.keys(L"!");
  CHECK_EQ(s.text(), std::wstring(L"a X b X\n"));
}

TEST(m5_isearch_m_percent_in_a_regexp_search_starts_query_replace_regexp) {
  Session s;
  s.load(L"a1 b22\n");
  s.keys(L"C-u C-s");
  s.type(L"[0-9]+");
  s.keys(L"M-%");
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Query replace regexp [0-9]+ with: "));
  s.type(L"<$0>");
  s.keys(L"RET");
  s.keys(L"!");
  CHECK_EQ(s.text(), std::wstring(L"a<1> b<22>\n"));
}

TEST(m5_isearch_c_u_c_s_and_c_u_c_r_search_regexps) {
  Session s;
  s.load(L"x12 y345\n");
  s.keys(L"C-u C-s");
  CHECK_EQ(echo(s), std::wstring(L"Regexp I-search: "));
  s.type(L"[0-9]+");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"C-s");
  CHECK_EQ(s.point(), size_t{8});
  s.keys(L"RET");
  s.keys(L"C-u C-r");
  CHECK_EQ(echo(s), std::wstring(L"Regexp I-search backward: "));
  s.type(L"[a-z][0-9]");
  CHECK_EQ(s.point(), size_t{4});
}

TEST(m5_isearch_failing_search_shows_failing_and_point_stays) {
  Session s;
  s.load(L"abc abd\n");
  s.keys(L"C-s");
  s.type(L"ab");
  CHECK_EQ(s.point(), size_t{2});
  s.type(L"x");
  CHECK_EQ(echo(s), std::wstring(L"Failing I-search: abx"));
  CHECK_EQ(s.point(), size_t{2});
  s.keys(L"DEL");
  CHECK_EQ(echo(s), std::wstring(L"I-search: ab"));
}

TEST(m5_isearch_c_s_after_a_failure_wraps_around) {
  Session s;
  s.load(L"foo bar foo\n");
  s.set_point(5);
  s.keys(L"C-s");
  s.type(L"foo");
  CHECK_EQ(s.point(), size_t{11});
  s.keys(L"C-s");
  CHECK_EQ(echo(s), std::wstring(L"Failing I-search: foo"));
  CHECK_EQ(s.point(), size_t{11});
  s.keys(L"C-s");
  CHECK_EQ(echo(s), std::wstring(L"Wrapped I-search: foo"));
  CHECK_EQ(s.point(), size_t{3});
  // Backward: the same from the start of the buffer.
  s.keys(L"RET");
  s.keys(L"M-<");
  s.keys(L"C-r");
  s.type(L"bar");
  CHECK_EQ(echo(s), std::wstring(L"Failing I-search backward: bar"));
  s.keys(L"C-r");
  CHECK_EQ(echo(s), std::wstring(L"Wrapped I-search backward: bar"));
  CHECK_EQ(s.point(), size_t{4});
}

TEST(m5_isearch_a_key_not_in_its_keymap_ends_it_and_runs) {
  Session s;
  s.load(L"abc foo xyz\n");
  s.keys(L"C-s");
  s.type(L"foo");
  s.keys(L"C-f");  // ends isearch at the match, then forward-char
  CHECK(!s.ed.isearch_active());
  CHECK_EQ(s.point(), size_t{8});
  CHECK(s.ed.buffer().has_mark);  // as RET: the mark where it started
  CHECK_EQ(s.ed.buffer().mark, size_t{0});
  // M-x after isearch: the minibuffer opens.
  s.keys(L"C-s");
  s.type(L"xyz");
  s.keys(L"M-x");
  CHECK(!s.ed.isearch_active());
  CHECK(s.ed.minibuffer_active());
  s.keys(L"C-g");
  // A prefix key ends isearch and continues as a prefix: C-x h.
  s.keys(L"C-s");
  s.type(L"abc");
  s.keys(L"C-x h");
  CHECK(!s.ed.isearch_active());
  CHECK(s.ed.buffer().mark_active);
  CHECK_EQ(s.point(), size_t{0});
}

TEST(m5_isearch_empty_c_s_uses_the_last_search_string) {
  Session s;
  s.load(L"foo x foo\n");
  s.keys(L"C-s");
  s.type(L"foo");
  s.keys(L"RET");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"C-s C-s");
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring(L"foo"));
  CHECK_EQ(s.point(), size_t{9});
  s.keys(L"RET");
  // No previous regexp search string.
  s.keys(L"C-u C-s C-s");
  CHECK(echo(s).find(L"[No previous search string]") != std::wstring::npos);
}

TEST(m5_isearch_ime_text_extends_the_pattern) {
  Session s;
  s.load(L"\x65E5\x672C\x8A9E \x672C\x8A9E\n");
  s.keys(L"C-s");
  s.ed.insert_text(L"\x672C\x8A9E");
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring(L"\x672C\x8A9E"));
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"DEL");  // the IME text was one input
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring());
  CHECK_EQ(s.text(), std::wstring(L"\x65E5\x672C\x8A9E \x672C\x8A9E\n"));
}

TEST(m5_isearch_a_click_ends_isearch) {
  Session s;
  s.load(L"abc foo\n");
  s.keys(L"C-s");
  s.type(L"foo");
  CHECK(s.ed.mouse_press(1, false));
  CHECK(!s.ed.isearch_active());
  CHECK_EQ(s.point(), size_t{1});
}

TEST(m5_isearch_point_lands_on_cluster_boundaries) {
  Session s;
  // "か" + U+3099 is one cluster: a match of "か" ends inside it.
  s.load(L"x\x304B\x3099y\n");
  s.keys(L"C-s");
  s.type(L"\x304B");
  CHECK_EQ(s.point(), size_t{3});  // after the whole cluster
  s.keys(L"RET");
  s.keys(L"C-r");
  s.type(L"\x3099");
  CHECK_EQ(s.point(), size_t{1});  // before the whole cluster
}

// ---- [C2] query-replace keys (PLAN 4.4 "during query-replace") ----

TEST(m5_query_replace_prompts) {
  Session s;
  s.load(L"a b a\n");
  s.keys(L"M-%");
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Query replace: "));
  s.type(L"a");
  s.keys(L"RET");
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Query replace a with: "));
  s.type(L"z");
  s.keys(L"RET");
  CHECK(s.ed.query_replace_active());
  CHECK_EQ(echo(s), std::wstring(L"Query replacing a with z: "));
  CHECK_EQ(s.point(), size_t{1});  // at the end of the first match
  s.keys(L"q");
  s.keys(L"C-M-%");
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Query replace regexp: "));
  s.type(L"[ab]");
  s.keys(L"RET");
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Query replace regexp [ab] with: "));
}

TEST(m5_query_replace_y_replaces_and_goes_to_the_next_match) {
  Session s;
  s.load(L"a b a b\n");
  start_query_replace(s, L"M-%", L"b", L"XY");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"y");
  CHECK_EQ(s.text(), std::wstring(L"a XY a b\n"));
  CHECK(s.ed.query_replace_active());
  CHECK_EQ(s.point(), size_t{8});  // end of the next "b"
  s.keys(L"y");
  CHECK_EQ(s.text(), std::wstring(L"a XY a XY\n"));
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Replaced 2 occurrences"));
}

TEST(m5_query_replace_spc_replaces) {
  Session s;
  s.load(L"cat cat\n");
  start_query_replace(s, L"M-%", L"cat", L"dog");
  s.keys(L"SPC");
  CHECK_EQ(s.text(), std::wstring(L"dog cat\n"));
  CHECK(s.ed.query_replace_active());
}

TEST(m5_query_replace_n_skips) {
  Session s;
  s.load(L"cat cat\n");
  start_query_replace(s, L"M-%", L"cat", L"dog");
  s.keys(L"n");
  CHECK_EQ(s.text(), std::wstring(L"cat cat\n"));
  CHECK_EQ(s.point(), size_t{7});
  s.keys(L"y");
  CHECK_EQ(s.text(), std::wstring(L"cat dog\n"));
  CHECK_EQ(s.message(), std::wstring(L"Replaced 1 occurrence"));
}

TEST(m5_query_replace_del_skips) {
  Session s;
  s.load(L"cat cat\n");
  start_query_replace(s, L"M-%", L"cat", L"dog");
  s.keys(L"DEL");
  CHECK_EQ(s.text(), std::wstring(L"cat cat\n"));
  CHECK_EQ(s.point(), size_t{7});
  CHECK(s.ed.query_replace_active());
}

TEST(m5_query_replace_bang_replaces_the_rest) {
  Session s;
  s.load(L"a1 a2 a3\na4\n");
  start_query_replace(s, L"M-%", L"a", L"b");
  s.keys(L"n");
  s.keys(L"!");
  CHECK_EQ(s.text(), std::wstring(L"a1 b2 b3\nb4\n"));
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Replaced 3 occurrences"));
}

TEST(m5_query_replace_dot_replaces_and_exits) {
  Session s;
  s.load(L"a a a\n");
  start_query_replace(s, L"M-%", L"a", L"b");
  s.keys(L".");
  CHECK_EQ(s.text(), std::wstring(L"b a a\n"));
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Replaced 1 occurrence"));
}

TEST(m5_query_replace_q_exits) {
  Session s;
  s.load(L"a a a\n");
  start_query_replace(s, L"M-%", L"a", L"b");
  s.keys(L"y");
  s.keys(L"q");
  CHECK_EQ(s.text(), std::wstring(L"b a a\n"));
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Replaced 1 occurrence"));
}

TEST(m5_query_replace_ret_exits) {
  Session s;
  s.load(L"a a a\n");
  start_query_replace(s, L"M-%", L"a", L"b");
  s.keys(L"RET");
  CHECK_EQ(s.text(), std::wstring(L"a a a\n"));
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Replaced 0 occurrences"));
}

TEST(m5_query_replace_is_one_undo_unit_with_text_and_point) {
  Session s;
  s.load(L"x a a\na a\n");
  s.set_point(1);
  start_query_replace(s, L"M-%", L"a", L"bb");
  s.keys(L"y");
  s.keys(L"n");
  s.keys(L"y");
  s.keys(L"!");
  CHECK_EQ(s.text(), std::wstring(L"x bb a\nbb bb\n"));
  CHECK(s.modified());
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"x a a\na a\n"));
  CHECK_EQ(s.point(), size_t{1});  // where query-replace started
  CHECK(!s.modified());
  s.keys(L"C-?");  // and redo brings all of it back
  CHECK_EQ(s.text(), std::wstring(L"x bb a\nbb bb\n"));
  s.keys(L"C-/");
  s.keys(L"C-/");  // nothing before it
  CHECK_EQ(s.message(), std::wstring(L"No further undo information"));
}

TEST(m5_query_replace_regexp_with_dollar_groups) {
  Session s;
  s.load(L"key=value k2=v2\n");
  start_query_replace(s, L"C-M-%", L"(\\w+)=(\\w+)", L"$2:${1}$$");
  CHECK_EQ(echo(s), std::wstring(L"Query replacing regexp (\\w+)=(\\w+) with $2:${1}$$: "));
  s.keys(L"!");
  CHECK_EQ(s.text(), std::wstring(L"value:key$ v2:k2$\n"));
  CHECK_EQ(s.message(), std::wstring(L"Replaced 2 occurrences"));
}

TEST(m5_query_replace_regexp_from_a_key_binding) {
  Session s;
  s.settings(R"j({"keys": {"<f12>": ["query-replace-regexp", "a(b)", "$1"]}})j");
  CHECK(s.ed.settings_errors().empty());
  s.load(L"ab CD\n");
  s.keys(L"<f12>");
  CHECK(s.ed.query_replace_active());
  s.keys(L"!");
  CHECK_EQ(s.text(), std::wstring(L"b CD\n"));
}

TEST(m5_query_replace_without_a_match_replaces_nothing) {
  Session s;
  s.load(L"abc\n");
  start_query_replace(s, L"M-%", L"zz", L"y");
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Replaced 0 occurrences"));
  CHECK(!s.modified());
}

TEST(m5_query_replace_c_g_quits_and_keeps_the_replacements) {
  Session s;
  s.load(L"a a a\n");
  start_query_replace(s, L"M-%", L"a", L"b");
  s.keys(L"y");
  s.keys(L"C-g");
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  CHECK_EQ(s.text(), std::wstring(L"b a a\n"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"a a a\n"));
}

TEST(m5_query_replace_empty_matches_move_on) {
  Session s;
  s.load(L"ab\n");
  start_query_replace(s, L"C-M-%", L"x*", L"-");
  s.keys(L"!");
  CHECK_EQ(s.text(), std::wstring(L"-a-b-\n-"));
  CHECK_EQ(s.message(), std::wstring(L"Replaced 4 occurrences"));
}

TEST(m5_query_replace_an_unbound_character_is_undefined) {
  Session s;
  s.load(L"a a\n");
  start_query_replace(s, L"M-%", L"a", L"b");
  s.type(L"z");
  CHECK(s.ed.query_replace_active());
  CHECK_EQ(s.text(), std::wstring(L"a a\n"));
  CHECK_EQ(echo(s), std::wstring(L"Query replacing a with b: [z is undefined]"));
  s.ed.insert_text(L"\x3042");  // IME text is not typed either
  CHECK_EQ(s.text(), std::wstring(L"a a\n"));
}

TEST(m5_query_replace_follows_other_commands) {
  Session s;
  s.load(L"a a a\nb a\n");
  start_query_replace(s, L"M-%", L"a", L"z");
  CHECK_EQ(s.point(), size_t{1});
  s.keys(L"C-e");  // a global command through the fallback: moves point
  CHECK(s.ed.query_replace_active());
  CHECK_EQ(s.point(), size_t{9});  // the next match from point
  s.keys(L"C-a");
  CHECK_EQ(s.point(), size_t{9});
  s.keys(L"y");
  CHECK_EQ(s.text(), std::wstring(L"a a a\nb z\n"));
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Replaced 1 occurrence"));
}

// Retry R1: a minibuffer opened during query-replace takes typed and IME
// characters, and C-g / ESC ESC ESC there close only the minibuffer.
TEST(m5_query_replace_mx_minibuffer_takes_typed_and_ime_text) {
  Session s;
  s.load(L"a a\n");
  start_query_replace(s, L"M-%", L"a", L"b");
  s.keys(L"M-x");
  CHECK(s.ed.minibuffer_active());
  s.type(L"end-of");
  s.ed.insert_text(L"-line");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"end-of-line"));
  CHECK_EQ(s.text(), std::wstring(L"a a\n"));
  s.keys(L"RET");  // end-of-line runs; query-replace looks again from point
  CHECK(!s.ed.minibuffer_active());
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Replaced 0 occurrences"));
}

TEST(m5_query_replace_goto_line_digits_in_its_minibuffer) {
  Session s;
  s.load(L"x\na a\na\n");
  start_query_replace(s, L"M-%", L"a", L"b");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"M-g g");
  s.type(L"3");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"3"));
  s.keys(L"RET");
  CHECK(s.ed.query_replace_active());  // the next match from line 3
  CHECK_EQ(s.point(), size_t{7});
  s.keys(L"y");
  CHECK_EQ(s.text(), std::wstring(L"x\na a\nb\n"));
}

TEST(m5_query_replace_c_g_in_its_minibuffer_keeps_query_replace) {
  for (const wchar_t* quit : {L"C-g", L"ESC ESC ESC"}) {
    Session s;
    s.load(L"a a\n");
    start_query_replace(s, L"M-%", L"a", L"b");
    s.keys(L"M-x");
    s.type(L"fo");
    s.keys(quit);
    CHECK(!s.ed.minibuffer_active());
    CHECK(s.ed.query_replace_active());
    s.keys(L"!");
    CHECK_EQ(s.text(), std::wstring(L"b b\n"));
    CHECK_EQ(s.message(), std::wstring(L"Replaced 2 occurrences"));
  }
}

// Retry R3: "!" replaces every match of a long line in one edit; the count,
// point and the one undo unit are as before.
TEST(m5_query_replace_bang_on_a_long_line) {
  std::wstring line;
  for (int i = 0; i < 2000; ++i) line += L"abcdefg ";
  Session s;
  s.load(line + L"\nab\n");
  start_query_replace(s, L"C-M-%", L"a(b)", L"<$1>");
  s.keys(L"n");  // the first one stays
  s.keys(L"!");
  CHECK_EQ(s.message(), std::wstring(L"Replaced 2000 occurrences"));
  std::wstring want = L"abcdefg ";
  for (int i = 1; i < 2000; ++i) want += L"<b>cdefg ";
  want += L"\n<b>\n";
  CHECK(s.text() == want);
  CHECK_EQ(s.point(), want.size() - 1);  // after the last replacement
  s.keys(L"C-/");
  CHECK(s.text() == line + L"\nab\n");
  // An invalid replacement after "!" keeps the ones before it.
  Session t;
  t.load(L"ab a\n");
  start_query_replace(t, L"C-M-%", L"a(b)?", L"$1");  // the second match leaves group 1 unset
  t.keys(L"!");
  CHECK_EQ(t.text(), std::wstring(L"b a\n"));
  CHECK(!t.ed.query_replace_active());
  CHECK(t.message().starts_with(L"Invalid replacement: "));
  CHECK(t.message().ends_with(L"; Replaced 1 occurrence"));
}

TEST(m5_query_replace_invalid_replacement_stops) {
  Session s;
  s.load(L"ab\n");
  start_query_replace(s, L"C-M-%", L"a", L"$9");
  s.keys(L"y");
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Invalid replacement: unknown substring; Replaced 0 occurrences"));
  CHECK_EQ(s.text(), std::wstring(L"ab\n"));
}

TEST(m5_query_replace_empty_from_and_read_only_are_refused) {
  Session s;
  s.load(L"ab\n");
  start_query_replace(s, L"M-%", L"", L"x");
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"The string to replace is empty"));
  s.keys(L"C-x C-q");
  start_query_replace(s, L"M-%", L"a", L"x");
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Buffer is read-only"));
}

// ---- [C3] regexps ----

TEST(m5_regexp_dot_matches_a_surrogate_pair) {
  Session s;
  s.load(L"a\U0001F600c\n");
  s.keys(L"C-u C-s");
  s.type(L"a.c");
  CHECK_EQ(s.point(), size_t{4});  // the pair is one character
  s.keys(L"RET");
  start_query_replace(s, L"M-< C-M-%", L"a(.)c", L"[$1]");
  s.keys(L"!");
  CHECK_EQ(s.text(), std::wstring(L"[\U0001F600]\n"));
}

TEST(m5_regexp_full_width_characters) {
  Session s;
  s.load(L"\xFF21\xFF22\xFF23 \x6F22\x5B57\x30AB\x30CA\n");
  s.keys(L"C-u C-s");
  s.type(L"[\xFF21-\xFF3A]+");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"RET");
  // \w (Unicode properties) covers kanji and katakana.
  s.keys(L"C-u C-s");
  s.type(L"\\w+");
  CHECK_EQ(s.point(), size_t{8});
  s.keys(L"RET");
  // Full-width letters fold case: "ａｂｃ" finds "ＡＢＣ".
  s.keys(L"M-< C-s");
  s.type(L"\xFF41\xFF42\xFF43");
  CHECK_EQ(s.point(), size_t{3});
}

TEST(m5_regexp_invalid_pattern_shows_the_message_and_keeps_the_buffer) {
  Session s;
  s.load(L"a(b\n");
  s.keys(L"C-u C-s");
  s.type(L"a(");
  CHECK(s.ed.isearch_active());
  CHECK_EQ(echo(s), std::wstring(L"Regexp I-search: a( [missing closing parenthesis]"));
  CHECK_EQ(s.point(), size_t{1});  // the last match ("a")
  CHECK(s.ed.search_highlights(10).matches.empty());
  s.keys(L"C-g");
  start_query_replace(s, L"C-M-%", L"[", L"x");
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Invalid regexp: missing terminating ] for character class"));
  CHECK_EQ(s.text(), std::wstring(L"a(b\n"));
  CHECK(!s.modified());
}

TEST(m5_regexp_never_matches_across_a_line_end) {
  for (const wchar_t* pattern : {L"a.b", L"a\\sb", L"a[^x]b", L"a\\nb", L"a$\\n^b", L"a\\Rb", L"a[\\s\\S]b"}) {
    Session s;
    s.load(L"xa\nb\n");
    s.keys(L"C-u C-s");
    s.type(pattern);
    if (s.ed.isearch_pattern() != pattern) std::printf("  pattern %s\n", test::to_display(std::wstring(pattern)).c_str());
    CHECK(echo(s).find(L"Failing") == 0);
    CHECK_EQ(s.point(), size_t{2});  // still after the "a" that matched
    s.keys(L"C-g");
  }
  // query-replace too.
  Session s;
  s.load(L"a\nb\n");
  start_query_replace(s, L"C-M-%", L"a\\s*b", L"x");
  CHECK_EQ(s.message(), std::wstring(L"Replaced 0 occurrences"));
  CHECK_EQ(s.text(), std::wstring(L"a\nb\n"));
}

// ---- [C4] search.case_fold (smart / always / never) and M-c ----

namespace {

// Searches "foo" / "Foo" from the start of "Foo foo" and returns point.
size_t search_with(const char* fold, const wchar_t* pattern) {
  Session s;
  s.settings(std::string(R"({"search": {"case_fold": ")") + fold + "\"}}");
  CHECK(s.ed.settings_errors().empty());
  s.load(L"xFoo foo FOO\n");
  s.keys(L"C-s");
  s.type(pattern);
  const size_t point = s.point();
  s.keys(L"C-g");
  return point;
}

}  // namespace

TEST(m5_case_fold_smart_lower_case_ignores_case) {
  CHECK_EQ(search_with("smart", L"foo"), size_t{4});
}

TEST(m5_case_fold_smart_upper_case_is_case_sensitive) {
  CHECK_EQ(search_with("smart", L"FOO"), size_t{12});
}

TEST(m5_case_fold_always_lower_case_ignores_case) {
  CHECK_EQ(search_with("always", L"foo"), size_t{4});
}

TEST(m5_case_fold_always_upper_case_ignores_case) {
  CHECK_EQ(search_with("always", L"FOO"), size_t{4});
}

TEST(m5_case_fold_never_lower_case_is_case_sensitive) {
  CHECK_EQ(search_with("never", L"foo"), size_t{8});
}

TEST(m5_case_fold_never_upper_case_is_case_sensitive) {
  CHECK_EQ(search_with("never", L"FOO"), size_t{12});
}

// M-c turns the case rule of the setting around; the matches highlighted
// (all of "Foo foo FOO" or only the exact one) show which rule applies.
TEST(m5_case_m_c_toggles_against_each_setting) {
  struct Row {
    const char* fold;
    const wchar_t* pattern;
    size_t before;  // highlighted matches
    size_t after;   // after M-c
    const wchar_t* prompt;
  };
  for (const Row& r : {Row{"smart", L"foo", 3, 1, L"Case-sensitive I-search: foo"},
                       Row{"smart", L"Foo", 1, 3, L"Case-insensitive I-search: Foo"},
                       Row{"always", L"FOO", 3, 1, L"Case-sensitive I-search: FOO"},
                       Row{"never", L"foo", 1, 3, L"Case-insensitive I-search: foo"}}) {
    Session s;
    s.settings(std::string(R"({"search": {"case_fold": ")") + r.fold + "\"}}");
    s.load(L"xFoo foo FOO\n");
    s.keys(L"C-s");
    s.type(r.pattern);
    CHECK_EQ(s.ed.search_highlights(10).matches.size(), r.before);
    s.keys(L"M-c");
    if (s.ed.search_highlights(10).matches.size() != r.after) {
      std::printf("  %s %s\n", r.fold, test::to_display(std::wstring(r.pattern)).c_str());
    }
    CHECK_EQ(s.ed.search_highlights(10).matches.size(), r.after);
    CHECK_EQ(echo(s), std::wstring(r.prompt));
    s.keys(L"M-c");  // and back
    CHECK_EQ(s.ed.search_highlights(10).matches.size(), r.before);
  }
}

TEST(m5_case_smart_case_of_a_regexp_skips_escapes) {
  CHECK(!geditor::pattern_has_upper_case(L"\\W\\S\\p{Lu}\\x{1F600}a", true));
  CHECK(geditor::pattern_has_upper_case(L"\\W\\S\\p{Lu}\\x{1F600}A", true));
  CHECK(geditor::pattern_has_upper_case(L"\\W", false));  // a literal backslash and W
  CHECK(geditor::pattern_has_upper_case(L"\xFF21", false));  // full-width A
  CHECK(!geditor::pattern_has_upper_case(L"\x6F22\xFF41", false));
}

TEST(m5_case_fold_applies_to_query_replace) {
  Session s;
  s.load(L"Ab ab AB\n");
  start_query_replace(s, L"M-%", L"ab", L"x");
  s.keys(L"!");
  CHECK_EQ(s.text(), std::wstring(L"x x x\n"));
  Session t;
  t.load(L"Ab ab AB\n");
  start_query_replace(t, L"M-%", L"AB", L"x");
  t.keys(L"!");
  CHECK_EQ(t.text(), std::wstring(L"Ab ab x\n"));
}

// ---- [C6] Windows compatible keys ----

TEST(m5_windows_key_c_s_f_starts_isearch) {
  Session s;
  s.load(L"ab foo\n");
  s.keys(L"C-S-f");
  CHECK(s.ed.isearch_active());
  s.type(L"foo");
  CHECK_EQ(s.point(), size_t{6});
}

TEST(m5_windows_keys_c_s_h_and_c_s_r_start_query_replace) {
  for (const wchar_t* key : {L"C-S-h", L"C-S-r"}) {
    Session s;
    s.load(L"a a\n");
    start_query_replace(s, key, L"a", L"b");
    CHECK(s.ed.query_replace_active());
    s.keys(L"!");
    CHECK_EQ(s.text(), std::wstring(L"b b\n"));
  }
}

TEST(m5_windows_key_c_s_g_runs_goto_line) {
  Session s;
  s.load(L"a\nb\nc\n");
  s.keys(L"C-S-g");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Goto line: "));
  s.type(L"3");
  s.keys(L"RET");
  CHECK_EQ(s.point(), size_t{4});
}

// ---- [C8] keys of other layers inside the modes ----

TEST(m5_modes_c_s_g_in_mx_aborts) {
  Session s;
  s.load(L"abc\n");
  s.keys(L"M-x");
  s.type(L"for");
  s.keys(L"C-S-g");  // shift-translated to C-g (keyboard-quit), not goto-line
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
}

TEST(m5_modes_c_s_w_in_mx_does_not_quit) {
  Session s;
  s.load(L"abc\n");
  s.keys(L"M-x");
  s.type(L"for");
  s.keys(L"C-S-w");  // C-w (kill-region in the minibuffer), not quit
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.host.quit_calls, 0);
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"for"));
  // C-S-a there is C-a (the start of the input), not select-all.
  s.keys(L"C-S-a");
  CHECK_EQ(s.ed.minibuffer_buffer().point, size_t{0});
  CHECK(!s.ed.buffer().mark_active);
}

TEST(m5_modes_c_s_s_in_isearch_goes_to_the_next_match) {
  Session s;
  s.load(L"foo foo\n");
  s.keys(L"C-s");
  s.type(L"foo");
  s.keys(L"C-S-s");  // C-s, not save-buffer
  CHECK(s.ed.isearch_active());
  CHECK_EQ(s.point(), size_t{7});
  CHECK(s.host.saves.empty());
  s.keys(L"C-S-g");  // C-g: back to the start
  CHECK(!s.ed.isearch_active());
  CHECK_EQ(s.point(), size_t{0});
}

TEST(m5_modes_c_s_z_in_isearch_does_not_undo) {
  Session s;
  s.load(L"abc\n");
  s.type(L"x");
  CHECK_EQ(s.text(), std::wstring(L"xabc\n"));
  s.keys(L"C-s");
  s.type(L"b");
  s.keys(L"C-S-z");  // ends isearch; C-z is undefined (not undo)
  CHECK(!s.ed.isearch_active());
  CHECK_EQ(s.text(), std::wstring(L"xabc\n"));
  CHECK_EQ(s.message(), std::wstring(L"C-S-z is undefined"));
}

TEST(m5_modes_windows_keys_in_query_replace) {
  Session s;
  s.load(L"a a\n");
  start_query_replace(s, L"M-%", L"a", L"b");
  s.keys(L"C-S-v");  // C-v (scroll), not paste
  CHECK(s.ed.query_replace_active());
  CHECK_EQ(s.text(), std::wstring(L"a a\n"));
  s.keys(L"C-S-g");  // C-g quits query-replace
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
}

TEST(m5_modes_windows_keys_still_work_outside_the_modes) {
  Session s;
  s.load(L"abc\n");
  s.keys(L"C-S-a");
  CHECK(s.ed.buffer().mark_active);
  CHECK_EQ(s.point(), size_t{4});
}

// ---- [C7] menus ----

TEST(m5_menu_every_item_is_disabled_during_isearch_and_back_after) {
  Session s;
  s.load(L"abc\n");
  const size_t before = enabled_items(s);
  CHECK(before > 0);
  s.keys(L"C-s");
  s.type(L"b");
  CHECK_EQ(enabled_items(s), size_t{0});
  CHECK(all_items() > 0);
  s.keys(L"RET");
  CHECK_EQ(enabled_items(s), before);
}

TEST(m5_menu_every_item_is_disabled_during_query_replace_and_back_after) {
  Session s;
  s.load(L"abc\n");
  const size_t before = enabled_items(s);
  start_query_replace(s, L"M-%", L"b", L"x");
  CHECK(s.ed.query_replace_active());
  CHECK_EQ(enabled_items(s), size_t{0});
  const MenuItem* undo = menu_item(L"Edit > Undo");
  CHECK(undo != nullptr && !s.ed.execute_menu_item(*undo));
  s.keys(L"q");
  CHECK_EQ(enabled_items(s), before);
}

TEST(m5_menu_find_replace_and_go_to_line_start_their_commands) {
  Session s;
  s.load(L"a\nb foo\n");
  const MenuItem* find = menu_item(L"Edit > Find...");
  const MenuItem* replace = menu_item(L"Edit > Replace...");
  const MenuItem* go = menu_item(L"Edit > Go to Line...");
  CHECK(find != nullptr && replace != nullptr && go != nullptr);
  if (find == nullptr || replace == nullptr || go == nullptr) return;
  CHECK(geditor::menu_item_enabled(s.ed, *find));
  CHECK(geditor::menu_item_enabled(s.ed, *replace));
  CHECK(geditor::menu_item_enabled(s.ed, *go));
  CHECK(s.ed.execute_menu_item(*find));
  CHECK(s.ed.isearch_active());
  CHECK_EQ(echo(s), std::wstring(L"I-search: "));
  s.keys(L"C-g");
  CHECK(s.ed.execute_menu_item(*replace));
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Query replace: "));
  s.keys(L"C-g");
  CHECK(s.ed.execute_menu_item(*go));
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Goto line: "));
  s.type(L"2");
  s.keys(L"RET");
  CHECK_EQ(s.point(), size_t{2});
}

// ---- [C9] highlights given to the renderer ----

TEST(m5_highlights_list_the_visible_matches_and_the_current_one) {
  Session s;
  s.load(L"ab ab\nxx ab\n");
  s.keys(L"C-s");
  s.type(L"ab");
  const geditor::Editor::SearchHighlights h = s.ed.search_highlights(10);
  CHECK_EQ(h.matches.size(), size_t{3});
  if (h.matches.size() == 3) {
    CHECK(h.matches[0] == (SearchMatch{0, 2}));
    CHECK(h.matches[1] == (SearchMatch{3, 5}));
    CHECK(h.matches[2] == (SearchMatch{9, 11}));
  }
  CHECK(h.has_current);
  CHECK(h.current == (SearchMatch{0, 2}));
  s.keys(L"C-s");
  CHECK(s.ed.search_highlights(10).current == (SearchMatch{3, 5}));
  // Only the rows asked for: the first row.
  CHECK_EQ(s.ed.search_highlights(1).matches.size(), size_t{2});
}

TEST(m5_highlights_are_gone_after_the_search_ends) {
  Session s;
  s.load(L"ab ab\n");
  s.keys(L"C-s");
  s.type(L"ab");
  CHECK(!s.ed.search_highlights(10).matches.empty());
  s.keys(L"RET");
  CHECK(s.ed.search_highlights(10).matches.empty());
  CHECK(!s.ed.search_highlights(10).has_current);
  start_query_replace(s, L"M-< M-%", L"ab", L"x");
  CHECK_EQ(s.ed.search_highlights(10).matches.size(), size_t{2});
  CHECK(s.ed.search_highlights(10).has_current);
  s.keys(L"q");
  CHECK(s.ed.search_highlights(10).matches.empty());
  // A failing isearch has no current match.
  s.keys(L"C-s");
  s.type(L"zz");
  CHECK(!s.ed.search_highlights(10).has_current);
  s.keys(L"C-g");
  CHECK(s.ed.search_highlights(10).matches.empty());
}

TEST(m5_highlights_are_widened_to_clusters) {
  Session s;
  s.load(L"\x304B\x3099 \x304B\n");
  s.keys(L"C-s");
  s.type(L"\x304B");
  const geditor::Editor::SearchHighlights h = s.ed.search_highlights(10);
  CHECK_EQ(h.matches.size(), size_t{2});
  if (h.matches.size() == 2) {
    CHECK(h.matches[0] == (SearchMatch{0, 2}));
    CHECK(h.matches[1] == (SearchMatch{3, 4}));
  }
}

// ---- [C10] goto-line ----

TEST(m5_goto_line_reads_the_line_in_the_minibuffer) {
  Session s;
  s.load(L"l1\nl2\nl3\n");
  s.set_point(1);
  s.keys(L"M-g g");
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Goto line: "));
  s.type(L"2");
  s.keys(L"RET");
  CHECK_EQ(s.point(), size_t{3});
  // The mark is pushed at the old position (as Emacs).
  CHECK(s.ed.buffer().has_mark);
  CHECK_EQ(s.ed.buffer().mark, size_t{1});
  CHECK(!s.ed.buffer().mark_active);
  CHECK_EQ(s.message(), std::wstring(L"Mark set"));
  s.keys(L"M-g M-g");
  s.type(L"3");
  s.keys(L"RET");
  CHECK_EQ(s.point(), size_t{6});
}

TEST(m5_goto_line_takes_the_prefix_argument) {
  Session s;
  s.load(L"l1\nl2\nl3\nl4\nl5\n");
  s.keys(L"M-3 M-g g");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.point(), size_t{6});
  s.keys(L"C-u M-g M-g");  // C-u alone is 4
  CHECK_EQ(s.point(), size_t{9});
  s.keys(L"C-u 2 M-x");
  s.type(L"goto-line");
  s.keys(L"RET");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.point(), size_t{3});
}

TEST(m5_goto_line_out_of_range_clamps) {
  Session s;
  s.load(L"l1\nl2\nl3");
  s.keys(L"M-g g");
  s.type(L"99");
  s.keys(L"RET");
  CHECK_EQ(s.point(), size_t{6});  // the start of the last line
  s.keys(L"M-g g");
  s.type(L"0");
  s.keys(L"RET");
  CHECK_EQ(s.point(), size_t{0});
  s.keys(L"M-- M-5 M-g g");
  CHECK_EQ(s.point(), size_t{0});
}

TEST(m5_goto_line_rejects_a_non_number) {
  Session s;
  s.load(L"l1\nl2\n");
  s.keys(L"M-g g");
  s.type(L"x2");
  s.keys(L"RET");
  CHECK(s.ed.minibuffer_active());  // the minibuffer stays for another try
  CHECK_EQ(echo(s), std::wstring(L"Goto line: x2 [Please enter a whole number]"));
  CHECK_EQ(s.point(), size_t{0});
}

TEST(m5_goto_line_keeps_an_active_region) {
  Session s;
  s.load(L"l1\nl2\nl3\n");
  s.keys(L"C-SPC");
  s.keys(L"M-3 M-g g");
  CHECK(s.ed.buffer().mark_active);
  CHECK_EQ(s.ed.buffer().mark, size_t{0});
  CHECK_EQ(s.point(), size_t{6});
}

// ---- [C11] PCRE2 is not used before the first search ----

TEST(m5_pcre2_is_not_compiled_before_the_first_search) {
  const size_t before = geditor::search_pattern_compiles();
  {
    Session s;
    s.settings(R"({"search": {"case_fold": "never"}})");
    s.load(L"abc\ndef\n");
    s.keys(L"C-n C-f M-x");
    s.keys(L"C-g");
    s.keys(L"C-s");  // isearch starts without compiling
    CHECK_EQ(geditor::search_pattern_compiles(), before);
    s.type(L"d");
    CHECK(geditor::search_pattern_compiles() > before);
  }
}

TEST(m5_registry_has_every_m5_command_implemented) {
  for (const char* name :
       {"isearch-forward", "isearch-backward", "isearch-repeat-forward", "isearch-repeat-backward",
        "isearch-delete-char", "isearch-exit", "isearch-abort", "isearch-toggle-regexp",
        "isearch-toggle-case-fold", "isearch-query-replace", "query-replace", "query-replace-regexp",
        "query-replace-act", "query-replace-skip", "query-replace-all", "query-replace-act-and-exit",
        "query-replace-exit", "goto-line"}) {
    const geditor::CommandInfo* info = geditor::find_command(name);
    CHECK(info != nullptr && info->fn != nullptr);
  }
  // The keyboard macros (M6) are still "not implemented yet".
  for (const char* name : {"kmacro-start-macro", "kmacro-end-macro", "kmacro-end-and-call-macro"}) {
    const geditor::CommandInfo* info = geditor::find_command(name);
    CHECK(info != nullptr && info->fn == nullptr);
  }
}

TEST(m5_mode_commands_outside_their_mode_fail) {
  Session s;
  s.load(L"abc\n");
  s.keys(L"M-x");
  s.type(L"isearch-repeat-forward");
  s.keys(L"RET");
  CHECK_EQ(s.message(), std::wstring(L"Not in isearch"));
  s.keys(L"M-x");
  s.type(L"query-replace-act");
  s.keys(L"RET");
  CHECK_EQ(s.message(), std::wstring(L"Not in query-replace"));
  CHECK_EQ(s.text(), std::wstring(L"abc\n"));
}

// ---- isearch_keys / query_replace_keys ----

TEST(m5_isearch_keys_and_query_replace_keys_settings) {
  Session s;
  s.settings(R"({"isearch_keys": {"C-n": "isearch-repeat-forward", "C-s": null},
                 "query_replace_keys": {"r": "query-replace-act", "y": null}})");
  CHECK(s.ed.settings_errors().empty());
  s.load(L"ab ab ab\n");
  s.keys(L"C-s");
  s.type(L"ab");
  s.keys(L"C-n");
  CHECK_EQ(s.point(), size_t{5});
  s.keys(L"C-s");  // unbound in isearch: stays, "is undefined"
  CHECK(s.ed.isearch_active());
  CHECK_EQ(echo(s), std::wstring(L"I-search: ab [C-s is undefined]"));
  s.keys(L"C-g");
  start_query_replace(s, L"M-%", L"ab", L"x");
  s.keys(L"y");  // unbound in query-replace
  CHECK_EQ(s.text(), std::wstring(L"ab ab ab\n"));
  s.keys(L"r");
  CHECK_EQ(s.text(), std::wstring(L"x ab ab\n"));
}

// ---- The matcher (search.*) ----

TEST(m5_search_finds_lines_split_over_pieces) {
  Session s;
  s.load(L"one\ntwo\nthree\n");
  // Edits split the lines over several pieces.
  s.set_point(5);
  s.type(L"W");
  s.set_point(11);
  s.type(L"E");
  CHECK_EQ(s.text(), std::wstring(L"one\ntWwo\nthEree\n"));
  CHECK(s.ed.buffer().text.piece_count() > 3);
  geditor::SearchPattern p;
  CHECK(p.set(L"wwo", false, false));
  SearchMatch m;
  CHECK(p.find_forward(s.ed.buffer().text, 0, false, &m) == geditor::SearchResult::kFound);
  CHECK(m == (SearchMatch{5, 8}));
  CHECK(p.set(L"hE.e+", true, true));
  CHECK(p.find_forward(s.ed.buffer().text, 0, false, &m) == geditor::SearchResult::kFound);
  CHECK(m == (SearchMatch{10, 15}));
  CHECK(p.find_backward(s.ed.buffer().text, 16, 16, &m) == geditor::SearchResult::kFound);
  CHECK(m == (SearchMatch{10, 15}));
  CHECK(p.find_backward(s.ed.buffer().text, 9, 16, &m) == geditor::SearchResult::kNotFound);
  CHECK(p.find_backward(s.ed.buffer().text, 16, 14, &m) == geditor::SearchResult::kNotFound);  // ends after 14
  // ^ sees the text before `from` on its line.
  CHECK(p.set(L"^t", true, true));
  CHECK(p.find_forward(s.ed.buffer().text, 5, false, &m) == geditor::SearchResult::kFound);
  CHECK(m == (SearchMatch{9, 10}));
}

TEST(m5_search_long_lines_are_highlighted_around_the_window) {
  std::wstring line(200000, L'x');
  line[150000] = L'y';
  line[10] = L'y';
  geditor::PieceTree text(line);
  geditor::SearchPattern p;
  CHECK(p.set(L"y", false, true));
  std::vector<SearchMatch> out;
  p.find_all(text, 149000, 151000, 100, &out);
  CHECK_EQ(out.size(), size_t{1});
  if (!out.empty()) CHECK(out[0] == (SearchMatch{150000, 150001}));
  p.find_all(text, 0, 100, 100, &out);
  CHECK_EQ(out.size(), size_t{1});
  // The window does not start the line: ^ is not at its start (NOTBOL), and
  // $ is not at its end (NOTEOL).
  CHECK(p.set(L"^x|x$", true, true));
  p.find_all(text, 100000, 100010, 100, &out);
  CHECK(out.empty());
  p.find_all(text, 0, 10, 100, &out);
  CHECK_EQ(out.size(), size_t{1});
  p.find_all(text, 199990, 200000, 100, &out);
  CHECK_EQ(out.size(), size_t{1});
  if (!out.empty()) CHECK(out[0] == (SearchMatch{199999, 200000}));
}

TEST(m5_undo_merge_groups_since) {
  geditor::PieceTree t(std::wstring(L"abc"));
  geditor::UndoHistory u;
  for (int i = 0; i < 3; ++i) {
    u.begin_command(0, false);
    u.record_insert(0, t.insert(0, L"x"));
    u.end_command(1);
  }
  CHECK_EQ(u.undo_count(), size_t{3});
  u.merge_groups_since(1, 7);
  CHECK_EQ(u.undo_count(), size_t{2});
  size_t point = 0;
  CHECK(u.undo(t, nullptr, &point));
  CHECK_EQ(t.text(), std::wstring(L"xabc"));
  CHECK_EQ(point, size_t{7});
  u.merge_groups_since(5, 0);  // none: nothing happens
  CHECK_EQ(u.undo_count(), size_t{1});
}
