// The run of lone clusters in WrapLayout::walk (the deferred performance
// work before M6): printable ASCII and the BMP characters without a
// Grapheme_Cluster_Break value or emoji property are placed without the
// general cluster walk when the walk's caller only passes over them. The
// tests compare a layout with that run against one that walks every cluster
// through for_each_cluster (use_general_walk_for_testing), on long lines of
// ASCII, CJK, emoji (ZWJ, flags, skin tones), combining marks, Prepend
// characters, Hangul jamo, lone surrogates and TABs, in every wrap mode's
// width: rows and their clusters, break positions, cell columns, recorded
// states, positions and goal columns, before and after edits.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "grapheme.h"
#include "layout.h"
#include "piece_tree.h"
#include "test_harness.h"

using geditor::Cluster;
using geditor::GraphemeBreaker;
using geditor::PieceTree;
using geditor::RowChar;
using geditor::RowInfo;
using geditor::WrapLayout;

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

// Text pieces; `plain` gives long runs of lone clusters, the rest the
// clusters that the general walk must take.
std::wstring random_line(Rng& rng, size_t units, int plain_percent) {
  static const wchar_t* const kPlain[] = {L"abc", L" ", L"editor ", L"\x65E5\x672C\x8A9E", L"\x30C6\x30AD",
                                          L"\x304B\x306A", L"0123", L"\xFF83\xFF7D", L"\x00E9", L"\x4E00"};
  static const wchar_t* const kOther[] = {
      L"\t",
      L"e\x0301",                                       // a combining mark
      L"\xD83D\xDE00",                                  // U+1F600
      L"\xD83D\xDC68\x200D\xD83D\xDC69\x200D\xD83D\xDC67",  // ZWJ family
      L"\xD83C\xDDEF\xD83C\xDDF5",                      // a flag
      L"\xD83D\xDC4D\xD83C\xDFFD",                      // a skin tone
      L"\x2639\xFE0F",                                  // U+FE0F
      L"\x0600\x0031",                                  // a Prepend character and "1"
      L"\x1100\x1161\x11A8",                            // Hangul jamo
      L"\xD800",                                        // a lone high surrogate
      L"\xDC00",                                        // a lone low surrogate
      L"\x200B",                                        // zero width space
      L"\x0915\x094D\x0937",                            // a conjunct (GB9c)
      L"\x304B\x3099",                                  // ka + combining dakuten
      L"\xD83C\xDDEF",                                  // a lone regional indicator
      L"\x00AD",                                        // soft hyphen
      L"\x3000",                                        // ideographic space
  };
  std::wstring s;
  while (s.size() < units) {
    if (static_cast<int>(rng.below(100)) < plain_percent) {
      s += kPlain[rng.below(std::size(kPlain))];
    } else {
      s += kOther[rng.below(std::size(kOther))];
    }
  }
  return s;
}

std::string row_char_text(const RowChar& c) {
  return std::to_string(c.offset) + ":" + std::to_string(c.column) + "/" + std::to_string(c.width) + "/" +
         std::to_string(c.length);
}

struct Compare {
  size_t checks = 0;
  size_t different = 0;
  void expect(bool same, const std::string& what) {
    ++checks;
    if (same) return;
    ++different;
    if (different <= 8) ::test::report_failure(__FILE__, __LINE__, "differs from the general walk: " + what);
  }
};

// Compares every row of every line (row_chars as drawing reads them, in
// order), row_count, row_start, the recorded states, and row_of / has_row /
// offset_at at random places.
void compare_layouts(Compare* cmp, Rng& rng, const PieceTree& tree, const WrapLayout& fast, const WrapLayout& general,
                     const std::string& name) {
  std::vector<RowChar> a;
  std::vector<RowChar> b;
  for (size_t line = 0; line < tree.line_count(); ++line) {
    // Random queries first (from line start states, before the rows are
    // walked in order), then the rows in order.
    const size_t start = tree.line_start(line);
    const size_t end = tree.line_end(line);
    for (int q = 0; q < 40; ++q) {
      const size_t offset = start + rng.below(end - start + 1);
      int ca = 0;
      int cb = 0;
      int64_t la = 0;
      int64_t lb = 0;
      const geditor::RowPos pa = fast.row_of(tree, offset, &ca, &la);
      const geditor::RowPos pb = general.row_of(tree, offset, &cb, &lb);
      cmp->expect(pa == pb && ca == cb && la == lb, name + " row_of " + std::to_string(offset));
      const size_t row = rng.below(pb.row + 3);
      cmp->expect(fast.has_row(tree, line, row) == general.has_row(tree, line, row),
                  name + " has_row " + std::to_string(row));
      const int column = static_cast<int>(rng.below(140));
      cmp->expect(fast.offset_at(tree, line, row, column) == general.offset_at(tree, line, row, column),
                  name + " offset_at " + std::to_string(row) + "," + std::to_string(column));
      cmp->expect(fast.row_start(tree, line, row) == general.row_start(tree, line, row),
                  name + " row_start " + std::to_string(row));
    }
    const size_t rows = general.row_count(tree, line);
    cmp->expect(fast.row_count(tree, line) == rows, name + " row_count of line " + std::to_string(line));
    for (size_t row = 0; row < rows; ++row) {
      const RowInfo ia = fast.row_chars(tree, line, row, 0, 1 << 20, &a);
      const RowInfo ib = general.row_chars(tree, line, row, 0, 1 << 20, &b);
      bool same = a.size() == b.size() && ia.complete == ib.complete && ia.last == ib.last &&
                  ia.end_column == ib.end_column && ia.end == ib.end;
      for (size_t k = 0; same && k < a.size(); ++k) {
        same = a[k].c == b[k].c && a[k].offset == b[k].offset && a[k].column == b[k].column &&
               a[k].width == b[k].width && a[k].length == b[k].length && a[k].code_points == b[k].code_points &&
               a[k].emoji == b[k].emoji;
      }
      if (!same) {
        std::string detail = name + " row " + std::to_string(row) + " of line " + std::to_string(line);
        for (size_t k = 0; k < a.size() && k < b.size(); ++k) {
          if (row_char_text(a[k]) != row_char_text(b[k])) {
            detail += " at " + row_char_text(a[k]) + " vs " + row_char_text(b[k]);
            break;
          }
        }
        cmp->expect(false, detail);
      } else {
        ++cmp->checks;
      }
      // Cursor movement by cluster: the clusters of the rows are the steps
      // of next_cluster_offset.
      for (size_t k = 0; k + 1 < a.size() && k < 4; ++k) {
        cmp->expect(geditor::next_cluster_offset(tree, a[k].offset) == a[k + 1].offset,
                    name + " next_cluster_offset " + std::to_string(a[k].offset));
      }
    }
    const auto* sa = fast.checkpoints_for_testing(line);
    const auto* sb = general.checkpoints_for_testing(line);
    cmp->expect((sa == nullptr) == (sb == nullptr) && (sa == nullptr || *sa == *sb),
                name + " recorded states of line " + std::to_string(line));
  }
}

}  // namespace

// Every BMP unit that lone_cluster_width accepts is a cluster of its own of
// that width between two lone ASCII letters, as ClusterBuilder makes it, and
// the table equals the function.
TEST(layout_lone_cluster_width_matches_cluster_builder) {
  size_t lone = 0;
  size_t different = 0;
  const int8_t* table = geditor::lone_cluster_widths();
  for (uint32_t u = 0; u < 0x10000; ++u) {
    const wchar_t unit = static_cast<wchar_t>(u);
    const int w = geditor::lone_cluster_width(unit);
    if (table[u] != w) ++different;
    if (w < 0) continue;
    ++lone;
    const wchar_t s[] = {L'a', unit, L'b', 0};
    std::vector<Cluster> clusters;
    geditor::for_each_cluster(std::wstring_view(s, 3), [&](const Cluster& c) {
      clusters.push_back(c);
      return true;
    });
    if (clusters.size() != 3 || clusters[1].offset != 1 || clusters[1].length != 1 || clusters[1].width != w ||
        clusters[1].emoji || clusters[2].before != GraphemeBreaker::after_lone_cluster()) {
      ++different;
    }
  }
  std::printf("  [layout lone] %zu lone units checked, %zu different\n", lone, different);
  CHECK(lone > 40000);  // most of the BMP (49699 with Unicode 18.0.0)
  CHECK_EQ(different, size_t{0});
}

TEST(layout_lone_walk_matches_general_walk) {
  Compare cmp;
  Rng rng(0x10E);
  for (int round = 0; round < 4; ++round) {
    // Lines: short ones, and long ones over recorded states (every 64K).
    std::wstring text;
    const size_t lines = 3;
    for (size_t l = 0; l < lines; ++l) {
      if (l > 0) text += L'\n';
      const bool long_line = (round + l) % 2 == 0;
      text += random_line(rng, long_line ? 70000 + rng.below(80000) : rng.below(400), round % 2 == 0 ? 60 : 97);
    }
    // Widths: none, the narrowest, and window-like ones.
    for (const int width : {0, 1, 2, 7, 117}) {
      for (const int tab : {4, 8}) {
        if (width < 7 && tab != 4) continue;
        PieceTree tree(text);
        WrapLayout fast;
        WrapLayout general;
        fast.configure(width, tab);
        general.configure(width, tab);
        general.use_general_walk_for_testing(true);
        const std::string name = "width " + std::to_string(width) + " tab " + std::to_string(tab);
        compare_layouts(&cmp, rng, tree, fast, general, name);
        // Edits: an insertion and an erasure in the middle of the first
        // line (the states before them are kept), then compared again.
        const size_t at = tree.line_end(0) / 2;
        tree.insert(at, L"x\x0301\xD83D\xDE00 abc\t\x65E5");
        fast.note_edit(tree, at, 10, 0);
        general.note_edit(tree, at, 10, 0);
        compare_layouts(&cmp, rng, tree, fast, general, name + " after an insertion");
        const size_t from = tree.line_end(0) / 3;
        tree.erase(from, 5);
        fast.note_edit(tree, from, 0, 5);
        general.note_edit(tree, from, 0, 5);
        compare_layouts(&cmp, rng, tree, fast, general, name + " after an erasure");
      }
    }
  }
  std::printf("  [layout lone] %zu comparisons, %zu different\n", cmp.checks, cmp.different);
  CHECK_EQ(cmp.different, size_t{0});
}
