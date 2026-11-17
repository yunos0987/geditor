// Default keys of 2026-11-17: C-h deletes the previous character as DEL
// does (in every default key table that binds DEL); C-u kills back to the
// start of the logical line (backward-kill-line, as Bash's C-u) and
// universal-argument has no default key; C-M-s / C-M-r start a regexp
// isearch (isearch-forward-regexp / isearch-backward-regexp) and repeat it
// inside isearch. Also the S1 rule of the backward search seen from isearch
// (the subject ends at the search limit; tests/test_search_backward.cpp has
// the matcher's tests).

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "commands.h"
#include "fake_host.h"
#include "grapheme.h"
#include "keymap.h"
#include "piece_tree.h"
#include "search.h"

using test::Session;

namespace {

constexpr char kUniversalArgumentOnCU[] = R"({"keys": {"C-u": "universal-argument"}})";

void start_query_replace(Session& s, std::wstring_view from, std::wstring_view to) {
  s.keys(L"M-%");
  s.type(from);
  s.keys(L"RET");
  s.type(to);
  s.keys(L"RET");
}

bool region_active(const Session& s) {
  const geditor::Buffer& b = s.ed.buffer();
  return b.mark_active && b.has_mark;
}

}  // namespace

// ---- C-h ----

TEST(c_h_deletes_one_cluster_backward_like_del) {
  // "a" + U+1F600 (a surrogate pair) + "e" + U+0301 (a combining mark).
  const std::wstring text = L"a\U0001F600e\x0301";
  Session s;
  s.load(text);
  s.set_point(text.size());
  s.keys(L"C-h");
  CHECK_EQ(s.text(), std::wstring(L"a\U0001F600"));  // e and its mark: one cluster
  s.keys(L"C-h");
  CHECK_EQ(s.text(), std::wstring(L"a"));  // the pair: one cluster
  CHECK_EQ(s.point(), size_t{1});
  // DEL does the same and is unchanged.
  Session d;
  d.load(text);
  d.set_point(text.size());
  d.keys(L"DEL");
  CHECK_EQ(d.text(), std::wstring(L"a\U0001F600"));
  // At the start of the buffer: the same message as DEL.
  s.keys(L"C-h");
  CHECK_EQ(s.text(), std::wstring());
  s.keys(L"C-h");
  d.set_point(0);
  d.keys(L"DEL");
  CHECK_EQ(s.message(), d.message());
  CHECK_EQ(s.message(), std::wstring(L"Beginning of buffer"));
}

TEST(c_h_deletes_the_region_like_del) {
  Session s;
  s.load(L"abcdef");
  s.set_point(1);
  s.keys(L"C-SPC");
  s.set_point(4);
  CHECK(region_active(s));
  s.keys(L"C-h");
  CHECK_EQ(s.text(), std::wstring(L"aef"));
  Session d;
  d.load(L"abcdef");
  d.set_point(1);
  d.keys(L"C-SPC");
  d.set_point(4);
  d.keys(L"DEL");
  CHECK_EQ(d.text(), s.text());
  CHECK_EQ(d.point(), s.point());
}

TEST(c_h_in_the_minibuffer_deletes_one_character) {
  Session s;
  s.load(L"abc");
  s.keys(L"M-x");
  s.type(L"forw");
  s.keys(L"C-h");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"for"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  // The minibuffer's own table has no C-h (nor DEL): both come from the
  // global table.
  CHECK(s.ed.keymap(geditor::KeymapKind::kMinibuffer).lookup(*geditor::parse_key_sequence(L"C-h")).result ==
        geditor::Keymap::Result::kUndefined);
}

TEST(c_h_in_isearch_undoes_the_last_input_like_del) {
  Session s;
  s.load(L"fo foo\n");
  s.keys(L"C-s");
  s.type(L"foo");
  CHECK_EQ(s.point(), size_t{6});
  s.keys(L"C-h");
  CHECK(s.ed.isearch_active());
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring(L"fo"));
  CHECK_EQ(s.point(), size_t{2});  // back to the match of "fo"
  Session d;
  d.load(L"fo foo\n");
  d.keys(L"C-s");
  d.type(L"foo");
  d.keys(L"DEL");
  CHECK_EQ(d.ed.isearch_pattern(), s.ed.isearch_pattern());
  CHECK_EQ(d.point(), s.point());
  CHECK_EQ(s.text(), std::wstring(L"fo foo\n"));
}

TEST(c_h_in_query_replace_skips_like_del) {
  Session s;
  s.load(L"a a a\n");
  start_query_replace(s, L"a", L"b");
  CHECK(s.ed.query_replace_active());
  s.keys(L"y");
  s.keys(L"C-h");  // skips the second
  s.keys(L"y");
  CHECK_EQ(s.text(), std::wstring(L"b a b\n"));
  Session d;
  d.load(L"a a a\n");
  start_query_replace(d, L"a", L"b");
  d.keys(L"y DEL y");
  CHECK_EQ(d.text(), s.text());
}

TEST(c_h_settings_binding_wins) {
  Session s;
  s.load(L"abc");
  s.settings(R"({"keys": {"C-h": "forward-char"}})");
  CHECK(s.ed.settings_errors().empty());
  s.set_point(1);
  s.keys(L"C-h");
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  CHECK_EQ(s.point(), size_t{2});
  s.keys(L"DEL");  // DEL is not affected
  CHECK_EQ(s.text(), std::wstring(L"ac"));
  // And the other way: DEL rebound, C-h still deletes.
  Session t;
  t.load(L"abc");
  t.settings(R"({"keys": {"DEL": "forward-char"}})");
  t.set_point(2);
  t.keys(L"C-h");
  CHECK_EQ(t.text(), std::wstring(L"ac"));
}

TEST(c_h_as_a_prefix_needs_null_first) {
  // A settings binding that uses the default command key C-h as a prefix is
  // refused with the message shown; "C-h": null before it makes it possible
  // (README, "キーの割り当て").
  Session s;
  s.load(L"abc");
  s.settings(R"({"keys": {"C-h x": "forward-char"}})");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{1});
  if (!s.ed.settings_errors().empty()) {
    CHECK_EQ(s.ed.settings_errors()[0].message, std::wstring(L"\"C-h x\": \"C-h\" runs a command; binding ignored"));
  }
  s.set_point(2);
  s.keys(L"C-h");
  CHECK_EQ(s.text(), std::wstring(L"ac"));  // still delete-backward-char
  Session t;
  t.load(L"abc");
  t.settings(R"({"keys": {"C-h": null, "C-h x": "forward-char"}})");
  CHECK(t.ed.settings_errors().empty());
  t.keys(L"C-h x");
  CHECK_EQ(t.point(), size_t{1});
  CHECK_EQ(t.text(), std::wstring(L"abc"));
  // After it, as JSON order goes, the null comes too late.
  Session u;
  u.load(L"abc");
  u.settings(R"({"keys": {"C-h x": "forward-char", "C-h": null}})");
  CHECK_EQ(u.ed.settings_errors().size(), size_t{1});
}

// ---- C-u: backward-kill-line ----

TEST(c_u_kills_back_to_the_line_start) {
  Session s;
  s.load(L"one\nabc def\nthree");
  s.set_point(8);  // after "abc "
  s.keys(L"C-u");
  CHECK_EQ(s.text(), std::wstring(L"one\ndef\nthree"));
  CHECK_EQ(s.point(), size_t{4});
  CHECK_EQ(s.host.clipboard, std::wstring(L"abc "));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"one\nabc def\nthree"));
}

TEST(c_u_at_the_line_start_does_nothing_and_is_no_error) {
  Session s;
  s.load(L"ab\ncd\n");
  s.set_point(3);
  s.host.clipboard = L"keep";
  s.keys(L"C-u");
  CHECK_EQ(s.text(), std::wstring(L"ab\ncd\n"));
  CHECK_EQ(s.point(), size_t{3});
  CHECK_EQ(s.host.clipboard, std::wstring(L"keep"));
  CHECK(!s.modified());
  CHECK_EQ(s.message(), std::wstring());
  // No error counted: a macro of C-u at a line start then "x" runs all of
  // its repeats (a failure would stop the run).
  Session m;
  m.load(L"");
  m.keys(L"C-x (");
  m.keys(L"C-u");
  m.type(L"x");
  m.keys(L"RET");
  m.keys(L"C-x )");
  CHECK_EQ(m.text(), std::wstring(L"x\n"));
  m.keys(L"M-3 C-x e");
  CHECK_EQ(m.text(), std::wstring(L"x\nx\nx\nx\n"));
}

TEST(c_u_and_c_k_join_in_text_order) {
  Session s;
  s.load(L"abc def\n");
  s.set_point(3);
  s.keys(L"C-k C-u");
  CHECK_EQ(s.text(), std::wstring(L"\n"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"abc def"));
  Session t;
  t.load(L"abc def\n");
  t.set_point(3);
  t.keys(L"C-u C-k");
  CHECK_EQ(t.text(), std::wstring(L"\n"));
  CHECK_EQ(t.host.clipboard, std::wstring(L"abc def"));
}

TEST(c_u_ignores_a_prefix_argument) {
  Session s;
  s.load(L"one\ntwo three");
  s.set_point(8);
  s.keys(L"M-3 C-u");
  CHECK_EQ(s.text(), std::wstring(L"one\nthree"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"two "));
}

TEST(c_u_in_a_read_only_buffer_is_refused) {
  Session s;
  s.load(L"abc def");
  s.set_point(4);
  s.keys(L"C-x C-q");
  s.keys(L"C-u");
  CHECK_EQ(s.text(), std::wstring(L"abc def"));
  CHECK_EQ(s.message(), std::wstring(L"Buffer is read-only"));
}

TEST(c_u_in_the_minibuffer_kills_the_input_to_its_start) {
  Session s;
  s.load(L"abc");
  s.keys(L"M-x");
  s.type(L"forward");
  s.keys(L"C-b C-b");
  s.keys(L"C-u");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"rd"));
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"M-x "));
  CHECK_EQ(s.host.clipboard, std::wstring(L"forwa"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
}

TEST(c_u_kills_whole_clusters_before_point) {
  // "x" + U+1F600 + "e" + U+0301, then "|y": everything before the bar.
  const std::wstring head = L"x\U0001F600e\x0301";
  Session s;
  s.load(head + L"y");
  s.set_point(head.size());
  s.keys(L"C-u");
  CHECK_EQ(s.text(), std::wstring(L"y"));
  CHECK_EQ(s.host.clipboard, head);
}

TEST(c_u_in_isearch_ends_it_and_kills) {
  Session s;
  s.load(L"ab foo\n");
  s.keys(L"C-s");
  s.type(L"foo");
  s.keys(L"C-u");
  CHECK(!s.ed.isearch_active());
  CHECK_EQ(s.text(), std::wstring(L"\n"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"ab foo"));
}

// ---- universal-argument without a default key ----

TEST(no_default_key_runs_universal_argument) {
  for (const auto& table : {geditor::default_global_bindings(), geditor::default_compat_bindings(),
                            geditor::default_minibuffer_bindings(), geditor::default_isearch_bindings(),
                            geditor::default_query_replace_bindings()}) {
    for (const geditor::DefaultBinding& b : table) {
      if (std::string(b.command) == "universal-argument") {
        std::printf("  bound by default: %s\n", test::to_display(b.keys).c_str());
      }
      CHECK(std::string(b.command) != "universal-argument");
    }
  }
  CHECK(geditor::find_command("universal-argument") != nullptr);  // still registered (M-x, settings)
  // M-x universal-argument still gives a prefix argument.
  Session s;
  s.load(L"0123456789");
  s.keys(L"M-x");
  s.type(L"universal-argument");
  s.keys(L"RET");
  s.keys(L"C-f");
  CHECK_EQ(s.point(), size_t{4});
}

TEST(m_digit_runs_a_macro_three_times) {
  Session s;
  s.load(L"");
  s.keys(L"C-x (");
  s.type(L"ab");
  s.keys(L"C-x )");
  s.keys(L"M-3 C-x e");
  CHECK_EQ(s.text(), std::wstring(L"abababab"));
}

// ---- C-M-s / C-M-r ----

TEST(c_m_s_and_c_m_r_equal_the_prefix_argument_forms) {
  // Today's C-M-s foo / C-M-r foo against the C-u C-s / C-u C-r of before
  // (universal-argument bound to C-u in the settings).
  const std::wstring text = L"x12 y345 z6\n";
  for (const bool forward : {true, false}) {
    for (const wchar_t* pattern : {L"[0-9]+", L"[a-z][0-9]", L"y3"}) {
      Session a;
      a.load(text);
      if (!forward) a.set_point(text.size());
      a.keys(forward ? L"C-M-s" : L"C-M-r");
      const std::wstring prompt = a.ed.echo_area_text();
      a.type(pattern);
      Session b;
      b.load(text);
      b.settings(kUniversalArgumentOnCU);
      if (!forward) b.set_point(text.size());
      b.keys(forward ? L"C-u C-s" : L"C-u C-r");
      CHECK_EQ(b.ed.echo_area_text(), prompt);
      b.type(pattern);
      CHECK_EQ(a.point(), b.point());
      CHECK_EQ(a.ed.echo_area_text(), b.ed.echo_area_text());
    }
  }
  Session s;
  s.load(text);
  s.keys(L"C-M-s");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"Regexp I-search: "));
  s.keys(L"RET");
  s.set_point(text.size());
  s.keys(L"C-M-r");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"Regexp I-search backward: "));
}

TEST(c_m_s_and_c_m_r_repeat_inside_isearch) {
  Session s;
  s.load(L"x12 y345 z6\n");
  s.keys(L"C-M-s");
  s.type(L"[0-9]+");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"C-M-s");
  CHECK_EQ(s.point(), size_t{8});
  s.keys(L"C-M-r");  // turns around: first to the start of the match
  CHECK_EQ(s.point(), size_t{5});
  s.keys(L"C-M-r");  // the last match of the forward scan before 5: "2" (as C-r)
  CHECK_EQ(s.point(), size_t{2});
  CHECK(s.ed.isearch_active());
}

// ---- S1 seen from isearch ----

// Rewritten in retry 1 ([C12]): before it, the first C-r stood still on the
// empty match at the current match's start and the second moved one back.
TEST(s1_backward_dot_star_then_c_r_moves_back_one_character) {
  // From the middle of a line, C-M-r .* then C-r: the repeat is bounded at
  // the current match's start (S1) and starts before it ([C12]). Before S1
  // every ".*" of the line ended after the limit and the search went on to
  // the line before; now ".*" from one character back ends at the limit.
  Session s;
  s.load(L"first\nabcdefghij\n");
  s.set_point(11);  // after "abcde"
  s.keys(L"C-M-r");
  s.type(L".*");
  CHECK_EQ(s.point(), size_t{10});  // ".*" from the "e" before the start
  s.keys(L"C-r");
  CHECK(s.ed.isearch_active());
  CHECK(s.ed.echo_area_text().find(L"Failing") == std::wstring::npos);
  CHECK_EQ(s.point(), size_t{9});  // "d", ending at the limit 10
  s.keys(L"C-r");
  CHECK_EQ(s.point(), size_t{8});
}

// ---- [C12] repeated C-r never returns a match at the current start ----

namespace {

struct RepeatStep {
  size_t point = 0;
  bool has_current = false;
  geditor::SearchMatch current;
  bool failing = false;
  bool wrapped = false;

  bool operator==(const RepeatStep&) const = default;
};

RepeatStep step_of(const Session& s) {
  RepeatStep r;
  r.point = s.point();
  const geditor::Editor::SearchHighlights h = s.ed.search_highlights(40);
  r.has_current = h.has_current;
  r.current = h.current;
  const std::wstring echo = s.ed.echo_area_text();
  r.failing = echo.find(L"Failing") != std::wstring::npos;
  r.wrapped = echo.find(L"Wrapped") != std::wstring::npos;
  return r;
}

// `start_keys` then `pattern` from `point`, then `presses` of `repeat`.
std::vector<RepeatStep> repeat_steps(const std::wstring& text, size_t point, const wchar_t* start_keys,
                                     const std::wstring& pattern, const wchar_t* repeat, int presses, bool old_rule) {
  Session s;
  s.load(text);
  s.ed.use_old_backward_repeat_for_testing(old_rule);
  s.set_point(point);
  s.keys(start_keys);
  s.type(pattern);
  std::vector<RepeatStep> steps{step_of(s)};
  for (int i = 0; i < presses; ++i) {
    s.keys(repeat);
    steps.push_back(step_of(s));
  }
  return steps;
}

}  // namespace

TEST(c12_repeated_c_r_moves_back_on_every_press_for_empty_capable_patterns) {
  // ASCII only: the point is the match start.
  const std::wstring text = L"ab xo\nfoo xoo bar\n";
  for (const wchar_t* pattern : {L".*", L"o*", L"x*", L"()", L"\\b"}) {
    const std::vector<RepeatStep> now = repeat_steps(text, 13, L"C-M-r", pattern, L"C-r", 20, false);
    size_t presses_before_failing = 0;
    bool decreasing = true;
    for (size_t i = 1; i < now.size(); ++i) {
      if (now[i].failing) break;
      ++presses_before_failing;
      if (!(now[i].point < now[i - 1].point)) decreasing = false;
    }
    if (!decreasing) std::printf("  pattern %s does not move back on every press\n", test::to_display(pattern).c_str());
    CHECK(decreasing);
    CHECK(presses_before_failing < 20);  // Failing within 20 presses on this short text
    CHECK(!now.front().failing);
    // Every step's match starts at the point (ASCII) and before the one
    // before it.
    for (size_t i = 1; i < now.size() && !now[i].failing; ++i) {
      CHECK(now[i].has_current);
      CHECK_EQ(now[i].current.start, now[i].point);
    }
    // The wrap after the failure is the same as with the old repeat.
    const size_t n = presses_before_failing + 1;  // up to and with the failing press
    const std::vector<RepeatStep> wrap_now = repeat_steps(text, 13, L"C-M-r", pattern, L"C-r", static_cast<int>(n + 1),
                                                          false);
    std::vector<RepeatStep> old = repeat_steps(text, 13, L"C-M-r", pattern, L"C-r", 60, true);
    size_t old_fail = 0;
    while (old_fail < old.size() && !old[old_fail].failing) ++old_fail;
    CHECK(old_fail + 1 < old.size());
    if (old_fail + 1 < old.size()) {
      CHECK(wrap_now.back().wrapped);
      CHECK(wrap_now.back() == old[old_fail + 1]);
    }
  }
  // The old repeat stood still on the press after a non-empty match for these.
  for (const wchar_t* pattern : {L".*", L"o*"}) {
    const std::vector<RepeatStep> old = repeat_steps(text, 13, L"C-M-r", pattern, L"C-r", 6, true);
    bool stood_still = false;
    for (size_t i = 1; i < old.size(); ++i) stood_still = stood_still || old[i].point == old[i - 1].point;
    CHECK(stood_still);
  }
}

// [C12] inside clusters of several units (retry 2): the rule works on the
// raw match (UTF-16 units); the highlight and the point are snapped to
// cluster boundaries, so some presses leave the display as it is (also
// before retry 1 and at HEAD 006ab9d). The raw matches are those of the
// rule replayed on SearchPattern (the editor keeps its raw match private);
// the editor's displayed start must be their cluster start at every press.
TEST(c12_repeated_c_r_inside_clusters_moves_the_raw_match_only) {
  const std::wstring texts[] = {
      L"a\U0001F468\x200D\U0001F469\x200D\U0001F467" L"b",  // a, a ZWJ family (8 units), b
      L"xe\x0301\x0302\x0303y",                               // x, e with 3 combining marks, y
  };
  for (const std::wstring& text : texts) {
    const geditor::PieceTree tree(text);
    for (const wchar_t* pattern : {L".*", L"o*"}) {
      const std::vector<RepeatStep> now = repeat_steps(text, text.size(), L"C-M-r", pattern, L"C-r", 20, false);
      CHECK(!now.front().failing && now.front().has_current);
      geditor::SearchPattern p;
      CHECK(p.set(pattern, true, false));
      size_t raw = now.front().current.start;
      CHECK_EQ(geditor::cluster_start(tree, raw), raw);  // the start is on a boundary (b, y, or the end)
      size_t unchanged = 0;
      size_t presses = 0;
      bool raw_decreasing = true;
      bool display_never_increases = true;
      bool display_is_snapped_raw = true;
      bool failed = false;
      for (size_t i = 1; i < now.size(); ++i) {
        geditor::SearchMatch m;
        const bool found =
            raw > 0 && p.find_backward(tree, raw - 1, raw, &m) == geditor::SearchResult::kFound;
        if (!found) {
          failed = now[i].failing;  // the editor fails at the same press
          break;
        }
        ++presses;
        raw_decreasing = raw_decreasing && m.start < raw;
        raw = m.start;
        display_is_snapped_raw = display_is_snapped_raw && !now[i].failing && now[i].has_current &&
                                 now[i].current.start == geditor::cluster_start(tree, raw);
        display_never_increases = display_never_increases && now[i].current.start <= now[i - 1].current.start;
        if (now[i].current.start == now[i - 1].current.start && now[i].point == now[i - 1].point) ++unchanged;
      }
      std::printf("  [C12 clusters] %s in %zu units: %zu presses before Failing, %zu of them kept the displayed "
                  "match start and the cursor\n",
                  test::to_display(pattern).c_str(), text.size(), presses, unchanged);
      CHECK(failed);
      CHECK(raw_decreasing);
      CHECK(display_never_increases);
      CHECK(display_is_snapped_raw);
    }
  }
}

TEST(c12_backward_repeats_of_patterns_that_cannot_match_empty_are_unchanged) {
  // Generated lines and start points; for each pattern that cannot match
  // empty, the steps of 25 C-r with the new repeat equal those with the
  // repeat of before retry 1 (use_old_backward_repeat_for_testing). Forward
  // repeats are not compared: the switch does not reach them, so both runs
  // would be the same code.
  struct P {
    const wchar_t* text;
    bool regexp;
  };
  const P patterns[] = {{L"o", false},  {L"oo", false},     {L"ab", false},        {L"o+", true},
                        {L"[a-z]+", true}, {L"\\w\\b", true}, {L".", true},          {L"o.", true},
                        {L"(?<=o)o", true}, {L"a|ab", true},  {L"\\bb", true},       {L"o(?=o)", true}};
  uint64_t state = 0xC12;
  auto below = [&](size_t n) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return static_cast<size_t>(state % n);
  };
  const wchar_t* pieces[] = {L"a", L"b", L"o", L"oo", L" ", L"ab", L"\n", L"x", L"\x6F22", L"\U0001F600"};
  size_t sequences = 0;
  size_t steps = 0;
  size_t different = 0;
  for (int round = 0; round < 30; ++round) {
    std::wstring text;
    const size_t length = 20 + below(60);
    while (text.size() < length) text += pieces[below(std::size(pieces))];
    for (const P& p : patterns) {
      const size_t point = below(text.size() + 1);
      const wchar_t* start = p.regexp ? L"C-M-r" : L"C-r";
      const std::vector<RepeatStep> now = repeat_steps(text, point, start, p.text, L"C-r", 25, false);
      const std::vector<RepeatStep> old = repeat_steps(text, point, start, p.text, L"C-r", 25, true);
      ++sequences;
      steps += now.size();
      if (!(now == old)) {
        ++different;
        if (different <= 3) {
          std::printf("  differs: pattern %s backward from %zu\n", test::to_display(p.text).c_str(), point);
        }
      }
    }
  }
  std::printf("  [C12] %zu backward repeat sequences (%zu steps) of patterns that cannot match empty compared, "
              "%zu different\n",
              sequences, steps, different);
  CHECK(sequences >= 350);
  CHECK_EQ(different, size_t{0});
}
