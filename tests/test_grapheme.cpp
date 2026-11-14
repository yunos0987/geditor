// Extended grapheme clusters (UAX #29) and cluster widths ([M3c-1], [M3c-2]).

#include <cstdio>
#include <string>
#include <vector>

#include "char_width.h"
#include "grapheme.h"
#include "piece_tree.h"
#include "test_harness.h"
#include "unicode.h"

namespace {

struct GraphemeBreakCase {
  int line;
  size_t first;
  size_t count;
  const char* breaks;
};

#include "grapheme_break_test.inc"

using geditor::Cluster;
using geditor::PieceTree;

std::wstring utf16(const std::vector<char32_t>& cps) {
  std::wstring s;
  for (const char32_t c : cps) geditor::append_utf16(s, c);
  return s;
}

// Boundaries of `s` (UTF-16 offsets, the end included) from for_each_cluster.
std::vector<size_t> string_boundaries(const std::wstring& s) {
  std::vector<size_t> out;
  geditor::for_each_cluster(s, [&](const Cluster& c) {
    out.push_back(c.offset);
    return true;
  });
  out.push_back(s.size());
  return out;
}

// The widths of the clusters of `s`.
std::vector<int> widths(const std::wstring& s) {
  std::vector<int> out;
  geditor::for_each_cluster(s, [&](const Cluster& c) {
    out.push_back(c.width);
    return true;
  });
  return out;
}

int single_width(const std::wstring& s) {
  const std::vector<int> w = widths(s);
  CHECK_EQ(w.size(), size_t{1});
  return w.empty() ? -1 : w.front();
}

}  // namespace

// [M3c-1] Every case of GraphemeBreakTest.txt of the table's Unicode version:
// through the string iterator, and through the PieceTree functions (start,
// next and previous boundary from every code unit).
TEST(grapheme_break_test_all_cases) {
  CHECK_EQ(std::string(kGraphemeBreakTestVersion), std::string(geditor::grapheme_unicode_version()));
  CHECK_EQ(std::string(kGraphemeBreakTestVersion), std::string(geditor::char_width_unicode_version()));
  size_t passed = 0;
  size_t failed = 0;
  for (const GraphemeBreakCase& k : kGraphemeBreakCases) {
    std::vector<char32_t> cps(kGraphemeBreakCodePoints + k.first, kGraphemeBreakCodePoints + k.first + k.count);
    const std::wstring s = utf16(cps);
    // Expected boundaries in UTF-16 offsets.
    std::vector<size_t> expected;
    size_t offset = 0;
    for (size_t i = 0; i <= cps.size(); ++i) {
      if (k.breaks[i] == '1') expected.push_back(offset);
      if (i < cps.size()) offset += cps[i] >= 0x10000 ? 2 : 1;
    }
    bool ok = string_boundaries(s) == expected;
    const PieceTree t(s);
    for (size_t u = 0; u <= s.size() && ok; ++u) {
      // The boundary at or before u and the next one after it.
      size_t at_or_before = 0;
      size_t after = s.size();
      for (const size_t b : expected) {
        if (b <= u) at_or_before = b;
      }
      for (auto it = expected.rbegin(); it != expected.rend(); ++it) {
        if (*it > u) after = *it;
      }
      // A unit in the middle of a surrogate pair belongs to its code point.
      const bool mid_pair = u > 0 && u < s.size() && geditor::is_low_surrogate(s[u]) &&
                            geditor::is_high_surrogate(s[u - 1]);
      if (!mid_pair && geditor::cluster_start(t, u) != at_or_before) ok = false;
      if (u < s.size() && geditor::next_cluster_offset(t, u) != after) ok = false;
      if (u > 0 && !mid_pair) {
        size_t before = 0;
        for (const size_t b : expected) {
          if (b < u) before = b;
        }
        if (geditor::prev_cluster_offset(t, u) != before) ok = false;
      }
    }
    if (ok) {
      ++passed;
    } else {
      ++failed;
      CHECK(ok);
      std::printf("  GraphemeBreakTest line %d fails\n", k.line);
    }
  }
  CHECK_EQ(failed, size_t{0});
  CHECK_EQ(passed, sizeof(kGraphemeBreakCases) / sizeof(kGraphemeBreakCases[0]));
  std::printf("  GraphemeBreakTest %s: %zu of %zu cases pass\n", kGraphemeBreakTestVersion, passed,
              sizeof(kGraphemeBreakCases) / sizeof(kGraphemeBreakCases[0]));
}

// ---- [M3c-2] Width of each sample (PLAN 2 "column counting", G2) ----

TEST(grapheme_width_text_style_smiling_face_is_1) {
  CHECK_EQ(single_width(L"☺"), 1);
}

TEST(grapheme_width_vs16_makes_2) {
  CHECK_EQ(single_width(L"☺️"), 2);
}

TEST(grapheme_width_vs15_keeps_base_width) {
  CHECK_EQ(single_width(L"☺︎"), 1);
}

TEST(grapheme_vs15_after_an_emoji_presentation_character_is_text_style) {
  // U+1F600 U+FE0E: the first code point's width (2, East Asian Wide) and not
  // shown as emoji, so it is drawn in one colour (user, 2026-11-14).
  const std::wstring text(L"\U0001F600\xFE0E");
  CHECK_EQ(single_width(text), 2);
  bool emoji = true;
  int clusters = 0;
  geditor::for_each_cluster(std::wstring_view(text), [&](const Cluster& c) {
    emoji = c.emoji;
    ++clusters;
    return true;
  });
  CHECK_EQ(clusters, 1);
  CHECK(!emoji);
  bool plain_emoji = false;
  geditor::for_each_cluster(std::wstring_view(L"\U0001F600"), [&](const Cluster& c) {
    plain_emoji = c.emoji;
    return true;
  });
  CHECK(plain_emoji);
}

TEST(grapheme_width_vs16_after_a_non_emoji_keeps_base_width) {
  // U+FE0F shows only Emoji characters as emoji: "g" + U+FE0F is text, and
  // "#" + U+FE0F (Emoji=Yes) is an emoji.
  CHECK_EQ(single_width(L"g\xFE0F"), 1);
  CHECK_EQ(single_width(L"#\xFE0F"), 2);
  bool g_emoji = true;
  bool hash_emoji = false;
  geditor::for_each_cluster(std::wstring_view(L"g\xFE0F"), [&](const Cluster& c) {
    g_emoji = c.emoji;
    return true;
  });
  geditor::for_each_cluster(std::wstring_view(L"#\xFE0F"), [&](const Cluster& c) {
    hash_emoji = c.emoji;
    return true;
  });
  CHECK(!g_emoji);
  CHECK(hash_emoji);
  CHECK(geditor::is_emoji(U'#'));
  CHECK(!geditor::is_emoji(U'g'));
}

TEST(grapheme_width_emoji_presentation_is_2) {
  CHECK_EQ(single_width(L"\U0001F44D"), 2);
}

TEST(grapheme_width_skin_tone_is_one_cluster_of_2) {
  CHECK_EQ(single_width(L"\U0001F44D\U0001F3FD"), 2);
  // A modifier after a base without Emoji_Presentation makes it 2 as well.
  CHECK_EQ(single_width(L"☝\U0001F3FB"), 2);
}

TEST(grapheme_width_zwj_family_is_one_cluster_of_2) {
  CHECK_EQ(single_width(L"\U0001F468‍\U0001F469‍\U0001F467‍\U0001F466"), 2);
  // A ZWJ between text-style pictographs: shown as emoji.
  CHECK_EQ(single_width(L"❤‍❤"), 2);
}

TEST(grapheme_width_flag_pair_is_2) {
  CHECK_EQ(single_width(L"\U0001F1EF\U0001F1F5"), 2);  // JP
  // Three indicators: a pair and a lone one.
  const std::vector<int> w = widths(L"\U0001F1EF\U0001F1F5\U0001F1FA");
  CHECK_EQ(w.size(), size_t{2});
  if (w.size() == 2) {
    CHECK_EQ(w[0], 2);
    CHECK_EQ(w[1], 1);
  }
}

TEST(grapheme_width_lone_regional_indicator_is_table_width) {
  CHECK_EQ(single_width(L"\U0001F1EF"), geditor::char_width(U'\U0001F1EF'));
  CHECK_EQ(single_width(L"\U0001F1EF"), 1);
}

TEST(grapheme_width_keycap_is_2) {
  CHECK_EQ(single_width(L"#️⃣"), 2);
  // Without U+FE0F it is the base's width.
  CHECK_EQ(single_width(L"#⃣"), 1);
}

TEST(grapheme_width_kana_with_voiced_mark_is_one_cluster_of_2) {
  CHECK_EQ(single_width(L"が"), 2);
}

TEST(grapheme_width_latin_with_acute_is_one_cluster_of_1) {
  CHECK_EQ(single_width(L"é"), 1);
}

TEST(grapheme_width_hangul_jamo_sequence_is_one_cluster_of_2) {
  CHECK_EQ(single_width(L"각"), 2);  // L V T
}

TEST(grapheme_width_devanagari_conjunct_is_one_cluster) {
  // KA + VIRAMA + SSA (GB9c): one cluster, the width of KA.
  CHECK_EQ(single_width(L"क्ष"), 1);
  CHECK(string_boundaries(L"क्ष") == (std::vector<size_t>{0, 3}));
}

TEST(grapheme_width_ascii_cjk_and_tab) {
  CHECK(widths(L"a一\t") == (std::vector<int>{1, 2, 1}));
}

TEST(grapheme_width_zero_width_only_clusters_are_0) {
  CHECK_EQ(single_width(L"​"), 0);
  CHECK_EQ(single_width(L"́"), 0);
  CHECK_EQ(single_width(L"́̂"), 0);
  CHECK_EQ(single_width(L"́️"), 0);
}

// ---- PieceTree functions ----

TEST(grapheme_tree_steps_over_clusters) {
  // a | e+acute | family | flag | keycap | b
  const std::wstring family = L"\U0001F468‍\U0001F469‍\U0001F467";
  const std::wstring s = L"aé" + family + L"\U0001F1EF\U0001F1F5#️⃣b";
  const PieceTree t(s);
  std::vector<size_t> forward{0};
  for (size_t p = 0; p < t.length();) {
    p = geditor::next_cluster_offset(t, p);
    forward.push_back(p);
  }
  const size_t f = 3;
  const std::vector<size_t> expected{0, 1, f, f + family.size(), f + family.size() + 4, f + family.size() + 7,
                                     f + family.size() + 8};
  CHECK(forward == expected);
  std::vector<size_t> backward{t.length()};
  for (size_t p = t.length(); p > 0;) {
    p = geditor::prev_cluster_offset(t, p);
    backward.insert(backward.begin(), p);
  }
  CHECK(backward == expected);
  // Every offset inside the family snaps to its start.
  for (size_t u = f; u < f + family.size(); ++u) CHECK_EQ(geditor::cluster_start(t, u), f);
}

TEST(grapheme_tree_cluster_at_measures) {
  const PieceTree t(L"x☺️y");
  const Cluster c = geditor::cluster_at(t, 2);
  CHECK_EQ(c.offset, size_t{1});
  CHECK_EQ(c.length, size_t{2});
  CHECK_EQ(c.width, 2);
  CHECK_EQ(static_cast<uint32_t>(c.first), 0x263Au);
}

TEST(grapheme_tree_line_feed_always_breaks) {
  const PieceTree t(L"a\ńb");
  CHECK_EQ(geditor::next_cluster_offset(t, 1), size_t{2});
  CHECK_EQ(geditor::next_cluster_offset(t, 2), size_t{3});  // the lone mark is a cluster
  CHECK_EQ(geditor::prev_cluster_offset(t, 3), size_t{2});
}

// [C6] Moving near the end of a 10MB line looks back a few code points only.
TEST(grapheme_tree_steps_near_end_of_long_line_are_local) {
  std::wstring s(10 * 1024 * 1024, L'a');
  s += L"é";
  const PieceTree t(std::move(s));
  size_t scanned = 0;
  const size_t end = t.length();
  CHECK_EQ(geditor::prev_cluster_offset(t, end, &scanned), end - 2);
  CHECK(scanned < 16);
  scanned = 0;
  CHECK_EQ(geditor::next_cluster_offset(t, end - 2, &scanned), end);
  CHECK(scanned < 16);
  scanned = 0;
  CHECK_EQ(geditor::cluster_start(t, end - 1, &scanned), end - 2);
  CHECK(scanned < 16);
}
