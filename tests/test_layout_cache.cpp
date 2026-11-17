// Deferred item K1: WrapLayout keeps its line cache (the line start, the
// last row break walked over and the line end) for lines of at least
// kMinCachedLine units, not only for lines of at least kCheckpointInterval.
// The tests compare it with the layout that caches long lines only
// (cache_long_lines_only_for_testing, the walk from the start of every
// shorter line as before): random edit sequences on lines of 1K-70K units
// through the editor's keys (typing, line ends, deletion, kills, yanks,
// undo and redo, motion, scrolling, clicks), with the text, point, mark,
// columns, point_view, the top row, the rows on screen and the scroll bar
// compared after every step, then every undo and redo back and forth; and
// the layout's own queries in random order on such lines after edits.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "display.h"
#include "fake_host.h"
#include "layout.h"
#include "piece_tree.h"

using geditor::PieceTree;
using geditor::RowChar;
using geditor::RowInfo;
using geditor::WrapLayout;
using test::Session;

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

// Words, CJK, TABs, emoji, combining marks, spaces and zero-width clusters
// of their own (U+200B, U+00AD, U+FEFF, U+2060; retry 1: a zero-width
// cluster at the end of a row).
const wchar_t* const kPieces[] = {
    L"abc", L" ", L"editor ", L"\x65E5\x672C\x8A9E", L"\t", L"e\x0301", L"\xD83D\xDE00", L"\x304B\x3099",
    L"\xD83D\xDC68\x200D\xD83D\xDC69", L"0123456789", L"\x3000", L"\xFF83\xFF7D", L"\x200B", L"\x00AD",
    L"\xFEFF", L"\x2060", L"\x200B\x6F22", L"\x0301",
};

std::wstring random_text(Rng& rng, size_t units) {
  std::wstring s;
  while (s.size() < units) s += kPieces[rng.below(std::size(kPieces))];
  return s;
}

struct Compare {
  size_t checks = 0;
  size_t different = 0;
  void expect(bool same, const std::string& what) {
    ++checks;
    if (same) return;
    ++different;
    if (different <= 8) ::test::report_failure(__FILE__, __LINE__, "differs from long-line caches only: " + what);
  }
};

bool same_cells(const geditor::DisplayRow& a, const geditor::DisplayRow& b) {
  if (!(a.pos == b.pos && a.first_row == b.first_row && a.complete == b.complete && a.last_row == b.last_row &&
        a.end_column == b.end_column && a.line_end == b.line_end && a.has_newline == b.has_newline &&
        a.cells.size() == b.cells.size())) {
    return false;
  }
  for (size_t k = 0; k < a.cells.size(); ++k) {
    const geditor::DisplayCell& x = a.cells[k];
    const geditor::DisplayCell& y = b.cells[k];
    if (!(x.c == y.c && x.column == y.column && x.width == y.width && x.offset == y.offset && x.length == y.length &&
          x.code_points == y.code_points && x.emoji == y.emoji && x.mark == y.mark && x.trailing == y.trailing)) {
      return false;
    }
  }
  return true;
}

// The editor state of the cached session `a` against the other one `b`.
void compare_editors(Compare* cmp, Session& a, Session& b, const std::string& name) {
  const geditor::Buffer& x = a.ed.buffer();
  const geditor::Buffer& y = b.ed.buffer();
  cmp->expect(x.text.length() == y.text.length() && a.text() == b.text(), name + " text");
  cmp->expect(x.point == y.point, name + " point " + std::to_string(x.point) + " vs " + std::to_string(y.point));
  cmp->expect(x.has_mark == y.has_mark && (!x.has_mark || x.mark == y.mark) && x.mark_active == y.mark_active,
              name + " mark");
  const geditor::Editor::PointView va = a.ed.point_view();
  const geditor::Editor::PointView vb = b.ed.point_view();
  cmp->expect(va.pos == vb.pos && va.column == vb.column,
              name + " point_view row " + std::to_string(va.pos.row) + " vs " + std::to_string(vb.pos.row));
  cmp->expect(a.ed.point_column() == b.ed.point_column(), name + " point_column");
  cmp->expect(a.ed.mode_line() == b.ed.mode_line(), name + " mode line");
  cmp->expect(a.ed.top() == b.ed.top(), name + " top row");
  cmp->expect(a.message() == b.message(), name + " message");
  const geditor::ScrollBarState sa = a.ed.scroll_bar();
  const geditor::ScrollBarState sb = b.ed.scroll_bar();
  cmp->expect(sa.max == sb.max && sa.page == sb.page && sa.pos == sb.pos && sa.by_offset == sb.by_offset &&
                  sa.unit == sb.unit,
              name + " scroll bar");
  // Positions of clicks on screen: past the end of rows too (offset_at past
  // a row end, retry 1).
  for (const long long r : {0LL, 3LL, 11LL}) {
    for (const int column : {0, 40, 79, 80, 85, 300}) {
      cmp->expect(a.ed.offset_at_screen(r, column) == b.ed.offset_at_screen(r, column),
                  name + " offset_at_screen " + std::to_string(r) + "," + std::to_string(column));
    }
  }
  // The rows on screen, as the window draws them (and one more).
  geditor::RowPos pa = a.ed.top();
  geditor::RowPos pb = b.ed.top();
  geditor::DisplayRow ra;
  geditor::DisplayRow rb;
  for (int r = 0; r <= a.host.lines; ++r) {
    a.ed.display_row(pa, 0, a.ed.text_columns() + 1, &ra);
    b.ed.display_row(pb, 0, b.ed.text_columns() + 1, &rb);
    cmp->expect(same_cells(ra, rb), name + " screen row " + std::to_string(r));
    const bool na = a.ed.next_row(&pa);
    const bool nb = b.ed.next_row(&pb);
    cmp->expect(na == nb && pa == pb, name + " next_row " + std::to_string(r));
    if (!na || !nb) break;
  }
}

// One random step on both sessions: keys, typed text, a click or a scroll.
void random_step(Rng& rng, Session& a, Session& b, std::string* what) {
  static const wchar_t* const kKeys[] = {
      L"RET", L"DEL", L"C-d", L"C-k", L"C-/", L"C-/", L"C-?", L"C-n", L"C-p", L"C-f", L"C-b", L"C-a", L"C-e",
      L"M-f", L"M-b", L"C-v", L"M-v", L"M->", L"M-<", L"C-SPC", L"C-w", L"C-y", L"M-w", L"C-l", L"<end>",
      L"<home>", L"M-3 C-f", L"M-2 C-n", L"S-<right>", L"S-<down>", L"TAB", L"C-x C-x", L"M-d"};
  const size_t kind = rng.below(10);
  if (kind < 4) {
    // Typing: mostly at the end of a line, as the deferred item measures.
    const std::wstring t = kPieces[rng.below(std::size(kPieces))];
    *what = "type";
    a.type(t);
    b.type(t);
  } else if (kind < 8) {
    const wchar_t* k = kKeys[rng.below(std::size(kKeys))];
    *what = "keys " + test::utf8(k);
    // Key notation with spaces is a sequence of keys.
    std::wstring seq = k;
    size_t start = 0;
    while (start <= seq.size()) {
      const size_t sp = seq.find(L' ', start);
      const std::wstring one = seq.substr(start, sp == std::wstring::npos ? std::wstring::npos : sp - start);
      a.keys(one.c_str());
      b.keys(one.c_str());
      if (sp == std::wstring::npos) break;
      start = sp + 1;
    }
  } else if (kind < 9) {
    const size_t offset = rng.below(a.ed.buffer().text.length() + 1);
    *what = "set_point " + std::to_string(offset);
    a.set_point(offset);
    b.set_point(offset);
  } else {
    const long long rows = static_cast<long long>(rng.below(60)) - 30;
    *what = "scroll " + std::to_string(rows);
    a.ed.scroll_by_rows(rows);
    b.ed.scroll_by_rows(rows);
  }
}

}  // namespace

// Random edit sequences on medium and long lines through the editor:
// the same state after every step and through every undo and redo.
TEST(layout_cache_editor_matches_long_line_caches_only) {
  Compare cmp;
  size_t steps = 0;
  size_t undos = 0;
  size_t medium_cached = 0;
  size_t medium_cached_plain = 0;
  Rng rng(0xC4C4E1);
  // Line lengths: around kMinCachedLine, 10K, 40K (the 1K-64K lines that
  // were walked from their start) and one over kCheckpointInterval.
  const size_t lengths[][3] = {{900, 1100, 3000}, {10000, 200, 12000}, {40000, 5, 30}, {70000, 2000, 50}};
  for (const auto& line_lengths : lengths) {
    for (const wchar_t* wrap : {L"max", L"window", L"none"}) {
      std::wstring content;
      for (size_t l = 0; l < 3; ++l) {
        if (l > 0) content += L'\n';
        content += random_text(rng, line_lengths[l]);
      }
      Session a;
      Session b;
      for (Session* s : {&a, &b}) {
        s->host.lines = 12;
        s->load(content);
        s->ed.set_wrap_mode(std::wstring_view(wrap) == L"max"      ? geditor::WrapMode::kMax
                            : std::wstring_view(wrap) == L"window" ? geditor::WrapMode::kWindow
                                                                   : geditor::WrapMode::kNone);
      }
      b.ed.layout().cache_long_lines_only_for_testing(true);
      // Start at the end of the first line (typing at a line end).
      a.set_point(a.ed.buffer().text.line_end(0));
      b.set_point(b.ed.buffer().text.line_end(0));
      const std::string base = "lines " + std::to_string(line_lengths[0]) + " wrap " + test::utf8(wrap);
      compare_editors(&cmp, a, b, base + " start");
      for (int step = 0; step < 250; ++step) {
        std::string what;
        random_step(rng, a, b, &what);
        ++steps;
        compare_editors(&cmp, a, b, base + " step " + std::to_string(step) + " (" + what + ")");
      }
      // The cached session did cache lines below kCheckpointInterval.
      for (size_t line = 0; line < a.ed.buffer().text.line_count(); ++line) {
        const size_t length = a.ed.buffer().text.line_end(line) - a.ed.buffer().text.line_start(line);
        if (length >= WrapLayout::kMinCachedLine && length < WrapLayout::kCheckpointInterval) {
          if (a.ed.layout().checkpoint_count(line) > 0) ++medium_cached;
          if (b.ed.layout().checkpoint_count(line) > 0) ++medium_cached_plain;
        }
      }
      // Every undo back to the file as loaded, then every redo.
      for (int i = 0; i < 400 && a.ed.buffer().undo.undo_count() > 0; ++i) {
        a.keys(L"C-/");
        b.keys(L"C-/");
        ++undos;
        compare_editors(&cmp, a, b, base + " undo " + std::to_string(i));
      }
      cmp.expect(a.text() == content && b.text() == content, base + " undone to the loaded text");
      for (int i = 0; i < 400 && a.ed.buffer().undo.redo_count() > 0; ++i) {
        a.keys(L"C-?");
        b.keys(L"C-?");
        ++undos;
        compare_editors(&cmp, a, b, base + " redo " + std::to_string(i));
      }
    }
  }
  std::printf("  [layout cache] %zu steps, %zu undo/redo, %zu comparisons, %zu different; lines of 1K-64K cached at "
              "the end: %zu (long-line caches only: %zu)\n",
              steps, undos, cmp.checks, cmp.different, medium_cached, medium_cached_plain);
  CHECK(steps >= 3000);
  // (A line of 70K that edits made shorter keeps its cache in both.)
  CHECK(medium_cached > medium_cached_plain + 8);
  CHECK_EQ(cmp.different, size_t{0});
}

// The layout's queries in random order on medium lines (the caches start at
// different places), after insertions and erasures reported by note_edit.
TEST(layout_cache_queries_match_long_line_caches_only) {
  Compare cmp;
  Rng rng(0x1C0DE);
  for (int round = 0; round < 6; ++round) {
    std::wstring text;
    for (size_t l = 0; l < 12; ++l) {
      if (l > 0) text += L'\n';
      text += random_text(rng, l % 3 == 0 ? rng.below(300) : 1000 + rng.below(20000));
    }
    for (const int width : {0, 1, 7, 80, 117}) {
      PieceTree tree(text);
      WrapLayout cached;
      WrapLayout plain;
      cached.configure(width, 4);
      plain.configure(width, 4);
      plain.cache_long_lines_only_for_testing(true);
      std::vector<RowChar> ca;
      std::vector<RowChar> cb;
      for (int q = 0; q < 1500; ++q) {
        const std::string name = "round " + std::to_string(round) + " width " + std::to_string(width) + " query " +
                                 std::to_string(q);
        const size_t line = rng.below(tree.line_count());
        const size_t rows = plain.row_count(tree, line);
        switch (rng.below(8)) {
          case 0: {
            const size_t offset = rng.below(tree.length() + 1);
            int c1 = 0;
            int c2 = 0;
            int64_t l1 = 0;
            int64_t l2 = 0;
            const geditor::RowPos p1 = cached.row_of(tree, offset, &c1, &l1);
            const geditor::RowPos p2 = plain.row_of(tree, offset, &c2, &l2);
            cmp.expect(p1 == p2 && c1 == c2 && l1 == l2, name + " row_of");
            break;
          }
          case 1:
            cmp.expect(cached.row_count(tree, line) == rows, name + " row_count");
            break;
          case 2: {
            const size_t row = rng.below(rows + 2);
            cmp.expect(cached.has_row(tree, line, row) == plain.has_row(tree, line, row), name + " has_row");
            break;
          }
          case 3: {
            const size_t row = rng.below(rows + 1);
            cmp.expect(cached.row_start(tree, line, row) == plain.row_start(tree, line, row), name + " row_start");
            break;
          }
          case 4: {
            const size_t row = rng.below(rows + 1);
            const int column = static_cast<int>(rng.below(140));
            cmp.expect(cached.offset_at(tree, line, row, column) == plain.offset_at(tree, line, row, column),
                       name + " offset_at");
            break;
          }
          case 5: {
            const size_t row = rng.below(rows);
            const int from = static_cast<int>(rng.below(20));
            const RowInfo i1 = cached.row_chars(tree, line, row, from, 1 << 20, &ca);
            const RowInfo i2 = plain.row_chars(tree, line, row, from, 1 << 20, &cb);
            bool same = ca.size() == cb.size() && i1.complete == i2.complete && i1.last == i2.last &&
                        i1.end_column == i2.end_column && i1.end == i2.end;
            for (size_t k = 0; same && k < ca.size(); ++k) {
              same = ca[k].offset == cb[k].offset && ca[k].column == cb[k].column && ca[k].width == cb[k].width &&
                     ca[k].length == cb[k].length;
            }
            cmp.expect(same, name + " row_chars");
            break;
          }
          case 6: {
            geditor::RowPos p1{line, rng.below(rows)};
            geditor::RowPos p2 = p1;
            const size_t n = rng.below(200);
            const bool forward = rng.below(2) == 0;
            const size_t m1 = forward ? cached.forward_rows(tree, &p1, n) : cached.backward_rows(tree, &p1, n);
            const size_t m2 = forward ? plain.forward_rows(tree, &p2, n) : plain.backward_rows(tree, &p2, n);
            cmp.expect(m1 == m2 && p1 == p2, name + " forward/backward_rows");
            break;
          }
          default: {
            // An edit inside a line (or across a line end), as the editor
            // reports it.
            const size_t at = rng.below(tree.length() + 1);
            if (rng.below(2) == 0 || tree.length() == at) {
              const std::wstring t = rng.below(5) == 0 ? std::wstring(L"x\ny") : random_text(rng, 1 + rng.below(6));
              tree.insert(at, t);
              cached.note_edit(tree, at, t.size(), 0);
              plain.note_edit(tree, at, t.size(), 0);
            } else {
              const size_t n = std::min<size_t>(1 + rng.below(8), tree.length() - at);
              tree.erase(at, n);
              cached.note_edit(tree, at, 0, n);
              plain.note_edit(tree, at, 0, n);
            }
            ++cmp.checks;
            break;
          }
        }
      }
    }
  }
  std::printf("  [layout cache] %zu layout queries and edits compared, %zu different\n", cmp.checks, cmp.different);
  CHECK_EQ(cmp.different, size_t{0});
}

// Retry 1 of K1: a walk that passes a row break records it, and a later
// query of that row past its end or from its end column starts there. The
// answers must be those of a walk from the line start (a fresh layout,
// which has no state but the line start), for every line length (500 to
// 70000 units, with and without the recorded states of 64K units), on rows
// that end in zero-width clusters of their own (U+200B, U+00AD, U+FEFF,
// U+2060) and rows that start with a cluster after a combining mark.
TEST(layout_cache_queries_after_a_row_break_match_a_walk_from_the_line_start) {
  Compare cmp;
  Rng rng(0x2E80);
  const wchar_t* const kZero[] = {L"\x200B", L"\x00AD", L"\xFEFF", L"\x2060"};
  size_t zero_width_row_ends = 0;
  for (const size_t length : {size_t{500}, size_t{1500}, size_t{20000}, size_t{70000}}) {
    for (const int width : {7, 80, 117}) {
      std::wstring line;
      while (line.size() < length) {
        line += random_text(rng, 1 + rng.below(30));
        if (rng.below(3) == 0) {
          // Zero-width clusters before a wide character: often the end of a row.
          line += kZero[rng.below(std::size(kZero))];
          if (rng.below(2) == 0) line += kZero[rng.below(std::size(kZero))];
          line += L"\x6F22";
        }
        if (rng.below(8) == 0) line += L"a\x0301";
      }
      const PieceTree tree(line + L"\nnext");
      WrapLayout cached;
      cached.configure(width, 4);
      const size_t rows = cached.row_count(tree, 0);
      const size_t step = rows / 250 + 1;
      for (size_t row = 0; row + 1 < rows; row += step) {
        const std::string name = "length " + std::to_string(length) + " width " + std::to_string(width) + " row " +
                                 std::to_string(row);
        // The end of the row, from a fresh layout.
        std::vector<RowChar> chars;
        WrapLayout fresh_row;
        fresh_row.configure(width, 4);
        const RowInfo info = fresh_row.row_chars(tree, 0, row, 0, 1 << 20, &chars);
        if (!chars.empty() && chars.back().width == 0) ++zero_width_row_ends;
        // A walk that stops just past the break after the row records it.
        if (row % 2 == 0) {
          cached.row_of(tree, info.end, nullptr);
        } else {
          cached.has_row(tree, 0, row + 1);
          cached.row_start(tree, 0, row + 1);
        }
        for (const int column : {info.end_column - 1, info.end_column, info.end_column + 1, width + 5, 1 << 20}) {
          WrapLayout fresh;
          fresh.configure(width, 4);
          const size_t want = fresh.offset_at(tree, 0, row, column);
          const size_t got = cached.offset_at(tree, 0, row, column);
          cmp.expect(got == want, name + " offset_at column " + std::to_string(column) + ": " + std::to_string(got) +
                                      " (from the line start " + std::to_string(want) + ")");
          std::vector<RowChar> a;
          std::vector<RowChar> b;
          const RowInfo ia = cached.row_chars(tree, 0, row, column, 1 << 20, &a);
          WrapLayout fresh2;
          fresh2.configure(width, 4);
          const RowInfo ib = fresh2.row_chars(tree, 0, row, column, 1 << 20, &b);
          bool same = a.size() == b.size() && ia.end == ib.end && ia.end_column == ib.end_column && ia.last == ib.last;
          for (size_t k = 0; same && k < a.size(); ++k) same = a[k].offset == b[k].offset && a[k].column == b[k].column;
          cmp.expect(same, name + " row_chars from column " + std::to_string(column));
        }
      }
    }
  }
  std::printf("  [layout cache] queries after a row break: %zu compared, %zu different (%zu rows ended in a zero-width "
              "cluster)\n",
              cmp.checks, cmp.different, zero_width_row_ends);
  CHECK(zero_width_row_ends > 50);
  CHECK_EQ(cmp.different, size_t{0});
}

// Retry 1 of K1, the reviewer's case: row 0 is 79 'a' and U+200B (a wide
// character breaks to row 1 at the fixed width 80). From column 79 of row 1,
// C-p / <up>, and a click past the end of row 0, land on the last 'a'
// (offset 78), as on a short line before K1, for lines of 500, 1500 and
// 70000 units (since retry 1 the line length no longer matters), and with
// U+00AD, U+FEFF and U+2060 in place of U+200B.
TEST(layout_cache_vertical_motion_and_click_past_a_zero_width_row_end) {
  size_t cases = 0;
  size_t wrong = 0;
  for (const wchar_t zero : {L'\x200B', L'\x00AD', L'\xFEFF', L'\x2060'}) {
    for (const size_t length : {size_t{500}, size_t{1500}, size_t{70000}}) {
      for (int variant = 0; variant < 4; ++variant) {
        Session s;
        s.host.lines = 10;
        s.host.columns = 120;
        s.settings("{\"wrap\": {\"mode\": \"fixed\", \"column\": 80}, \"line_numbers\": false}");
        std::wstring line(79, L'a');
        line += zero;
        line += L'\x6F22';
        while (line.size() < length) line += L'b';
        s.load(line + L"\nnext line\n");
        // Point on row 1, column 79 (after the wide character and 77 'b').
        s.set_point(81 + 77);
        const geditor::Editor::PointView start = s.ed.point_view();
        size_t got = 0;
        if (variant == 0) {
          s.keys(L"C-p");
          got = s.point();
        } else if (variant == 1) {
          s.keys(L"<up>");
          got = s.point();
        } else if (variant == 2) {
          got = s.ed.offset_at_screen(0, 85);
        } else {
          s.ed.mouse_press(s.ed.offset_at_click(0, 85), false);
          s.ed.mouse_release();
          got = s.point();
        }
        ++cases;
        if (got != 78 || start.pos.row != 1 || start.column != 79) {
          ++wrong;
          if (wrong <= 8) {
            char code[8];
            std::snprintf(code, sizeof(code), "%04X", static_cast<unsigned>(zero));
            ::test::report_failure(__FILE__, __LINE__,
                                   std::string("U+") + code + " length " +
                                       std::to_string(length) + " variant " + std::to_string(variant) + ": offset " +
                                       std::to_string(got) + " (want 78), start row " + std::to_string(start.pos.row) +
                                       " column " + std::to_string(start.column));
          }
        }
      }
    }
  }
  std::printf("  [layout cache] zero-width row end, C-p / <up> / click: %zu cases, %zu wrong\n", cases, wrong);
  CHECK_EQ(cases, size_t{48});
  CHECK_EQ(wrong, size_t{0});
}
