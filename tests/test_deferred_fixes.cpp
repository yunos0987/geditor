// The deferred fixes after M6 (2026-11-17): query-replace as one undo unit
// after undos (S2) and its count (S3), point after a replacement inside a
// grapheme cluster (S4), a settings reload of search.case_fold during
// isearch (S5), C-s / C-r during query-replace (S6), the default line end
// after a save that rewrites LF after CR (F1), redo of a joining insertion
// (E1) and the one whitespace predicate of the trailing-whitespace marks
// (E3). Each test fails on HEAD 6064002.

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "display.h"
#include "fake_host.h"
#include "grapheme.h"
#include "layout.h"
#include "piece_tree.h"
#include "search.h"

using geditor::SearchMatch;
using test::Session;

namespace {

// "M-% FROM RET TO RET".
void start_query_replace(Session& s, std::wstring_view from, std::wstring_view to) {
  s.keys(L"M-%");
  s.type(from);
  s.keys(L"RET");
  s.type(to);
  s.keys(L"RET");
}

}  // namespace

// ---- S2: one undo unit after undos inside the run ----

TEST(deferred_s2_query_replace_stays_one_undo_unit_after_undoing_older_history) {
  Session s;
  s.load(L"a a a\n");
  s.type(L"Z");  // an edit before query-replace (its own undo group)
  CHECK_EQ(s.text(), std::wstring(L"Za a a\n"));
  start_query_replace(s, L"a", L"b");
  CHECK(s.ed.query_replace_active());
  s.keys(L"y");
  CHECK_EQ(s.text(), std::wstring(L"Zb a a\n"));
  // C-/ (through the global keymap) undoes that replacement and then the
  // older "Z": undo goes past where query-replace started.
  s.keys(L"C-/");
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"a a a\n"));
  CHECK(s.ed.query_replace_active());
  s.keys(L"y");
  s.keys(L"y");
  s.keys(L"q");
  CHECK_EQ(s.text(), std::wstring(L"b b a\n"));
  CHECK(!s.ed.query_replace_active());
  // The two replacements that stand are one unit (HEAD 6064002: two units,
  // because the start index no longer matched the history).
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"a a a\n"));
  // Nothing older was merged into it: the undone "Z" stays undone and there
  // is nothing more to undo.
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"a a a\n"));
  CHECK_EQ(s.message(), std::wstring(L"No further undo information"));
}

TEST(deferred_s2_an_older_edit_is_not_merged_after_undo_and_redo) {
  Session s;
  s.load(L"a a\n");
  s.type(L"Z");
  start_query_replace(s, L"a", L"b");
  s.keys(L"C-/");  // undoes "Z" (before the first replacement)
  s.keys(L"C-?");  // and redoes it: an older edit, back on the history
  CHECK_EQ(s.text(), std::wstring(L"Za a\n"));
  s.keys(L"!");
  CHECK_EQ(s.text(), std::wstring(L"Zb b\n"));
  CHECK_EQ(s.message(), std::wstring(L"Replaced 2 occurrences"));
  s.keys(L"C-/");  // the replacements only
  CHECK_EQ(s.text(), std::wstring(L"Za a\n"));
  s.keys(L"C-/");  // then the "Z"
  CHECK_EQ(s.text(), std::wstring(L"a a\n"));
}

// ---- S3: the count of the replacements that stand ----

TEST(deferred_s3_replaced_count_leaves_out_undone_replacements) {
  Session s;
  s.load(L"a a a a\n");
  start_query_replace(s, L"a", L"b");
  s.keys(L"y");
  s.keys(L"y");
  CHECK_EQ(s.text(), std::wstring(L"b b a a\n"));
  s.keys(L"C-/");  // the second replacement is undone
  CHECK_EQ(s.text(), std::wstring(L"b a a a\n"));
  s.keys(L"y");
  s.keys(L"y");
  CHECK(!s.ed.query_replace_active());
  CHECK_EQ(s.text(), std::wstring(L"b a b b\n"));
  // Three stand (HEAD 6064002 said 4).
  CHECK_EQ(s.message(), std::wstring(L"Replaced 3 occurrences"));
}

TEST(deferred_s3_count_after_undo_and_redo_and_bang) {
  Session s;
  s.load(L"a a a a\na a\n");
  start_query_replace(s, L"a", L"b");
  s.keys(L"y");
  s.keys(L"C-/");
  s.keys(L"C-?");  // redone: it stands again
  CHECK_EQ(s.text(), std::wstring(L"b a a a\na a\n"));
  // After each undo / redo query-replace goes on from point: the next
  // match is the "a" after the redone "b " (the one at 2 is behind point).
  s.keys(L"!");
  CHECK_EQ(s.text(), std::wstring(L"b a b b\nb b\n"));
  CHECK_EQ(s.message(), std::wstring(L"Replaced 5 occurrences"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"a a a a\na a\n"));
}

// ---- S4: point after a replacement inside a cluster ----

TEST(deferred_s4_point_ends_on_a_cluster_boundary_after_the_replaced_text) {
  // "e" + U+0301 is one cluster; "e" matches inside it.
  Session s;
  s.load(L"e\x0301x e\x0301\n");
  start_query_replace(s, L"e", L"o");
  CHECK(s.ed.query_replace_active());
  // The highlight is widened to the cluster; the match is the "e" only.
  const geditor::Editor::SearchHighlights h = s.ed.search_highlights(10);
  CHECK(h.has_current);
  CHECK(h.current == (SearchMatch{0, 2}));
  s.keys(L".");
  // The edit replaced exactly the matched unit.
  CHECK_EQ(s.text(), std::wstring(L"o\x0301x e\x0301\n"));
  CHECK(!s.ed.query_replace_active());
  // Point is after the cluster that holds the replacement (HEAD 6064002: 0,
  // before it).
  CHECK_EQ(s.point(), size_t{2});

  Session t;
  t.load(L"e\x0301x e\x0301\n");
  start_query_replace(t, L"e", L"o");
  t.keys(L"y");
  CHECK_EQ(t.point(), size_t{6});  // the end of the next match's cluster
  t.keys(L"y");                    // the last match: query-replace ends
  CHECK(!t.ed.query_replace_active());
  CHECK_EQ(t.text(), std::wstring(L"o\x0301x o\x0301\n"));
  CHECK_EQ(t.point(), size_t{6});  // HEAD 6064002: 4

  Session u;
  u.load(L"e\x0301x e\x0301\n");
  start_query_replace(u, L"e", L"o");
  u.keys(L"!");
  CHECK_EQ(u.text(), std::wstring(L"o\x0301x o\x0301\n"));
  CHECK_EQ(u.point(), size_t{6});  // HEAD 6064002: 4
  CHECK_EQ(u.message(), std::wstring(L"Replaced 2 occurrences"));
}

// ---- S5: a settings reload of search.case_fold during isearch ----

TEST(deferred_s5_settings_reload_searches_again_with_the_new_case_fold) {
  Session s;
  s.load(L"FOO\n");
  s.settings(R"({"search": {"case_fold": "never"}})");
  s.keys(L"C-s");
  s.type(L"foo");
  CHECK(s.ed.isearch_active());
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"Failing I-search: foo"));
  CHECK(!s.ed.search_highlights(10).has_current);
  s.settings(R"({"search": {"case_fold": "always"}})");
  CHECK(s.ed.isearch_active());
  // At once: the match, the point, the highlights and the prompt (HEAD
  // 6064002 kept "Failing" and no highlight until the next key).
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"I-search: foo"));
  CHECK_EQ(s.point(), size_t{3});
  const geditor::Editor::SearchHighlights h = s.ed.search_highlights(10);
  CHECK(h.has_current);
  CHECK(h.current == (SearchMatch{0, 3}));
  CHECK_EQ(h.matches.size(), size_t{1});
  // Back to "never": failing again, no highlight.
  s.settings(R"({"search": {"case_fold": "never"}})");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"Failing I-search: foo"));
  CHECK(s.ed.search_highlights(10).matches.empty());
  // M-c overrides the setting: a reload leaves it alone.
  s.keys(L"M-c");
  const std::wstring overridden = s.ed.echo_area_text();
  s.settings(R"({"search": {"case_fold": "always"}})");
  CHECK_EQ(s.ed.echo_area_text(), overridden);
}

// ---- S6: C-s / C-r during query-replace ----

TEST(deferred_s6_c_s_during_query_replace_says_why_and_query_replace_goes_on) {
  Session s;
  s.load(L"a a\n");
  start_query_replace(s, L"a", L"b");
  s.keys(L"C-s");
  CHECK(s.ed.query_replace_active());
  CHECK(!s.ed.isearch_active());
  CHECK_EQ(s.ed.echo_area_text(),
           std::wstring(L"Query replacing a with b: [I-search cannot be started during query-replace]"));
  s.keys(L"C-r");
  CHECK(s.ed.query_replace_active());
  CHECK(s.message().find(L"minibuffer") == std::wstring::npos);
  s.keys(L"y");
  CHECK_EQ(s.text(), std::wstring(L"b a\n"));
  CHECK(s.ed.query_replace_active());
}

// ---- F1: the default line end after a save that keeps a CR ----

TEST(deferred_f1_default_line_end_after_the_cr_rewrite_is_the_one_of_reopening) {
  Session s;
  s.host.add_file(L"C:\\work\\t.txt", "a\rx\ny\nb\r\n");  // CR, LF, LF, CRLF: the default is LF
  s.ed.request_open(L"C:\\work\\t.txt");
  CHECK_EQ(s.text(), std::wstring(L"a\nx\ny\nb\n"));
  CHECK_EQ(s.ed.eol_text(), std::wstring(L"Mixed(LF)"));
  s.set_point(2);
  s.keys(L"C-d C-f C-d");  // "a\r\r\rb\r\n" when saved: 3 CR, 1 CRLF
  s.keys(L"C-x C-s");
  const std::string saved = s.host.file(L"C:\\work\\t.txt");
  CHECK_EQ(saved, std::string("a\r\r\rb\r\n"));
  Session reopened;
  reopened.host.add_file(L"C:\\work\\t.txt", saved);
  reopened.ed.request_open(L"C:\\work\\t.txt");
  CHECK_EQ(reopened.text(), s.text());
  // HEAD 6064002 kept Mixed(LF) until the file was opened again.
  CHECK_EQ(reopened.ed.eol_text(), std::wstring(L"Mixed(CR)"));
  CHECK_EQ(s.ed.eol_text(), reopened.ed.eol_text());
  CHECK(s.ed.buffer().format.eol == reopened.ed.buffer().format.eol);
  // A new line end is the default one, as in the reopened buffer.
  const size_t cr = s.ed.buffer().text.eol_count(geditor::Eol::kCr);
  s.keys(L"M-> RET");
  CHECK_EQ(s.ed.buffer().text.eol_count(geditor::Eol::kCr), cr + 1);
}

// ---- E1: redo of a joining insertion ----

TEST(deferred_e1_redo_of_a_joining_insertion_puts_point_after_the_cluster) {
  // A ZWJ typed between two emoji joins them into one cluster; point goes
  // to its end (decision G4). Redo puts it there again.
  Session s;
  s.load(L"\xD83D\xDC68\xD83D\xDC69\n");  // U+1F468 U+1F469
  s.set_point(2);
  s.ed.insert_text(L"\x200D");
  CHECK_EQ(s.text(), std::wstring(L"\xD83D\xDC68\x200D\xD83D\xDC69\n"));
  CHECK_EQ(s.point(), size_t{5});
  s.keys(L"C-/");
  CHECK_EQ(s.point(), size_t{2});
  s.keys(L"C-?");
  CHECK_EQ(s.text(), std::wstring(L"\xD83D\xDC68\x200D\xD83D\xDC69\n"));
  CHECK_EQ(s.point(), size_t{5});  // HEAD 6064002: 0, before the family
  s.type(L"x");
  CHECK_EQ(s.text(), std::wstring(L"\xD83D\xDC68\x200D\xD83D\xDC69x\n"));
}

TEST(deferred_e1_redo_of_a_yank_that_joins_the_text_after_it) {
  // A yank that ends with U+0600 (a Prepend character) joins the "a" after
  // it into one cluster. Redo puts point where the yank left it.
  Session s;
  s.load(L"a\n");
  s.host.clipboard = L"x\x0600";
  s.host.clipboard_has_text = true;
  s.keys(L"C-y");
  CHECK_EQ(s.text(), std::wstring(L"x\x0600" L"a\n"));
  const size_t after_yank = s.point();
  CHECK_EQ(geditor::cluster_start(s.ed.buffer().text, after_yank), after_yank);
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"a\n"));
  s.keys(L"C-?");
  CHECK_EQ(s.text(), std::wstring(L"x\x0600" L"a\n"));
  CHECK_EQ(s.point(), after_yank);
}

// ---- E3: one whitespace predicate ----

TEST(deferred_e3_one_whitespace_predicate_for_the_trailing_marks) {
  // Space, TAB and U+3000, and nothing else of the BMP and the planes above.
  size_t count = 0;
  for (char32_t c = 0; c < 0x110000; ++c) {
    if (geditor::is_trailing_whitespace(c)) ++count;
  }
  CHECK_EQ(count, size_t{3});
  CHECK(geditor::is_trailing_whitespace(U' '));
  CHECK(geditor::is_trailing_whitespace(U'\t'));
  CHECK(geditor::is_trailing_whitespace(U'\x3000'));
}

namespace {

struct Rng {
  uint64_t state;
  explicit Rng(uint64_t seed) : state(seed) {}
  uint64_t next() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }
  size_t below(size_t n) { return static_cast<size_t>(next() % n); }
};

}  // namespace

// The differential fuzz of WrapLayout::trailing_start (kept per line and
// updated by edits) against a full scan of the line
// (trailing_whitespace_start), after random insertions, deletions, undos and
// redos of whitespace, text, line feeds and clusters.
TEST(deferred_e3_trailing_start_fuzz_matches_a_full_scan) {
  static const wchar_t* const kPieces[] = {L" ",     L"\t",  L"\x3000", L"a",       L"  ",       L"\n",
                                           L"e\x0301", L"\x0600", L" \x0301", L"\xD83D\xDE00", L"\x3000 \t",
                                           L"xy",    L"\x00A0", L"\x2003"};
  Rng rng(0xE3E3);
  size_t steps = 0;
  size_t compared = 0;
  size_t different = 0;
  for (int round = 0; round < 60; ++round) {
    Session s;
    s.load(L"ab  \t \x3000\nxy  z   \n   \nq");
    const geditor::PieceTree& t = s.ed.buffer().text;
    for (int step = 0; step < 40; ++step) {
      const size_t op = rng.below(10);
      if (op < 5) {
        s.set_point(rng.below(t.length() + 1));
        std::wstring text;
        const size_t n = rng.below(3) + 1;
        for (size_t i = 0; i < n; ++i) text += kPieces[rng.below(std::size(kPieces))];
        s.ed.insert_text(text);
      } else if (op < 8) {
        s.set_point(rng.below(t.length() + 1));
        const size_t n = rng.below(4) + 1;
        for (size_t i = 0; i < n; ++i) s.keys(rng.below(2) == 0 ? L"C-d" : L"DEL");
      } else if (op == 8) {
        s.keys(L"C-/");
      } else {
        s.keys(L"C-?");
      }
      ++steps;
      for (size_t line = 0; line < t.line_count(); ++line) {
        ++compared;
        const size_t cached = s.ed.layout().trailing_start(t, line);
        const size_t full = geditor::trailing_whitespace_start(t, line);
        if (cached != full) {
          ++different;
          if (different <= 5) {
            ::test::report_failure(__FILE__, __LINE__,
                                   "trailing_start differs on line " + std::to_string(line) + ": " +
                                       std::to_string(cached) + " vs " + std::to_string(full) + " in " +
                                       ::test::to_display(t.text()));
          }
        }
      }
    }
  }
  std::printf("  [trailing fuzz] %zu edits, %zu lines compared, %zu different\n", steps, compared, different);
  CHECK_EQ(different, size_t{0});
}

// ---- Retry 1 (code review of the deferred fixes) ----

// R2: a macro call whose events undo history older than the call is still
// one undo unit, and no older edit joins it.
TEST(deferred_r2_macro_call_that_undoes_older_history_is_one_undo_unit) {
  Session s;
  s.load(L"abc\n");
  s.keys(L"C-e");
  s.type(L"p");
  s.keys(L"C-x (");
  s.keys(L"C-/");
  s.keys(L"C-o C-o");
  s.keys(L"C-x )");
  CHECK_EQ(s.text(), std::wstring(L"abc\n\n\n"));
  s.keys(L"C-e");
  s.type(L"q");
  CHECK_EQ(s.text(), std::wstring(L"abcq\n\n\n"));
  s.keys(L"C-x e");  // undoes the "q" (older than the call), then two C-o
  CHECK_EQ(s.text(), std::wstring(L"abc\n\n\n\n\n"));
  s.keys(L"C-/");  // the call: both line feeds (it split into two units)
  CHECK_EQ(s.text(), std::wstring(L"abc\n\n\n"));
  s.keys(L"C-/");  // the older C-o of the recording, on its own
  CHECK_EQ(s.text(), std::wstring(L"abc\n\n"));
}

// R3: a macro that ends with query-replace still open. The call (with the
// replacement it made) is one undo unit; the replacements made after the
// call are another; the count covers both.
TEST(deferred_r3_macro_ending_inside_query_replace_keeps_its_own_undo_unit) {
  Session s;
  s.load(L"a a a a\n");
  s.keys(L"C-x (");
  s.type(L"x");
  start_query_replace(s, L"a", L"b");
  s.keys(L"y");
  s.keys(L"C-x )");
  s.keys(L"q");
  CHECK_EQ(s.text(), std::wstring(L"xb a a a\n"));
  s.keys(L"C-a");
  s.keys(L"C-x e");
  CHECK(s.ed.query_replace_active());
  CHECK_EQ(s.text(), std::wstring(L"xxb b a a\n"));
  s.keys(L"y");
  s.keys(L"q");
  CHECK_EQ(s.text(), std::wstring(L"xxb b b a\n"));
  CHECK_EQ(s.message(), std::wstring(L"Replaced 2 occurrences"));
  s.keys(L"C-/");  // the replacement after the call
  CHECK_EQ(s.text(), std::wstring(L"xxb b a a\n"));
  s.keys(L"C-/");  // the call
  CHECK_EQ(s.text(), std::wstring(L"xb a a a\n"));
}

// Q5 (what AGENTS.md says of R3): a macro called inside a running
// query-replace. The replacements made before the call stay one undo unit
// each; the call is one; the replacements after it are one.
TEST(deferred_q5_macro_called_inside_query_replace_undo_units) {
  Session s;
  s.load(L"a a a a a\n");
  s.keys(L"M->");
  s.keys(L"C-x (");
  s.keys(L"C-o");  // open-line: no query-replace key, so it runs inside it
  s.keys(L"C-x )");
  CHECK_EQ(s.text(), std::wstring(L"a a a a a\n\n"));
  s.keys(L"M-<");
  start_query_replace(s, L"a", L"b");
  s.keys(L"y");
  s.keys(L"y");
  s.keys(L"C-x e");  // a line feed at point: after the third "a" (the current match)
  CHECK(s.ed.query_replace_active());
  s.keys(L"y");
  s.keys(L"q");
  CHECK_EQ(s.text(), std::wstring(L"b b a\n b a\n\n"));
  CHECK_EQ(s.message(), std::wstring(L"Replaced 3 occurrences"));
  const std::wstring after[] = {L"b b a\n a a\n\n", L"b b a a a\n\n", L"b a a a a\n\n", L"a a a a a\n\n"};
  for (const std::wstring& want : after) {
    s.keys(L"C-/");
    CHECK_EQ(s.text(), want);
  }
}

// R4: after a reload that changes search.case_fold during isearch, the
// states that DEL goes back to follow the new rule too.
TEST(deferred_r4_del_after_a_case_fold_reload_follows_the_new_rule) {
  Session s;
  s.load(L"FOO\n");
  s.settings(R"({"search": {"case_fold": "never"}})");
  s.keys(L"C-s");
  s.type(L"foo");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"Failing I-search: foo"));
  s.settings(R"({"search": {"case_fold": "always"}})");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"I-search: foo"));
  s.keys(L"DEL");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"I-search: fo"));
  CHECK_EQ(s.point(), size_t{2});
  geditor::Editor::SearchHighlights h = s.ed.search_highlights(10);
  CHECK(h.has_current);
  CHECK(h.current == (SearchMatch{0, 2}));
  s.keys(L"DEL");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"I-search: f"));
  CHECK_EQ(s.point(), size_t{1});
  // And back to "never": every step fails again.
  s.settings(R"({"search": {"case_fold": "never"}})");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"Failing I-search: f"));
  s.type(L"o");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"Failing I-search: fo"));
  s.keys(L"DEL");
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"Failing I-search: f"));
}
