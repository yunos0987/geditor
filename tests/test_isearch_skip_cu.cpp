// Decisions of 2026-11-18:
// - 4 a: a repeated isearch (C-s / C-r inside isearch) returns only a match
//   that moves the cursor: forward its displayed end (widened to grapheme
//   clusters) is after the current one's, backward its displayed start is
//   before the current one's. The matches inside the same cluster are
//   skipped; DEL goes back over one press however many were skipped.
// - 5 b: C-u inside isearch empties the search string (isearch-delete-pattern)
//   and isearch goes on; C-u inside query-replace does nothing (ignore);
//   C-u in the buffer and the minibuffer stays backward-kill-line.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "commands.h"
#include "fake_host.h"
#include "keymap.h"
#include "search.h"

using test::Session;

namespace {

const std::wstring kFamily = L"\U0001F468\x200D\U0001F469\x200D\U0001F467";  // 8 units, one cluster

struct ClusterText {
  const char* name;
  std::wstring text;
  std::wstring inner;  // one code point inside a cluster (the non-regexp search)
};

std::vector<ClusterText> cluster_texts() {
  return {
      {"ZWJ family", L"a" + kFamily + L"b", L"\x200D"},
      {"e + 3 marks", L"xe\x0301\x0302\x0303y", L"\x0301"},
      {"two flags", L"a\U0001F1EF\U0001F1F5\U0001F1FA\U0001F1F8b", L"\U0001F1F5"},
      {"U+2639 U+FE0F", L"a\x2639\xFE0F" L"b", L"\xFE0F"},
  };
}

struct Shown {
  size_t point = 0;
  bool has_current = false;
  geditor::SearchMatch current;  // the displayed (snapped) current match
  bool failing = false;
  bool wrapped = false;
};

Shown shown(const Session& s) {
  Shown r;
  r.point = s.point();
  const geditor::Editor::SearchHighlights h = s.ed.search_highlights(40);
  r.has_current = h.has_current;
  r.current = h.current;
  const std::wstring echo = s.ed.echo_area_text();
  r.failing = echo.starts_with(L"Failing");
  r.wrapped = echo.find(L"rapped") != std::wstring::npos;
  return r;
}

// One [C1] case: `presses` repeats; true when every rule holds. `fail_at`:
// the first failing press (0: none); `kept`: presses that kept the cursor
// although they did not fail.
bool run_case(const std::wstring& text, bool forward, bool regexp, const std::wstring& pattern, int presses,
              bool raw_rule, size_t* fail_at, size_t* kept) {
  Session s;
  s.load(text);
  s.ed.use_raw_repeat_for_testing(raw_rule);
  s.set_point(forward ? 0 : text.size());
  s.keys(forward ? (regexp ? L"C-M-s" : L"C-s") : (regexp ? L"C-M-r" : L"C-r"));
  s.type(pattern);
  Shown before = shown(s);
  bool ok = !before.failing && before.has_current;
  *fail_at = 0;
  *kept = 0;
  for (int i = 1; i <= presses; ++i) {
    s.keys(forward ? L"C-s" : L"C-r");
    const Shown now = shown(s);
    if (before.failing) {
      ok = ok && now.wrapped;  // the press after a failing one wraps
    } else if (now.failing) {
      if (*fail_at == 0) *fail_at = static_cast<size_t>(i);
      ok = ok && now.point == before.point;
    } else {
      if (now.point == before.point) ++*kept;
      ok = ok && now.point != before.point && now.has_current && before.has_current;
      if (forward) {
        ok = ok && now.current.end > before.current.end && now.point == now.current.end;
      } else {
        ok = ok && now.current.start < before.current.start && now.point == now.current.start;
      }
    }
    before = now;
  }
  return ok && *fail_at != 0;
}

}  // namespace

// ---- [C1] the cursor moves on every repeat ----

TEST(repeat_moves_the_cursor_on_every_press_inside_clusters) {
  struct P {
    const wchar_t* text;  // nullptr: the text's inner code point, not a regexp
    bool regexp;
  };
  const P patterns[] = {{L".", true}, {L".*", true}, {L"o*", true}, {L"\\X", true}, {nullptr, false}};
  size_t cases = 0;
  size_t raw_kept_total = 0;
  for (const ClusterText& t : cluster_texts()) {
    for (const P& p : patterns) {
      const std::wstring pattern = p.text ? std::wstring(p.text) : t.inner;
      for (const bool forward : {true, false}) {
        size_t fail_at = 0;
        size_t kept = 0;
        const bool ok = run_case(t.text, forward, p.regexp, pattern, 30, false, &fail_at, &kept);
        size_t raw_fail_at = 0;
        size_t raw_kept = 0;
        run_case(t.text, forward, p.regexp, pattern, 30, true, &raw_fail_at, &raw_kept);
        raw_kept_total += raw_kept;
        ++cases;
        std::printf("  [C1] %-14s %-9s %s: %zu presses to Failing%s (rule of HEAD 078b156: %zu presses kept the "
                    "cursor)\n",
                    t.name, p.text ? test::to_display(p.text).c_str() : "inner cp", forward ? "C-s" : "C-r",
                    fail_at, ok ? "" : "  RULE BROKEN", raw_kept);
        CHECK(ok);
        CHECK_EQ(kept, size_t{0});
      }
    }
  }
  CHECK_EQ(cases, size_t{40});
  CHECK(raw_kept_total > 0);  // the old rule stood still in some of these cases
}

TEST(repeat_dot_over_a_zwj_family_lands_on_each_cluster) {
  // a, the family (units 1 to 9), b: the forward points are 1, 9, 10, then
  // Failing; backward from the end 9, 1, 0, then Failing.
  const std::wstring text = L"a" + kFamily + L"b";
  Session s;
  s.load(text);
  s.keys(L"C-M-s");
  s.type(L".");
  std::vector<size_t> points{s.point()};
  for (int i = 0; i < 3; ++i) {
    s.keys(L"C-s");
    points.push_back(s.point());
  }
  CHECK(points == (std::vector<size_t>{1, 9, 10, 10}));
  CHECK(s.ed.echo_area_text().starts_with(L"Failing"));
  s.keys(L"C-s");
  CHECK(s.ed.echo_area_text().starts_with(L"Wrapped"));
  CHECK_EQ(s.point(), size_t{1});
  Session b;
  b.load(text);
  b.set_point(text.size());
  b.keys(L"C-M-r");
  b.type(L".");
  std::vector<size_t> back{b.point()};
  for (int i = 0; i < 3; ++i) {
    b.keys(L"C-r");
    back.push_back(b.point());
  }
  CHECK(back == (std::vector<size_t>{9, 1, 0, 0}));
  CHECK(b.ed.echo_area_text().starts_with(L"Failing"));
  // The repeat of HEAD 078b156: after reaching 9 the cursor stayed there for
  // the matches of the other four code points of the family.
  Session r;
  r.load(text);
  r.ed.use_raw_repeat_for_testing(true);
  r.keys(L"C-M-s");
  r.type(L".");
  std::vector<size_t> raw{r.point()};
  for (int i = 0; i < 6; ++i) {
    r.keys(L"C-s");
    raw.push_back(r.point());
  }
  CHECK(raw == (std::vector<size_t>{1, 9, 9, 9, 9, 9, 10}));
}

// ---- [C2] texts of one-unit clusters: no change ----

TEST(repeat_on_one_unit_clusters_equals_the_rule_of_head_078b156) {
  // Generated texts of ASCII, kana, kanji, line breaks and empty lines: every
  // cluster is one UTF-16 unit. 20 repeats forward and backward, with the
  // repeat of HEAD 078b156 (use_raw_repeat_for_testing) as the base.
  struct P {
    const wchar_t* text;
    bool regexp;
  };
  const P patterns[] = {{L"o", false},     {L"oo", false},   {L"\x3042", false}, {L"o+", true},  {L"[a-z]+", true},
                        {L".", true},      {L"\\w+", true},  {L"o*", true},      {L".*", true},  {L"x*", true},
                        {L"()", true},     {L"\\b", true},   {L"^", true},       {L"$", true},   {L"\\X", true}};
  const wchar_t* pieces[] = {L"a",      L"b",      L"o",  L"oo", L" ",    L"ab",
                             L"\x3042", L"\x30AB", L"\x6F22", L"\n", L"\n\n", L"x"};
  uint64_t state = 0x4A;
  auto below = [&](size_t n) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return static_cast<size_t>(state % n);
  };
  struct Step {
    bool has_raw = false;
    geditor::SearchMatch raw;
    size_t point = 0;
    bool failing = false;
    bool wrapped = false;
    bool operator==(const Step&) const = default;
  };
  auto run = [&](const std::wstring& text, size_t point, const P& p, bool forward, bool raw_rule) {
    Session s;
    s.load(text);
    s.ed.use_raw_repeat_for_testing(raw_rule);
    s.set_point(point);
    s.keys(forward ? (p.regexp ? L"C-M-s" : L"C-s") : (p.regexp ? L"C-M-r" : L"C-r"));
    s.type(p.text);
    std::vector<Step> steps;
    for (int i = 0; i <= 20; ++i) {
      if (i > 0) s.keys(forward ? L"C-s" : L"C-r");
      Step st;
      st.has_raw = s.ed.isearch_raw_match_for_testing(&st.raw);
      st.point = s.point();
      const std::wstring echo = s.ed.echo_area_text();
      st.failing = echo.starts_with(L"Failing");
      st.wrapped = echo.find(L"rapped") != std::wstring::npos;
      steps.push_back(st);
    }
    return steps;
  };
  size_t sequences = 0;
  size_t steps = 0;
  size_t different = 0;
  for (int round = 0; round < 25; ++round) {
    std::wstring text;
    const size_t length = 10 + below(50);
    while (text.size() < length) text += pieces[below(std::size(pieces))];
    for (const P& p : patterns) {
      const size_t point = below(text.size() + 1);
      for (const bool forward : {true, false}) {
        const std::vector<Step> now = run(text, point, p, forward, false);
        const std::vector<Step> base = run(text, point, p, forward, true);
        ++sequences;
        steps += now.size();
        if (!(now == base)) {
          ++different;
          if (different <= 3) {
            std::printf("  differs: %s %s from %zu\n", test::to_display(p.text).c_str(), forward ? "C-s" : "C-r",
                        point);
          }
        }
      }
    }
  }
  std::printf("  [C2] %zu sequences (%zu steps of raw match, point, Failing, Wrapped) compared with the repeat of "
              "HEAD 078b156, %zu different\n",
              sequences, steps, different);
  CHECK_EQ(sequences, size_t{25 * std::size(patterns) * 2});
  CHECK_EQ(different, size_t{0});
}

// ---- [C3] DEL and keyboard macros ----

TEST(del_after_a_repeat_that_skipped_matches_goes_back_one_press) {
  const std::wstring text = L"a" + kFamily + L"b";
  // C-M-s . (point 1), C-s (point 9, raw U+1F468), C-s skips the ZWJ and the
  // other code points of the family (point 10).
  Session s;
  s.load(text);
  s.keys(L"C-M-s");
  s.type(L".");
  s.keys(L"C-s");
  geditor::SearchMatch raw_before;
  CHECK(s.ed.isearch_raw_match_for_testing(&raw_before));
  const Shown before = shown(s);
  CHECK_EQ(before.point, size_t{9});
  s.keys(L"C-s");
  CHECK_EQ(s.point(), size_t{10});
  s.keys(L"DEL");
  geditor::SearchMatch raw_after;
  CHECK(s.ed.isearch_raw_match_for_testing(&raw_after));
  CHECK(raw_after == raw_before);
  CHECK_EQ(s.point(), before.point);
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring(L"."));
  CHECK(!shown(s).failing);
  // A failing press that skipped matches (the second ZWJ): DEL goes back to
  // the first ZWJ, not failing.
  Session f;
  f.load(text);
  f.keys(L"C-s");
  f.type(L"\x200D");
  geditor::SearchMatch first;
  CHECK(f.ed.isearch_raw_match_for_testing(&first));
  CHECK_EQ(first.start, size_t{3});
  CHECK_EQ(f.point(), size_t{9});
  f.keys(L"C-s");
  CHECK(shown(f).failing);
  CHECK_EQ(f.point(), size_t{9});
  f.keys(L"DEL");
  geditor::SearchMatch back;
  CHECK(f.ed.isearch_raw_match_for_testing(&back));
  CHECK(back == first);
  CHECK(!shown(f).failing);
  CHECK_EQ(f.point(), size_t{9});
}

TEST(repeat_in_a_keyboard_macro_equals_typed_keys) {
  std::wstring text;
  for (int i = 0; i < 4; ++i) text += L"a" + kFamily + L"b ";
  const wchar_t* keys = L"C-M-s . C-s C-s RET";
  // Typed three times.
  Session typed;
  typed.load(text);
  std::vector<size_t> typed_points;
  for (int i = 0; i < 3; ++i) {
    typed.keys(L"C-M-s");
    typed.type(L".");
    typed.keys(L"C-s C-s RET");
    typed_points.push_back(typed.point());
  }
  // Recorded once (it runs while recorded), then run twice.
  Session m;
  m.load(text);
  m.keys(L"C-x (");
  m.keys(L"C-M-s");
  m.type(L".");
  m.keys(L"C-s C-s RET");
  m.keys(L"C-x )");
  std::vector<size_t> macro_points{m.point()};
  m.keys(L"C-x e");
  macro_points.push_back(m.point());
  m.keys(L"C-x e");
  macro_points.push_back(m.point());
  std::printf("  [C3] %s typed: %zu %zu %zu; macro: %zu %zu %zu\n", test::to_display(keys).c_str(), typed_points[0],
              typed_points[1], typed_points[2], macro_points[0], macro_points[1], macro_points[2]);
  CHECK(typed_points == macro_points);
  // Copies of 11 units: b (10), the second family (20), the third a (23).
  CHECK(typed_points == (std::vector<size_t>{10, 20, 23}));
  // Two runs with one call: the skipped matches are no failure (the run is
  // not stopped).
  Session n;
  n.load(text);
  n.keys(L"C-x (");
  n.keys(L"C-M-s");
  n.type(L".");
  n.keys(L"C-s C-s RET");
  n.keys(L"C-x )");
  const uint64_t errors = n.ed.errors_for_testing();
  n.keys(L"M-2 C-x e");
  CHECK_EQ(n.point(), size_t{23});
  CHECK_EQ(n.ed.errors_for_testing(), errors);
}

// ---- [C4] time over a cluster of 4096 units ----

TEST(repeat_over_a_cluster_of_4096_units_time) {
  // "xx", e + 4095 x U+0301 (units 2 to 4098), "yy".
  std::wstring text = L"xxe";
  text.append(4095, L'\x0301');
  text += L"yy";
  Session s;
  s.load(text);
  s.keys(L"C-M-s");
  s.type(L".");
  s.keys(L"C-s C-s");  // the second x, then the cluster (its first unit)
  CHECK_EQ(s.point(), size_t{4098});
  auto t0 = std::chrono::steady_clock::now();
  s.keys(L"C-s");  // skips the 4095 marks
  auto t1 = std::chrono::steady_clock::now();
  CHECK_EQ(s.point(), size_t{4099});
  const double forward_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  Session b;
  b.load(text);
  b.set_point(text.size());
  b.keys(L"C-M-r");
  b.type(L".");
  b.keys(L"C-r C-r");  // the first y, then the last mark (the cluster's start)
  CHECK_EQ(b.point(), size_t{2});
  t0 = std::chrono::steady_clock::now();
  b.keys(L"C-r");  // skips the other 4094 marks and the e
  t1 = std::chrono::steady_clock::now();
  CHECK_EQ(b.point(), size_t{1});
  const double backward_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  std::printf("  [C4] one repeat of . over a cluster of 4096 units: forward %.1f ms, backward %.1f ms\n", forward_ms,
              backward_ms);
#ifdef NDEBUG
  CHECK(forward_ms < 500.0);
  CHECK(backward_ms < 500.0);
#endif
}

// ---- [C5] C-u inside isearch ----

namespace {

struct Snapshot {
  std::wstring text;
  bool modified = false;
  std::wstring clipboard;
  size_t undo = 0;
  size_t redo = 0;
  bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const Session& s) {
  return Snapshot{s.text(), s.modified(), s.host.clipboard, s.ed.buffer().undo.undo_count(),
                  s.ed.buffer().undo.redo_count()};
}

}  // namespace

TEST(c_u_in_isearch_empties_the_pattern_and_isearch_goes_on) {
  Session s;
  s.load(L"ab foo bar\n");
  s.host.clipboard = L"keep";
  s.set_point(1);
  s.keys(L"C-s");
  s.type(L"foo");
  CHECK_EQ(s.point(), size_t{6});
  const Snapshot before = snapshot(s);
  const uint64_t errors = s.ed.errors_for_testing();
  s.keys(L"C-u");
  CHECK(s.ed.isearch_active());
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring());
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"I-search: "));
  CHECK_EQ(s.point(), size_t{1});  // where this isearch started
  CHECK(snapshot(s) == before);
  CHECK_EQ(s.ed.errors_for_testing(), errors);
  CHECK(!s.ed.search_highlights(40).has_current);
  // b is searched from the start (the b at 1), not from foo (the b at 7).
  s.type(L"b");
  CHECK_EQ(s.point(), size_t{2});
  CHECK(snapshot(s) == before);
}

TEST(c_u_in_isearch_then_del_restores_the_pattern_and_its_match) {
  Session s;
  s.load(L"ab foo bar\n");
  s.keys(L"C-s");
  s.type(L"foo");
  s.keys(L"C-u");
  s.keys(L"DEL");
  CHECK(s.ed.isearch_active());
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring(L"foo"));
  CHECK_EQ(s.point(), size_t{6});
  const geditor::Editor::SearchHighlights h = s.ed.search_highlights(40);
  CHECK(h.has_current);
  CHECK_EQ(h.current.start, size_t{3});
  CHECK_EQ(h.current.end, size_t{6});
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"I-search: foo"));
}

TEST(c_u_in_isearch_on_an_empty_pattern_changes_nothing) {
  Session s;
  s.load(L"ab foo\n");
  s.set_point(2);
  s.keys(L"C-s");
  const uint64_t errors = s.ed.errors_for_testing();
  s.keys(L"C-u");
  CHECK(s.ed.isearch_active());
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"I-search: "));
  CHECK_EQ(s.point(), size_t{2});
  CHECK_EQ(s.ed.errors_for_testing(), errors);
  // Nothing was pushed: DEL has nothing to go back to and isearch stays.
  s.keys(L"DEL");
  CHECK(s.ed.isearch_active());
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring());
  // A macro with C-u on the empty pattern runs all of its repeats (a C-g
  // cannot be recorded: it ends the recording, so RET ends the isearch).
  Session m;
  m.load(L"");
  m.keys(L"C-x (");
  m.keys(L"C-s C-u RET");
  m.type(L"x");
  m.keys(L"C-x )");
  CHECK_EQ(m.text(), std::wstring(L"x"));
  m.keys(L"M-3 C-x e");
  CHECK_EQ(m.text(), std::wstring(L"xxxx"));
}

TEST(c_u_in_isearch_while_failing_or_with_an_invalid_regexp) {
  Session s;
  s.load(L"ab foo\n");
  s.keys(L"C-s");
  s.type(L"foox");
  CHECK(s.ed.echo_area_text().starts_with(L"Failing"));
  s.keys(L"C-u");
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring());
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"I-search: "));
  CHECK_EQ(s.point(), size_t{0});
  s.type(L"foo");
  CHECK_EQ(s.point(), size_t{6});
  Session r;
  r.load(L"ab foo\n");
  r.keys(L"C-M-s");
  r.type(L"[");
  CHECK(r.ed.echo_area_text().find(L"[missing") != std::wstring::npos);
  r.keys(L"C-u");
  CHECK(r.ed.isearch_active());
  CHECK_EQ(r.ed.isearch_pattern(), std::wstring());
  CHECK_EQ(r.ed.echo_area_text(), std::wstring(L"Regexp I-search: "));
}

TEST(c_u_in_isearch_keeps_direction_regexp_and_case_toggle) {
  Session s;
  s.load(L"ab foo ab\n");
  s.set_point(10);
  s.keys(L"C-M-r");
  s.type(L"ab");
  CHECK_EQ(s.point(), size_t{7});
  s.keys(L"C-u");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"Regexp I-search backward: "));
  CHECK_EQ(s.point(), size_t{10});
  Session c;
  c.load(L"ab foo\n");
  c.keys(L"C-s");
  c.type(L"foo");
  c.keys(L"M-c");
  CHECK_EQ(c.ed.echo_area_text(), std::wstring(L"Case-sensitive I-search: foo"));
  c.keys(L"C-u");
  CHECK_EQ(c.ed.echo_area_text(), std::wstring(L"Case-sensitive I-search: "));
  // A wrapped search starts again unwrapped.
  Session w;
  w.load(L"foo foo\n");
  w.keys(L"C-s");
  w.type(L"foo");
  w.keys(L"C-s C-s C-s");
  CHECK(w.ed.echo_area_text().starts_with(L"Wrapped"));
  w.keys(L"C-u");
  CHECK_EQ(w.ed.echo_area_text(), std::wstring(L"I-search: "));
}

TEST(c_u_in_isearch_does_not_keep_the_removed_string) {
  Session s;
  s.load(L"ab foo\n");
  s.keys(L"C-s");
  s.type(L"foo");
  s.keys(L"C-u RET");
  CHECK(!s.ed.isearch_active());
  CHECK_EQ(s.host.clipboard, std::wstring());
  s.keys(L"C-s C-s");  // the last search string: none
  CHECK(s.ed.echo_area_text().find(L"No previous search string") != std::wstring::npos);
  // Outside isearch the command fails as the other isearch commands do.
  Session o;
  o.load(L"abc");
  o.keys(L"M-x");
  o.type(L"isearch-delete-pattern");
  o.keys(L"RET");
  CHECK_EQ(o.message(), std::wstring(L"Not in isearch"));
}

// ---- [C6] C-u inside query-replace ----

namespace {

void start_query_replace(Session& s, std::wstring_view from, std::wstring_view to) {
  s.keys(L"M-%");
  s.type(from);
  s.keys(L"RET");
  s.type(to);
  s.keys(L"RET");
}

}  // namespace

TEST(c_u_in_query_replace_does_nothing) {
  Session s;
  s.load(L"a a a\n");
  s.host.clipboard = L"keep";
  start_query_replace(s, L"a", L"b");
  s.keys(L"y");  // the first; the second is current
  CHECK(s.ed.query_replace_active());
  const Snapshot before = snapshot(s);
  const size_t point = s.point();
  const std::wstring echo = s.ed.echo_area_text();
  const geditor::Editor::SearchHighlights h = s.ed.search_highlights(40);
  const uint64_t errors = s.ed.errors_for_testing();
  s.keys(L"C-u");
  CHECK(s.ed.query_replace_active());
  CHECK(snapshot(s) == before);
  CHECK_EQ(s.point(), point);
  CHECK_EQ(s.ed.echo_area_text(), echo);
  const geditor::Editor::SearchHighlights after = s.ed.search_highlights(40);
  CHECK(after.has_current && h.has_current);
  CHECK(after.current == h.current);
  CHECK_EQ(s.ed.errors_for_testing(), errors);
  s.keys(L"y");
  Session d;
  d.load(L"a a a\n");
  start_query_replace(d, L"a", L"b");
  d.keys(L"y y");
  CHECK_EQ(s.text(), d.text());
  CHECK_EQ(s.text(), std::wstring(L"b b a\n"));
  CHECK_EQ(s.point(), d.point());
}

TEST(c_u_in_query_replace_does_not_stop_a_macro) {
  // Recorded (and run) once: the a at 0 replaced; q leaves point after the
  // next a (3). Each run replaces the next a from there and leaves point
  // after the one after it; a run stopped at C-u would replace nothing and
  // leave query-replace active.
  Session m;
  m.load(L"a a a a a a a\n");
  m.keys(L"C-x (");
  start_query_replace(m, L"a", L"b");
  m.keys(L"C-u y q");
  m.keys(L"C-x )");
  CHECK_EQ(m.text(), std::wstring(L"b a a a a a a\n"));
  CHECK(!m.ed.query_replace_active());
  const uint64_t errors = m.ed.errors_for_testing();
  m.keys(L"M-2 C-x e");
  CHECK_EQ(m.text(), std::wstring(L"b a b a b a a\n"));  // both runs went on after C-u
  CHECK(!m.ed.query_replace_active());
  CHECK_EQ(m.ed.errors_for_testing(), errors);
}

// ---- [C7] C-u elsewhere and the default tables ----

TEST(c_u_in_a_minibuffer_during_query_replace_kills_its_input) {
  Session s;
  s.load(L"a a\n");
  start_query_replace(s, L"a", L"b");
  s.keys(L"M-x");
  s.type(L"forw");
  s.keys(L"C-u");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring());
  CHECK_EQ(s.host.clipboard, std::wstring(L"forw"));
  CHECK_EQ(s.text(), std::wstring(L"a a\n"));
  s.keys(L"C-g");
  CHECK(!s.ed.minibuffer_active());
  CHECK(s.ed.query_replace_active());
}

TEST(c_u_default_bindings_of_2026_11_18) {
  auto command_of = [](std::span<const geditor::DefaultBinding> table) {
    std::string found;
    for (const geditor::DefaultBinding& b : table) {
      if (std::wstring_view(b.keys) == L"C-u") found = b.command;
    }
    return found;
  };
  CHECK_EQ(command_of(geditor::default_global_bindings()), std::string("backward-kill-line"));
  CHECK_EQ(command_of(geditor::default_minibuffer_bindings()), std::string());  // from the global table
  CHECK_EQ(command_of(geditor::default_isearch_bindings()), std::string("isearch-delete-pattern"));
  CHECK_EQ(command_of(geditor::default_query_replace_bindings()), std::string("ignore"));
  CHECK(geditor::find_command("isearch-delete-pattern") != nullptr);
  CHECK(geditor::find_command("ignore") != nullptr);
  // M-x ignore does nothing and is no error.
  Session s;
  s.load(L"abc");
  s.set_point(1);
  const uint64_t errors = s.ed.errors_for_testing();
  s.keys(L"M-x");
  s.type(L"ignore");
  s.keys(L"RET");
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  CHECK_EQ(s.point(), size_t{1});
  CHECK_EQ(s.message(), std::wstring());
  CHECK_EQ(s.ed.errors_for_testing(), errors);
}

TEST(c_u_with_universal_argument_in_keys_inside_isearch_and_query_replace) {
  // README: "C-u": "universal-argument" in `keys` changes the buffer's C-u
  // only; isearch and query-replace keep their own C-u.
  const char settings[] = R"({"keys": {"C-u": "universal-argument"}})";
  Session s;
  s.load(L"ab foo\n");
  s.settings(settings);
  CHECK(s.ed.settings_errors().empty());
  s.keys(L"C-s");
  s.type(L"foo");
  s.keys(L"C-u");
  CHECK(s.ed.isearch_active());
  CHECK_EQ(s.ed.isearch_pattern(), std::wstring());
  Session q;
  q.load(L"a a\n");
  q.settings(settings);
  start_query_replace(q, L"a", L"b");
  q.keys(L"C-u y");
  CHECK_EQ(q.text(), std::wstring(L"b a\n"));
  CHECK(q.ed.query_replace_active());
  // Outside them C-u is the prefix argument (4).
  Session g;
  g.load(L"0123456789");
  g.settings(settings);
  g.keys(L"C-u C-f");
  CHECK_EQ(g.point(), size_t{4});
}
