// M6 (docs/PLAN.md 5 M6): consecutive kills that add to the clipboard
// ([M6-1]), shift selection ([M6-1]), keyboard macros ([M6-2]), and the
// deferred items B2 to B6 (--encoding without a file, read-only-mode kept by
// revert-buffer-with-coding-system, saving a "!" buffer, the scroll bar of
// few long lines).

#include <cstdio>
#include <string>

#include "commands.h"
#include "encoding.h"
#include "fake_host.h"
#include "layout.h"
#include "menu.h"

using geditor::RowPos;
using test::Session;

namespace {

constexpr wchar_t kPath[] = L"C:\\work\\t.txt";

// Opens `bytes` as C:\work\t.txt in `encoding` ("": detected).
void open_bytes(Session& s, const std::string& bytes, const wchar_t* encoding = L"") {
  s.host.add_file(kPath, bytes);
  s.ed.request_open(kPath, encoding);
  CHECK(s.ed.buffer().path == kPath);
}

bool region_active(const Session& s) {
  const geditor::Buffer& b = s.ed.buffer();
  return b.mark_active && b.has_mark;
}

}  // namespace

// ---- [C1] [M6-1] kills in a row add to the clipboard ----

TEST(m6_kill_c_k_c_k_joins_two_lines) {
  Session s;
  s.load(L"ab\ncd\nef");
  s.keys(L"C-k C-k C-k C-k");
  CHECK_EQ(s.text(), std::wstring(L"ef"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"ab\ncd\n"));
  s.keys(L"C-y");
  CHECK_EQ(s.text(), std::wstring(L"ab\ncd\nef"));
}

TEST(m6_kill_m_d_m_d_appends) {
  Session s;
  s.load(L"foo bar baz");
  s.keys(L"M-d M-d");
  CHECK_EQ(s.text(), std::wstring(L" baz"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"foo bar"));
}

TEST(m6_kill_m_del_prepends) {
  Session s;
  s.load(L"foo bar baz");
  s.set_point(11);
  s.keys(L"M-DEL");
  CHECK_EQ(s.host.clipboard, std::wstring(L"baz"));
  s.keys(L"M-DEL");
  CHECK_EQ(s.text(), std::wstring(L"foo "));
  CHECK_EQ(s.host.clipboard, std::wstring(L"bar baz"));
  // A forward kill right after a backward one adds at the end.
  Session t;
  t.load(L"one two three");
  t.set_point(4);
  t.keys(L"M-DEL M-d");
  CHECK_EQ(t.text(), std::wstring(L" three"));
  CHECK_EQ(t.host.clipboard, std::wstring(L"one two"));
}

TEST(m6_kill_after_a_non_kill_command_replaces) {
  Session s;
  s.load(L"ab\ncd");
  s.keys(L"C-k");
  CHECK_EQ(s.host.clipboard, std::wstring(L"ab"));
  s.type(L"x");  // self-insert-command: not a kill
  s.keys(L"C-k");
  CHECK_EQ(s.text(), std::wstring(L"xcd"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"\n"));
}

TEST(m6_kill_move_kill_replaces) {
  Session s;
  s.load(L"ab\ncd\n");
  s.keys(L"C-k");
  s.keys(L"C-n");
  s.keys(L"C-k");
  CHECK_EQ(s.text(), std::wstring(L"\n\n"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"cd"));
}

TEST(m6_kill_region_after_kill_appends_and_prefix_argument_keeps_the_sequence) {
  Session s;
  s.load(L"one\ntwo\nthree\n");
  s.keys(L"C-k C-u 2 C-k");  // C-u between kills does not end the sequence
  CHECK_EQ(s.text(), std::wstring(L"three\n"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"one\ntwo\n"));
}

TEST(m6_kill_sequence_ends_at_a_mouse_click) {
  Session s;
  s.load(L"ab\ncd");
  s.keys(L"C-k");
  CHECK(s.ed.mouse_press(0, false));
  s.ed.mouse_release();
  s.keys(L"C-k");
  CHECK_EQ(s.host.clipboard, std::wstring(L"\n"));
}

TEST(m6_kill_does_not_read_the_clipboard_back) {
  // Another program's text put on the clipboard between two kills is not
  // noticed (documented): the second kill writes the kills of the sequence.
  Session s;
  s.load(L"ab\ncd");
  s.keys(L"C-k");
  s.host.clipboard = L"other";
  s.keys(L"C-k");
  CHECK_EQ(s.host.clipboard, std::wstring(L"ab\n"));
}

// ---- [C2] [M6-1] shift selection: each Shift + movement key of PLAN 4.4 ----

namespace {

// "one two\nthree four\nfive": line 1 starts at 8, line 2 at 19.
constexpr wchar_t kText[] = L"one two\nthree four\nfive";

void check_selection(const Session& s, size_t mark, size_t point) {
  CHECK(region_active(s));
  CHECK(s.ed.buffer().shift_mark);
  CHECK_EQ(s.ed.buffer().mark, mark);
  CHECK_EQ(s.point(), point);
}

// 30 lines "l\n": line k starts at 2k (a 10-row window).
std::wstring thirty_lines() {
  std::wstring t;
  for (int i = 0; i < 30; ++i) t += L"l\n";
  return t;
}

}  // namespace

TEST(m6_shift_select_s_left) {
  Session s;
  s.load(kText);
  s.set_point(3);
  s.keys(L"S-<left>");
  check_selection(s, 3, 2);
}

TEST(m6_shift_select_s_right) {
  Session s;
  s.load(kText);
  s.keys(L"S-<right>");
  check_selection(s, 0, 1);
}

TEST(m6_shift_select_s_up) {
  Session s;
  s.load(kText);
  s.set_point(10);
  s.keys(L"S-<up>");
  check_selection(s, 10, 2);
}

TEST(m6_shift_select_s_down) {
  Session s;
  s.load(kText);
  s.set_point(2);
  s.keys(L"S-<down>");
  check_selection(s, 2, 10);
}

TEST(m6_shift_select_s_home) {
  Session s;
  s.load(kText);
  s.set_point(10);
  s.keys(L"S-<home>");
  check_selection(s, 10, 8);
}

TEST(m6_shift_select_s_end) {
  Session s;
  s.load(kText);
  s.set_point(8);
  s.keys(L"S-<end>");
  check_selection(s, 8, 18);
}

TEST(m6_shift_select_s_prior) {
  Session s;
  s.load(thirty_lines());
  s.keys(L"<next>");  // top line 8, point on line 8
  CHECK_EQ(s.ed.top_line(), size_t{8});
  s.keys(L"C-u 9 C-n");  // line 17, the bottom row
  CHECK_EQ(s.point(), size_t{34});
  s.keys(L"S-<prior>");  // top line 0: point goes up to the bottom row (line 9)
  CHECK_EQ(s.ed.top_line(), size_t{0});
  check_selection(s, 34, 18);
}

TEST(m6_shift_select_s_next) {
  Session s;
  s.load(thirty_lines());
  s.keys(L"S-<next>");
  CHECK_EQ(s.ed.top_line(), size_t{8});
  check_selection(s, 0, 16);
}

TEST(m6_shift_select_c_s_left) {
  Session s;
  s.load(kText);
  s.set_point(7);
  s.keys(L"C-S-<left>");
  check_selection(s, 7, 4);
}

TEST(m6_shift_select_c_s_right) {
  Session s;
  s.load(kText);
  s.keys(L"C-S-<right>");
  check_selection(s, 0, 3);
}

TEST(m6_shift_select_c_s_home) {
  Session s;
  s.load(kText);
  s.set_point(10);
  s.keys(L"C-S-<home>");
  check_selection(s, 10, 0);
}

TEST(m6_shift_select_c_s_end) {
  Session s;
  s.load(kText);
  s.keys(L"C-S-<end>");
  check_selection(s, 0, 23);
}

TEST(m6_shift_select_unshifted_move_removes_the_temporary_mark) {
  Session s;
  s.load(kText);
  s.keys(L"S-<right> S-<right>");
  check_selection(s, 0, 2);  // the second key extends the same region
  s.keys(L"<right>");
  CHECK(!s.ed.buffer().mark_active);
  CHECK(!s.ed.buffer().shift_mark);
  CHECK_EQ(s.point(), size_t{3});
  // C-f (the Emacs key of the same command) removes it too.
  s.keys(L"S-<left> C-f");
  CHECK(!s.ed.buffer().mark_active);
}

TEST(m6_shift_select_keeps_and_extends_an_active_mark) {
  Session s;
  s.load(kText);
  s.keys(L"C-SPC <right>");  // an ordinary region from 0
  s.keys(L"S-<right>");
  CHECK(region_active(s));
  CHECK(!s.ed.buffer().shift_mark);
  CHECK_EQ(s.ed.buffer().mark, size_t{0});
  CHECK_EQ(s.point(), size_t{2});
  s.keys(L"<right>");  // not a shift selection: the region stays
  CHECK(region_active(s));
  CHECK_EQ(s.point(), size_t{3});
}

TEST(m6_shift_select_settings_binding_on_an_s_key_wins) {
  Session s;
  s.settings("{\"keys\": {\"S-<right>\": \"forward-word\"}}");
  CHECK(s.ed.settings_errors().empty());
  s.load(kText);
  s.keys(L"S-<right>");
  CHECK_EQ(s.point(), size_t{3});  // forward-word, without selection
  CHECK(!s.ed.buffer().mark_active);
}

TEST(m6_shift_select_in_the_minibuffer) {
  Session s;
  s.load(kText);
  s.keys(L"M-x");
  s.type(L"abc");
  s.keys(L"S-<left> S-<left>");
  const geditor::Buffer& m = s.ed.minibuffer_buffer();
  CHECK(m.mark_active && m.shift_mark);
  CHECK_EQ(m.mark, size_t{3});
  CHECK_EQ(m.point, size_t{1});
  CHECK(!s.ed.buffer().mark_active);
  s.keys(L"DEL");  // deletes the region
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"a"));
}

TEST(m6_shift_select_then_typing_ends_the_region) {
  Session s;
  s.load(kText);
  s.keys(L"S-<right>");
  s.type(L"x");
  CHECK(!s.ed.buffer().mark_active);
  CHECK(!s.ed.buffer().shift_mark);
  s.keys(L"S-<right>");  // a new temporary mark where point is
  check_selection(s, 2, 3);
}

// ---- [C3] [M6-2] keyboard macros ----

TEST(m6_macro_record_then_run) {
  Session s;
  s.load(L"");
  s.keys(L"C-x (");
  CHECK_EQ(s.message(), std::wstring(L"Defining kbd macro..."));
  CHECK(s.ed.mode_line().ends_with(L"  Def"));
  s.type(L"ab");
  s.keys(L"C-x )");
  CHECK_EQ(s.message(), std::wstring(L"Keyboard macro defined"));
  CHECK(!s.ed.macro_recording());
  CHECK(!s.ed.mode_line().ends_with(L"  Def"));
  CHECK_EQ(s.ed.macro_size(), size_t{2});  // "a" and "b"; not C-x )
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  s.keys(L"C-x e");
  CHECK_EQ(s.text(), std::wstring(L"abab"));
  CHECK_EQ(s.message(), std::wstring(L"(Type e to repeat macro)"));
}

TEST(m6_macro_c_u_3_c_x_e_runs_three_times) {
  Session s;
  s.load(L"");
  s.keys(L"C-x (");
  s.type(L"x");
  s.keys(L"C-x )");
  s.keys(L"C-u 3 C-x e");
  CHECK_EQ(s.text(), std::wstring(L"xxxx"));
}

TEST(m6_macro_e_repeats_after_c_x_e) {
  Session s;
  s.load(L"");
  s.keys(L"C-x (");
  s.type(L"x");
  s.keys(L"C-x )");
  s.keys(L"C-x e");
  CHECK_EQ(s.text(), std::wstring(L"xx"));
  s.keys(L"e");
  s.keys(L"e");
  CHECK_EQ(s.text(), std::wstring(L"xxxx"));
  s.keys(L"C-a e");  // another key ends the repeats: e is a character again
  CHECK_EQ(s.text(), std::wstring(L"exxxx"));
}

TEST(m6_macro_f3_f4) {
  Session s;
  s.load(L"");
  s.keys(L"<f3>");
  CHECK(s.ed.macro_recording());
  s.type(L"ab");
  s.keys(L"<f4>");  // while recording: ends it
  CHECK(!s.ed.macro_recording());
  CHECK_EQ(s.ed.macro_size(), size_t{2});
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  s.keys(L"<f4>");  // runs it
  CHECK_EQ(s.text(), std::wstring(L"abab"));
  s.keys(L"C-x (");  // C-x e also only ends a recording
  s.type(L"c");
  s.keys(L"C-x e");
  CHECK(!s.ed.macro_recording());
  CHECK_EQ(s.text(), std::wstring(L"ababc"));
  s.keys(L"C-x e");
  CHECK_EQ(s.text(), std::wstring(L"ababcc"));
}

TEST(m6_macro_c_g_while_recording_keeps_the_previous_macro) {
  Session s;
  s.load(L"");
  s.keys(L"C-x (");
  s.type(L"a");
  s.keys(L"C-x )");
  s.keys(L"C-x (");
  s.type(L"zz");
  s.keys(L"C-g");
  CHECK(!s.ed.macro_recording());
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  CHECK_EQ(s.ed.macro_size(), size_t{1});
  s.keys(L"C-x e");
  CHECK_EQ(s.text(), std::wstring(L"azza"));
}

TEST(m6_macro_with_minibuffer_input) {
  Session s;
  s.load(L"1\n2\n3\n4\n5\n6\n");
  s.keys(L"C-x (");
  s.keys(L"M-g g");
  s.type(L"5");
  s.keys(L"RET");
  CHECK_EQ(s.point(), size_t{8});
  s.keys(L"M-x");
  s.type(L"end-of-line");
  s.keys(L"RET");
  s.type(L"!");
  s.keys(L"C-x )");
  CHECK_EQ(s.text(), std::wstring(L"1\n2\n3\n4\n5!\n6\n"));
  s.keys(L"M-<");
  s.keys(L"C-x e");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.text(), std::wstring(L"1\n2\n3\n4\n5!!\n6\n"));
}

TEST(m6_macro_ended_by_m_x_drops_the_whole_m_x) {
  Session s;
  s.load(L"");
  s.keys(L"C-x (");
  s.type(L"q");
  s.keys(L"M-x");
  s.type(L"kmacro-end-macro");
  s.keys(L"RET");
  CHECK(!s.ed.macro_recording());
  CHECK_EQ(s.ed.macro_size(), size_t{1});
  s.keys(L"C-x e");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.text(), std::wstring(L"qq"));
}

TEST(m6_macro_with_isearch) {
  Session s;
  s.load(L"foo bar foo bar\n");
  s.keys(L"C-x (");
  s.keys(L"C-s");
  s.type(L"bar");
  s.keys(L"RET");
  s.type(L"!");
  s.keys(L"C-x )");
  CHECK_EQ(s.text(), std::wstring(L"foo bar! foo bar\n"));
  s.keys(L"C-x e");
  CHECK_EQ(s.text(), std::wstring(L"foo bar! foo bar!\n"));
  CHECK(!s.ed.isearch_active());
}

TEST(m6_macro_with_query_replace) {
  Session s;
  s.load(L"a b a b a b\n");
  s.keys(L"C-x (");
  s.keys(L"M-<");
  s.keys(L"M-%");
  s.type(L"a");
  s.keys(L"RET");
  s.type(L"z");
  s.keys(L"RET");
  s.type(L"yq");  // replaces the first "a", then quits
  s.keys(L"C-x )");
  CHECK_EQ(s.text(), std::wstring(L"z b a b a b\n"));
  CHECK(!s.ed.query_replace_active());
  s.keys(L"C-x e");
  CHECK_EQ(s.text(), std::wstring(L"z b z b a b\n"));
  CHECK(!s.ed.query_replace_active());
}

TEST(m6_macro_with_ime_text) {
  Session s;
  s.load(L"");
  s.keys(L"C-x (");
  s.ed.insert_text(L"\x65E5\x672C");
  s.type(L"-");
  s.keys(L"C-x )");
  CHECK_EQ(s.ed.macro_size(), size_t{2});
  s.keys(L"C-x e");
  CHECK_EQ(s.text(), std::wstring(L"\x65E5\x672C-\x65E5\x672C-"));
}

TEST(m6_macro_one_run_is_one_undo) {
  Session s;
  s.load(L"abcdef");
  s.keys(L"C-x (");
  s.type(L"xy");
  s.keys(L"C-f");
  s.type(L"z");
  s.keys(L"C-x )");
  CHECK_EQ(s.text(), std::wstring(L"xyazbcdef"));
  CHECK_EQ(s.point(), size_t{4});
  s.keys(L"C-x e");  // two typing groups and a move: one undo unit
  CHECK_EQ(s.text(), std::wstring(L"xyazxybzcdef"));
  CHECK_EQ(s.point(), size_t{8});
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"xyazbcdef"));
  CHECK_EQ(s.point(), size_t{4});
  // C-u 2: both runs of one call are one undo unit too.
  s.keys(L"C-u 2 C-x e");
  CHECK_EQ(s.text(), std::wstring(L"xyazxybzxyczdef"));
  CHECK_EQ(s.point(), size_t{12});
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"xyazbcdef"));
  CHECK_EQ(s.point(), size_t{4});
  // The macro's first characters do not join the typing just before it.
  s.type(L"q");
  s.keys(L"C-x e");
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"xyazqbcdef"));
}

TEST(m6_macro_error_stops_the_run_and_the_repeats) {
  Session s;
  s.load(L"ab");
  s.keys(L"C-x (");
  s.keys(L"C-f");
  s.type(L"-");
  s.keys(L"C-x )");
  CHECK_EQ(s.text(), std::wstring(L"a-b"));
  s.keys(L"C-u 5 C-x e");  // the second C-f fails at the end of the buffer
  CHECK_EQ(s.text(), std::wstring(L"a-b-"));
  CHECK_EQ(s.message(), std::wstring(L"End of buffer"));
  s.keys(L"e");  // no repeat after a failed run
  CHECK_EQ(s.text(), std::wstring(L"a-b-e"));
}

TEST(m6_macro_failing_search_stops_the_run) {
  Session s;
  s.load(L"foo foo");
  s.keys(L"C-x (");
  s.keys(L"C-s");
  s.type(L"foo");
  s.keys(L"RET");
  s.type(L"x");
  s.keys(L"C-x )");
  CHECK_EQ(s.text(), std::wstring(L"foox foo"));
  s.keys(L"C-u 5 C-x e");
  CHECK_EQ(s.text(), std::wstring(L"foox foox"));
  CHECK(!s.ed.isearch_active());  // the failing isearch ended (as C-g)
  CHECK_EQ(s.point(), size_t{9});
}

TEST(m6_macro_read_only_stops_the_run) {
  Session s;
  s.load(L"");
  s.keys(L"C-x (");
  s.type(L"a");
  s.keys(L"C-x )");
  s.keys(L"C-x C-q");
  s.keys(L"C-u 3 C-x e");
  CHECK_EQ(s.text(), std::wstring(L"a"));
  CHECK_EQ(s.message(), std::wstring(L"Buffer is read-only"));
}

TEST(m6_macro_messages_for_wrong_state_calls) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-x )");
  CHECK_EQ(s.message(), std::wstring(L"Not defining kbd macro"));
  s.keys(L"C-x e");
  CHECK_EQ(s.message(), std::wstring(L"No kbd macro has been defined"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  s.keys(L"C-x (");
  s.type(L"z");
  s.keys(L"C-x (");
  CHECK_EQ(s.message(), std::wstring(L"Already defining keyboard macro"));
  CHECK(s.ed.macro_recording());
  s.keys(L"C-x )");
  CHECK_EQ(s.ed.macro_size(), size_t{1});  // the second C-x ( is not in it
  CHECK_EQ(s.text(), std::wstring(L"zabc"));
  // An empty recording keeps the last macro.
  s.keys(L"C-x ( C-x )");
  CHECK_EQ(s.message(), std::wstring(L"Ignore empty macro"));
  CHECK_EQ(s.ed.macro_size(), size_t{1});
}

// ---- [C5] negative: mouse and menu input are not recorded ----

TEST(m6_macro_does_not_record_mouse_or_menu_input) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-x (");
  s.type(L"x");
  CHECK(s.ed.mouse_press(3, false));
  s.ed.mouse_release();
  CHECK(s.ed.mouse_double_click(0));
  const geditor::MenuItem& select_all = geditor::context_menu_items()[6];
  CHECK(std::string(select_all.command) == "select-all");
  CHECK(s.ed.execute_menu_item(select_all));
  s.type(L"y");  // at the end, where Select All left point
  s.keys(L"C-x )");
  CHECK_EQ(s.ed.macro_size(), size_t{2});  // "x" and "y"
  const std::wstring before = s.text();
  CHECK_EQ(before, std::wstring(L"xabcy"));
  s.keys(L"C-x e");  // no click and no Select All: both characters at point
  CHECK_EQ(s.text(), before + L"xy");
}

// ---- B2: --encoding without a file ----

TEST(m6_b2_new_buffer_takes_the_encoding) {
  Session s;
  s.settings("{\"new_file\": {\"encoding\": \"utf-16le\"}}");
  s.ed.set_new_buffer_encoding(L"shift_jis");
  CHECK_EQ(s.ed.encoding_text(), std::wstring(L"Shift_JIS"));
  CHECK(s.ed.mode_line().find(L"  Shift_JIS ") != std::wstring::npos);
  CHECK(!s.modified());
  // A settings reload does not replace it (as for a named new file).
  s.settings("{\"new_file\": {\"encoding\": \"euc-jp\"}}");
  CHECK_EQ(s.ed.encoding_text(), std::wstring(L"Shift_JIS"));
  s.ed.insert_text(L"\x65E5\x672C");
  s.keys(L"C-x C-w");
  s.type(L"n.txt");
  s.keys(L"RET");
  CHECK_EQ(s.host.file(L"C:\\work\\n.txt"), std::string("\x93\xFA\x96\x7B"));
  // An unknown name: the message, and the encoding stays.
  Session u;
  u.ed.set_new_buffer_encoding(L"big5");
  CHECK(u.message().find(L"Unknown encoding: big5") == 0);
  CHECK_EQ(u.ed.encoding_text(), std::wstring(L"UTF-8"));
}

// ---- B3: revert-buffer-with-coding-system keeps read-only-mode ----

TEST(m6_b3_revert_keeps_read_only_mode) {
  Session s;
  s.load(L"abc\n");
  s.keys(L"C-x C-q");
  CHECK(s.ed.buffer().read_only);
  s.keys(L"C-x RET r");
  s.type(L"shift_jis");
  s.keys(L"RET");
  CHECK_EQ(s.ed.encoding_text(), std::wstring(L"Shift_JIS"));
  CHECK(s.ed.buffer().read_only);
  CHECK(s.ed.mode_line().starts_with(L"-%%"));
  // A writable buffer stays writable.
  Session w;
  w.load(L"abc\n");
  w.keys(L"C-x RET r");
  w.type(L"utf-8");
  w.keys(L"RET");
  CHECK(!w.ed.buffer().read_only);
  // A file with the read-only attribute is read-only after the revert even
  // when C-x C-q turned read-only-mode off.
  Session a;
  a.host.add_file(kPath, "abc\n");
  a.host.set_read_only(kPath, true);
  a.ed.request_open(kPath);
  CHECK(a.ed.buffer().read_only);
  a.keys(L"C-x C-q");
  CHECK(!a.ed.buffer().read_only);
  a.keys(L"C-x RET r");
  a.type(L"utf-8");
  a.keys(L"RET");
  CHECK(a.ed.buffer().read_only);
}

// ---- B4 / B5: saving a "!" buffer ----

namespace {

// Shift_JIS "a" CRLF, an unreadable lead byte 85, "@b": read as
// "a\n\xFFFD@b" (the U+FFFD at offset 2, line 2).
const std::string kBadShiftJis = std::string("a\r\n\x85\x40") + "b";

}  // namespace

TEST(m6_b4_code_page_no_writes_nothing) {
  Session s;
  open_bytes(s, kBadShiftJis, L"shift_jis");
  CHECK_EQ(s.ed.encoding_text(), std::wstring(L"Shift_JIS!"));
  const unsigned long long time = s.host.write_time(kPath);
  s.keys(L"M->");
  s.type(L"z");
  s.host.yes_no_answers.push_back(false);
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.yes_no_dialogs, 1);
  CHECK_EQ(s.host.yes_no_questions.back(),
           std::wstring(L"t.txt was read as Shift_JIS with bytes that are not valid Shift_JIS (first at byte "
                        L"offset 3, line 2; read as U+FFFD); their original bytes will be lost. Save as Shift_JIS "
                        L"anyway?"));
  CHECK_EQ(s.message(), std::wstring(L"Not saved"));
  CHECK_EQ(s.host.file(kPath), kBadShiftJis);
  CHECK_EQ(s.host.write_time(kPath), time);
  CHECK(s.modified());
}

TEST(m6_b4_code_page_yes_stops_at_a_remaining_replacement_character) {
  Session s;
  open_bytes(s, kBadShiftJis, L"shift_jis");
  const unsigned long long time = s.host.write_time(kPath);
  s.keys(L"M->");
  s.type(L"z");
  s.host.yes_no_answers.push_back(true);
  s.keys(L"C-x C-s");
  CHECK_EQ(s.message(), std::wstring(L"Cannot save as Shift_JIS: U+FFFD at line 2, column 0 cannot be written in "
                                     L"Shift_JIS; choose another encoding with set-buffer-file-coding-system"));
  CHECK_EQ(s.point(), size_t{2});
  CHECK_EQ(s.host.file(kPath), kBadShiftJis);
  CHECK_EQ(s.host.write_time(kPath), time);
  CHECK(s.host.saves.empty());  // not even a temporary file
  CHECK_EQ(s.ed.encoding_text(), std::wstring(L"Shift_JIS!"));
  CHECK(s.modified());
}

TEST(m6_b4_code_page_yes_after_deleting_the_replacement_character_saves) {
  Session s;
  open_bytes(s, kBadShiftJis, L"shift_jis");
  s.set_point(2);
  s.keys(L"C-d");
  CHECK_EQ(s.text(), std::wstring(L"a\n@b"));
  s.host.yes_no_answers.push_back(true);
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.yes_no_dialogs, 1);
  CHECK_EQ(s.host.file(kPath), std::string("a\r\n@b"));
  CHECK_EQ(s.ed.encoding_text(), std::wstring(L"Shift_JIS"));
  CHECK(!s.modified());
  s.type(L"c");  // saved: the next save asks nothing
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.yes_no_dialogs, 1);
  CHECK_EQ(s.host.file(kPath), std::string("a\r\nc@b"));
}

TEST(m6_b4_code_page_write_back_only_saves_the_bytes_it_gives) {
  // Code page 932 reads ED 40 as U+7E8A and writes it as FA 5C.
  Session s;
  open_bytes(s, std::string("ab\n\xED\x40\n"), L"shift_jis");
  CHECK_EQ(s.ed.encoding_text(), std::wstring(L"Shift_JIS!"));
  s.keys(L"M->");
  s.type(L"z");
  s.host.yes_no_answers.push_back(true);
  s.keys(L"C-x C-s");
  CHECK(s.host.yes_no_questions.back().find(L"with bytes that Shift_JIS does not give back the same (first at byte "
                                            L"offset 3, line 2)") != std::wstring::npos);
  CHECK_EQ(s.host.file(kPath), std::string("ab\n\xFA\x5C\nz"));
  CHECK_EQ(s.ed.encoding_text(), std::wstring(L"Shift_JIS"));
  CHECK(!s.modified());
}

TEST(m6_b4_utf8_behaviour_is_unchanged) {
  Session s;
  open_bytes(s, std::string("a\xFF"), L"utf-8");
  s.keys(L"M->");
  s.type(L"b");
  s.host.yes_no_answers.push_back(false);
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.yes_no_questions.back(),
           std::wstring(L"t.txt was read as UTF-8 with bytes that are not valid UTF-8 (first at byte offset 1, "
                        L"line 1; read as U+FFFD); their original bytes will be lost. Save as UTF-8 anyway?"));
  CHECK_EQ(s.host.file(kPath), std::string("a\xFF"));
  s.host.yes_no_answers.push_back(true);
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.file(kPath), std::string("a\xEF\xBF\xBD" "b"));
  CHECK_EQ(s.ed.encoding_text(), std::wstring(L"UTF-8"));
}

TEST(m6_b5_question_names_the_encoding_the_file_was_read_with) {
  // A "UTF-8!" buffer switched to Shift_JIS.
  Session s;
  open_bytes(s, std::string("a\xFF"), L"utf-8");
  s.keys(L"C-x RET f");
  s.type(L"shift_jis");
  s.keys(L"RET");
  CHECK_EQ(s.ed.encoding_text(), std::wstring(L"Shift_JIS!"));
  s.host.yes_no_answers.push_back(true);
  s.keys(L"C-x C-s");
  CHECK(s.host.yes_no_questions.back().starts_with(
      L"t.txt was read as UTF-8 with bytes that are not valid UTF-8 (first at byte offset 1, line 1;"));
  CHECK(s.host.yes_no_questions.back().ends_with(L"Save as Shift_JIS anyway?"));
  // The U+FFFD cannot be written in Shift_JIS: the save stops there.
  CHECK(s.message().starts_with(L"Cannot save as Shift_JIS: U+FFFD at line 1, column 1"));
  CHECK_EQ(s.host.file(kPath), std::string("a\xFF"));
}

// ---- B6: the scroll bar of few long lines ----

TEST(m6_b6_scroll_bar_by_offset_for_a_long_line) {
  Session s;  // 10 rows; wrap max 80
  s.load(std::wstring(5000, L'x'));  // 63 rows of 80 cells (the last 40)
  geditor::ScrollBarState bar = s.ed.scroll_bar();
  CHECK(bar.by_offset);
  CHECK_EQ(bar.unit, size_t{1});
  CHECK_EQ(bar.max, size_t{5000});
  CHECK_EQ(bar.page, size_t{800});
  CHECK_EQ(bar.pos, size_t{0});
  s.keys(L"M->");
  CHECK(s.ed.top() == (RowPos{0, 53}));
  bar = s.ed.scroll_bar();
  CHECK_EQ(bar.pos, size_t{4240});
  CHECK_EQ(bar.page, size_t{760});  // 9 rows of 80 and the last 40
  // The thumb dragged to an offset: its row goes to the top.
  s.ed.scroll_to_bar_position(2000);
  CHECK(s.ed.top() == (RowPos{0, 25}));
  CHECK_EQ(s.ed.scroll_bar().pos, size_t{2000});
  s.ed.scroll_to_bar_position(0);
  CHECK(s.ed.top() == (RowPos{0, 0}));
  s.ed.scroll_to_bar_position(bar.max);  // SB_BOTTOM: the last screen
  CHECK(s.ed.top() == (RowPos{0, 53}));
  // Near the end, the rows from there do not fill the window: the last
  // screen too.
  s.ed.scroll_to_bar_position(0);
  s.ed.scroll_to_bar_position(4500);
  CHECK(s.ed.top() == (RowPos{0, 53}));
}

TEST(m6_b6_scroll_bar_units_keep_the_range_in_16_bits) {
  Session s;
  s.load(std::wstring(200000, L'x'));
  const geditor::ScrollBarState bar = s.ed.scroll_bar();
  CHECK(bar.by_offset);
  CHECK_EQ(bar.unit, size_t{4});  // 200000 / 0xFFFF + 1
  CHECK(bar.max <= 0xFFFF);
  s.ed.scroll_to_bar_position(25000);  // offset 100000: row 1250
  CHECK(s.ed.top() == (RowPos{0, 1250}));
  CHECK_EQ(s.ed.scroll_bar().pos, size_t{25000});
}

TEST(m6_b6_scroll_bar_does_not_lay_out_the_whole_line) {
  Session s;
  s.load(std::wstring(1000000, L'x'));
  s.ed.layout().reset_scanned();
  const geditor::ScrollBarState bar = s.ed.scroll_bar();
  CHECK(bar.by_offset);
  CHECK(s.ed.layout().scanned() < 10000);
  s.ed.scroll_to_bar_position(bar.max / 2);
  s.ed.layout().reset_scanned();
  s.ed.scroll_bar();
  // Walks from the recorded states before the top row (a few of them), not
  // from the start of the 1M-unit line.
  std::printf("  scanned for the bar in the middle of the line: %zu\n", s.ed.layout().scanned());
  CHECK(s.ed.layout().scanned() <= 3 * geditor::WrapLayout::kCheckpointInterval);
}

// ---- M6 retry 1 ----

// R1: kills in a macro run join in memory and reach the clipboard once.
TEST(m6_r1_macro_forward_kills_write_the_joined_text_once) {
  Session s;
  s.load(L"a\nb\nc\nd\ne\n");
  s.keys(L"C-x ( C-k C-k C-x )");
  CHECK_EQ(s.host.clipboard, std::wstring(L"a\n"));
  s.host.clipboard_writes = 0;
  s.keys(L"C-u 3 C-x e");
  CHECK_EQ(s.text(), std::wstring(L"e\n"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"b\nc\nd\n"));
  CHECK_EQ(s.host.clipboard_writes, 1);
}

TEST(m6_r1_macro_backward_kills_prepend) {
  Session s;
  s.load(L"one two three four");
  s.set_point(18);
  s.keys(L"C-x ( M-DEL C-x )");
  CHECK_EQ(s.host.clipboard, std::wstring(L"four"));
  s.host.clipboard_writes = 0;
  s.keys(L"C-u 2 C-x e");
  CHECK_EQ(s.text(), std::wstring(L"one "));
  CHECK_EQ(s.host.clipboard, std::wstring(L"two three "));
  CHECK_EQ(s.host.clipboard_writes, 1);
}

TEST(m6_r1_sequence_of_mixed_kills_joins_both_ways) {
  // A command sequence defers the write too: M-d appends, then M-DEL
  // prepends.
  Session s;
  s.settings("{\"keys\": {\"C-c k\": [[\"kill-word\"], [\"backward-kill-word\"]]}}");
  CHECK(s.ed.settings_errors().empty());
  s.load(L"aa bb cc dd");
  s.set_point(6);
  s.host.clipboard_writes = 0;
  s.keys(L"C-c k");
  CHECK_EQ(s.text(), std::wstring(L"aa  dd"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"bb cc"));
  CHECK_EQ(s.host.clipboard_writes, 1);
}

TEST(m6_r1_waiting_kills_reach_the_clipboard_before_a_yank) {
  Session s;
  s.load(L"ab\ncd");
  s.keys(L"C-x ( C-k C-y C-x )");
  CHECK_EQ(s.text(), std::wstring(L"ab\ncd"));
  s.keys(L"C-n C-a C-x e");  // kills "cd" and yanks it back
  CHECK_EQ(s.host.clipboard, std::wstring(L"cd"));
  CHECK_EQ(s.text(), std::wstring(L"ab\ncd"));
}

TEST(m6_r1_failed_clipboard_write_at_the_end_of_a_run_says_so_and_undo_restores) {
  Session s;
  s.load(L"a\nb\nc\n");
  s.keys(L"C-x ( C-k C-k C-x )");
  s.host.fail_clipboard = true;
  s.keys(L"C-x e");
  CHECK_EQ(s.text(), std::wstring(L"c\n"));
  CHECK_EQ(s.message(),
           std::wstring(L"Cannot write to the clipboard; the killed text is not on it (C-/ brings it back)"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"b\nc\n"));
  // At the top level the old rule stays: a failed write kills nothing.
  s.keys(L"C-k");
  CHECK_EQ(s.message(), std::wstring(L"Cannot write to the clipboard"));
  CHECK_EQ(s.text(), std::wstring(L"b\nc\n"));
}

// R2: C-g during a run stops it; what was done is one undo unit.
TEST(m6_r2_c_g_stops_a_macro_run) {
  Session s;
  s.load(L"");
  s.keys(L"C-x (");
  s.type(L"x");
  s.keys(L"C-x )");
  s.ed.set_macro_poll_ms_for_testing(0);  // ask before every event
  s.host.quit_at_poll = 5;                // C-g before the fifth event
  s.keys(L"C-u 1 0 0 C-x e");
  CHECK_EQ(s.text(), std::wstring(L"xxxxx"));  // the recording and 4 events
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  CHECK_EQ(s.host.quit_polls, 5);
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"x"));
  s.type(L"e");  // no repeat after a stopped run
  CHECK_EQ(s.text(), std::wstring(L"xe"));
}

// R3: a question answered No or Cancel stops the run and its repeats.
TEST(m6_r3_declined_questions_stop_the_run) {
  Session s;
  s.load(L"abc\n");
  s.type(L"z");
  s.host.yes_no_answers.push_back(false);
  s.keys(L"C-x ( C-x RET r");
  s.type(L"utf-8");
  s.keys(L"RET C-x )");
  CHECK_EQ(s.host.yes_no_dialogs, 1);
  for (int i = 0; i < 3; ++i) s.host.yes_no_answers.push_back(false);
  s.keys(L"C-u 3 C-x e");
  CHECK_EQ(s.host.yes_no_dialogs, 2);  // asked once: No stopped the run
  CHECK_EQ(s.message(), std::wstring(L"Cancelled"));
  // Cancel of the save question of C-x C-c.
  s.host.save_answers.push_back(geditor::SaveChoice::kCancel);
  s.keys(L"C-x ( C-x C-c C-x )");
  CHECK_EQ(s.host.save_dialogs, 1);
  for (int i = 0; i < 3; ++i) s.host.save_answers.push_back(geditor::SaveChoice::kCancel);
  s.keys(L"C-u 3 C-x e");
  CHECK_EQ(s.host.save_dialogs, 2);
  CHECK_EQ(s.host.quit_calls, 0);
  // Cancel of the open choice of C-x C-f.
  s.host.add_file(L"C:\\work\\other.txt", "other\n");
  s.host.open_answers.push_back(geditor::OpenChoice::kCancel);
  s.keys(L"C-x ( C-x C-f");
  s.type(L"other.txt");
  s.keys(L"RET C-x )");
  CHECK_EQ(s.host.open_dialogs, 1);
  for (int i = 0; i < 3; ++i) s.host.open_answers.push_back(geditor::OpenChoice::kCancel);
  s.keys(L"C-u 3 C-x e");
  CHECK_EQ(s.host.open_dialogs, 2);
  // No to overwriting a file in write-file.
  s.host.yes_no_answers.clear();
  s.host.yes_no_answers.push_back(false);
  s.keys(L"C-x ( C-x C-w C-a C-k");
  s.type(L"C:\\work\\other.txt");
  s.keys(L"RET C-x )");
  CHECK_EQ(s.host.yes_no_dialogs, 3);
  for (int i = 0; i < 3; ++i) s.host.yes_no_answers.push_back(false);
  s.keys(L"C-u 3 C-x e");
  CHECK_EQ(s.host.yes_no_dialogs, 4);
  CHECK_EQ(s.host.file(L"C:\\work\\other.txt"), std::string("other\n"));
}

// R4: the wheel, the scroll bar and a dropped file end the `e` repeat.
TEST(m6_r4_other_input_ends_the_e_repeat) {
  std::wstring lines;
  for (int i = 0; i < 40; ++i) lines += L"l\n";
  for (int which = 0; which < 4; ++which) {
    Session s;
    s.load(lines);
    s.keys(L"C-x (");
    s.type(L"x");
    s.keys(L"C-x ) C-x e");
    CHECK_EQ(s.message(), std::wstring(L"(Type e to repeat macro)"));
    if (which == 0) s.ed.scroll_by_rows(1);  // the wheel and the arrows of the bar
    if (which == 1) s.ed.scroll_to_bar_position(2);
    if (which == 2) {
      s.host.open_answers.push_back(geditor::OpenChoice::kCancel);
      s.ed.request_open(L"C:\\work\\dropped.txt");  // a drop
    }
    if (which == 3) s.ed.set_point(0);
    const size_t at = s.ed.buffer().point;
    s.type(L"e");
    CHECK_EQ(s.text().substr(at, 1), std::wstring(L"e"));
    CHECK(s.text().find(L"xxx") == std::wstring::npos);
  }
}

// R5: after a run that ends inside isearch, `e` is isearch input.
TEST(m6_r5_e_after_a_run_ending_in_isearch_goes_to_isearch) {
  Session s;
  s.load(L"one e two");
  s.keys(L"C-x ( C-s C-x )");  // C-x ends isearch; the macro is C-s
  CHECK_EQ(s.ed.macro_size(), size_t{1});
  s.keys(L"M-< C-x e");
  CHECK(s.ed.isearch_active());
  CHECK(s.message() != std::wstring(L"(Type e to repeat macro)"));
  s.type(L"e");
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring(L"e"));
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"I-search: e"));
}

// R6: the save question names the file that was read, also on save-as.
TEST(m6_r6_save_question_names_the_read_file_on_write_file) {
  for (const wchar_t* encoding : {L"shift_jis", L"utf-8"}) {
    Session s;
    open_bytes(s, std::wstring(encoding) == L"utf-8" ? std::string("a\xFF") : kBadShiftJis, encoding);
    s.host.yes_no_answers.push_back(false);
    s.keys(L"C-x C-w C-a C-k");
    s.type(L"C:\\work\\b4_other.bin");
    s.keys(L"RET");
    CHECK_EQ(s.host.yes_no_dialogs, 1);
    CHECK(s.host.yes_no_questions.back().starts_with(L"t.txt was read as "));
    CHECK(s.host.yes_no_questions.back().find(L"b4_other.bin") == std::wstring::npos);
    CHECK(!s.host.has_file(L"C:\\work\\b4_other.bin"));
  }
}

TEST(m6_b6_scroll_bar_stays_by_line_when_it_fits_or_has_many_lines) {
  Session fits;
  fits.load(L"short\ntext\n");
  CHECK(!fits.ed.scroll_bar().by_offset);
  CHECK_EQ(fits.ed.scroll_bar().max, size_t{2});
  Session many;
  std::wstring text;
  for (int i = 0; i < 30; ++i) text += std::wstring(200, L'y') + L"\n";
  many.load(text);
  const geditor::ScrollBarState bar = many.ed.scroll_bar();
  CHECK(!bar.by_offset);
  CHECK_EQ(bar.max, size_t{30});
  CHECK_EQ(bar.page, size_t{10});
}
