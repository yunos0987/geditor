// M3c retry 1: the backward cluster search is linear (R1), point after an
// insertion goes forward over the cluster it joined (R2), the minibuffer
// input is segmented apart from the prompt (R3), and the widths of the
// grapheme table agree with char_width (R4).

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "char_width.h"
#include "echo_text.h"
#include "fake_host.h"
#include "grapheme.h"
#include "piece_tree.h"
#include "test_harness.h"
#include "unicode.h"

using geditor::PieceTree;
using test::Session;

namespace {

// A run of k copies of `unit` (plus `head` before and `tail` after) that
// forms one cluster.
std::wstring run_of(const std::wstring& head, const std::wstring& unit, size_t k, const std::wstring& tail) {
  std::wstring s = head;
  s.reserve(head.size() + unit.size() * k + tail.size());
  for (size_t i = 0; i < k; ++i) s += unit;
  s += tail;
  return s;
}

struct RunCost {
  size_t prev_scanned = 0;
  size_t start_scanned = 0;
  double ms = 0;
  bool one_cluster = false;
};

RunCost measure_run(const std::wstring& s) {
  const PieceTree t(s);
  RunCost cost;
  const auto t0 = std::chrono::steady_clock::now();
  const size_t prev = geditor::prev_cluster_offset(t, t.length(), &cost.prev_scanned);
  const size_t start = geditor::cluster_start(t, t.length() - 1, &cost.start_scanned);
  const auto t1 = std::chrono::steady_clock::now();
  cost.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  cost.one_cluster = prev == 0 && start == 0 && geditor::next_cluster_offset(t, 0) == t.length();
  return cost;
}

// [R1] The work of one backward search over a run of k code points that is
// one cluster grows linearly with k (it was quadratic).
void check_linear(const char* name, const std::wstring& head, const std::wstring& unit, const std::wstring& tail) {
  const size_t k = 16000;
  const RunCost one = measure_run(run_of(head, unit, k, tail));
  const RunCost two = measure_run(run_of(head, unit, 2 * k, tail));
  std::printf("  %s: k=%zu prev scanned %zu, cluster_start scanned %zu, %.2f ms; k=%zu scanned %zu, %.2f ms\n", name,
              k, one.prev_scanned, one.start_scanned, one.ms, 2 * k, two.prev_scanned, two.ms);
  CHECK(one.one_cluster);
  CHECK(two.one_cluster);
  const size_t units = unit.size() * k + head.size() + tail.size();
  CHECK(one.prev_scanned <= 8 * units);
  CHECK(one.start_scanned <= 8 * units);
  CHECK(two.prev_scanned <= 3 * one.prev_scanned);
}

// Boundaries of `s` by the forward segmentation, the end included.
std::vector<size_t> forward_boundaries(const std::wstring& s) {
  std::vector<size_t> out;
  geditor::for_each_cluster(std::wstring_view(s), [&](const geditor::Cluster& c) {
    out.push_back(c.offset);
    return true;
  });
  out.push_back(s.size());
  return out;
}

}  // namespace

TEST(retry1_backward_search_is_linear_for_spacing_marks) {
  check_linear("a + U+0903 x k", L"a", L"\x0903", L"");
}

TEST(retry1_backward_search_is_linear_for_hangul_leading_jamo) {
  check_linear("U+1100 x k", L"", L"\x1100", L"");
}

TEST(retry1_backward_search_is_linear_for_zwj_chains) {
  check_linear("(U+1F468 ZWJ) x k + U+1F468", L"", L"\U0001F468\x200D", L"\U0001F468");
}

TEST(retry1_backward_search_is_linear_for_prepend_runs) {
  check_linear("U+0600 x k + a", L"", L"\x0600", L"a");
}

// [R1] cluster_start / prev / next of the PieceTree agree with the forward
// segmentation at every unit of random strings of every property class.
TEST(retry1_tree_cluster_functions_match_forward_segmentation) {
  const std::vector<std::wstring> alphabet = {
      L"a", L" ", L"\n", L"\t", L"\r",
      L"\x0301",                          // Extend
      L"\x200D",                          // ZWJ
      L"\x0903",                          // SpacingMark
      L"\x0600",                          // Prepend
      L"\x1100", L"\x1161", L"\x11A8",    // L, V, T
      L"\xAC00", L"\xAC01",               // LV, LVT
      L"\U0001F1EF", L"\U0001F1F5",       // Regional_Indicator
      L"\U0001F468", L"\x2764",           // Extended_Pictographic
      L"\U0001F3FD",                      // Emoji_Modifier (Extend)
      L"\xFE0F",                          // variation selector (Extend)
      L"\x0915", L"\x0937",               // InCB Consonant
      L"\x094D",                          // InCB Linker
      L"\x1CF5",                          // InCB Linker with GCB Other
      L"\x0308",                          // InCB Extend
      L"\x3042",                          // wide Other
  };
  uint32_t seed = 12345;
  auto next = [&seed] {
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) & 0x7FFF;
  };
  size_t strings = 0;
  size_t failures = 0;
  for (int n = 0; n < 2000; ++n) {
    std::wstring s;
    const size_t length = 1 + next() % 24;
    for (size_t i = 0; i < length; ++i) s += alphabet[next() % alphabet.size()];
    const std::vector<size_t> expected = forward_boundaries(s);
    const PieceTree t(s);
    bool ok = true;
    for (size_t u = 0; u <= s.size() && ok; ++u) {
      const bool mid_pair = u > 0 && u < s.size() && geditor::is_low_surrogate(s[u]) &&
                            geditor::is_high_surrogate(s[u - 1]);
      size_t at_or_before = 0;
      size_t before = 0;
      size_t after = s.size();
      for (const size_t b : expected) {
        if (b <= u) at_or_before = b;
        if (b < u) before = b;
      }
      for (auto it = expected.rbegin(); it != expected.rend(); ++it) {
        if (*it > u) after = *it;
      }
      if (!mid_pair && geditor::cluster_start(t, u) != at_or_before) ok = false;
      if (u < s.size() && geditor::next_cluster_offset(t, u) != after) ok = false;
      if (u > 0 && !mid_pair && geditor::prev_cluster_offset(t, u) != before) ok = false;
    }
    ++strings;
    if (!ok) {
      ++failures;
      if (failures <= 3) std::printf("  mismatch on random string %d (%zu units)\n", n, s.size());
    }
  }
  std::printf("  %zu random strings, %zu mismatches\n", strings, failures);
  CHECK_EQ(failures, size_t{0});
}

// ---- [R2] Point after an insertion that joins the next cluster ----

TEST(retry1_typing_zwj_between_emoji_keeps_typing_order) {
  Session s;
  s.load(L"\U0001F468\U0001F469");
  s.set_point(2);
  s.type(L"\x200D");
  CHECK_EQ(s.point(), size_t{5});  // after the joined family, not before it
  s.type(L"x");
  CHECK_EQ(s.text(), std::wstring(L"\U0001F468\x200D\U0001F469x"));
  // Typing is one undo group: one undo takes both characters back.
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"\U0001F468\U0001F469"));
  CHECK_EQ(s.point(), size_t{2});
  // The insertion alone, undone.
  s.type(L"\x200D");
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"\U0001F468\U0001F469"));
}

TEST(retry1_typing_a_virama_before_a_consonant_keeps_typing_order) {
  Session s;
  s.load(L"\x0915\x0937");
  s.set_point(1);
  s.type(L"\x094D");
  CHECK_EQ(s.point(), size_t{3});
  s.type(L"x");
  CHECK_EQ(s.text(), std::wstring(L"\x0915\x094D\x0937x"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"\x0915\x0937"));
  CHECK_EQ(s.point(), size_t{1});
  s.type(L"\x094D");
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"\x0915\x0937"));
}

TEST(retry1_typing_a_prepend_character_keeps_typing_order) {
  Session s;
  s.load(L"abc");
  s.set_point(1);
  s.type(L"\x0600");
  // U+0600 joins the "b" after it: point goes past that cluster.
  CHECK_EQ(s.point(), size_t{3});
  s.type(L"x");
  CHECK_EQ(s.text(), std::wstring(L"a\x0600" L"bxc"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  CHECK_EQ(s.point(), size_t{1});
  s.type(L"\x0600");
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"abc"));
}

TEST(retry1_other_commands_still_snap_back) {
  // Not an insertion: deleting the TAB between "e" and U+0301 snaps back.
  Session s;
  s.load(L"e\t\x0301");
  s.set_point(2);
  s.keys(L"DEL");
  CHECK_EQ(s.point(), size_t{0});
}

// ---- [R3] The minibuffer input is segmented apart from the prompt ----

TEST(retry1_minibuffer_input_starting_with_a_mark_has_its_caret_at_the_start) {
  const geditor::win::MinibufferLine line = geditor::win::fit_minibuffer(L"M-x ", L"\x0301" L"abc", 0, L"", 80);
  CHECK_EQ(line.input_start, size_t{4});
  CHECK_EQ(line.input_column, 4);
  CHECK_EQ(line.cursor_column, 4);
  const geditor::win::MinibufferLine end = geditor::win::fit_minibuffer(L"M-x ", L"\x0301" L"abc", 4, L"", 80);
  CHECK_EQ(end.cursor_column, 4 + 3);  // the lone mark has no cells
}

// ---- [R4] The widths in the grapheme table ----

TEST(retry1_grapheme_table_widths_equal_char_width) {
  size_t differences = 0;
  for (char32_t c = 0; c <= 0x10FFFF; ++c) {
    if (geditor::grapheme_table_width(c) != geditor::char_width(c)) ++differences;
  }
  CHECK_EQ(differences, size_t{0});
}
