// SearchPattern::find_backward after the deferred performance work (before
// M6): long lines are scanned in windows back from max_start instead of
// from the line start. `head_find_backward` below is a copy of
// find_backward at HEAD 368313b (every match of a forward scan of each line,
// the last one that qualifies), on a pattern compiled with the same options;
// the tests compare the two on generated lines and patterns.

#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 16
#endif
#include <pcre2.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "piece_tree.h"
#include "search.h"
#include "test_harness.h"
#include "unicode.h"

using geditor::PieceTree;
using geditor::SearchMatch;
using geditor::SearchPattern;
using geditor::SearchResult;

namespace {

// A pattern compiled as SearchPattern::set compiles it (UTF, invalid UTF
// allowed, UCP or LITERAL, CASELESS), matched with a JIT stack of 32K to 1M.
class HeadPattern {
 public:
  HeadPattern(std::wstring_view pattern, bool regexp, bool case_sensitive) {
    uint32_t options = PCRE2_UTF | PCRE2_MATCH_INVALID_UTF;
    options |= regexp ? PCRE2_UCP : PCRE2_LITERAL;
    if (!case_sensitive) options |= PCRE2_CASELESS;
    int code = 0;
    PCRE2_SIZE offset = 0;
    code_ = pcre2_compile_16(reinterpret_cast<PCRE2_SPTR16>(pattern.data()), pattern.size(), options, &code,
                             &offset, nullptr);
    if (code_ == nullptr) return;
    pcre2_jit_compile_16(code_, PCRE2_JIT_COMPLETE);
    data_ = pcre2_match_data_create_from_pattern_16(code_, nullptr);
    context_ = pcre2_match_context_create_16(nullptr);
    jit_stack_ = pcre2_jit_stack_create_16(32 * 1024, 1024 * 1024, nullptr);
    pcre2_jit_stack_assign_16(context_, nullptr, jit_stack_);
  }
  ~HeadPattern() {
    if (data_ != nullptr) pcre2_match_data_free_16(data_);
    if (code_ != nullptr) pcre2_code_free_16(code_);
    if (context_ != nullptr) pcre2_match_context_free_16(context_);
    if (jit_stack_ != nullptr) pcre2_jit_stack_free_16(jit_stack_);
  }
  HeadPattern(const HeadPattern&) = delete;
  HeadPattern& operator=(const HeadPattern&) = delete;
  bool valid() const { return code_ != nullptr; }

  // find_backward of 368313b on the text `text` (the whole buffer).
  SearchResult find_backward(const std::wstring& text, size_t max_start, size_t end_bound, SearchMatch* out) {
    max_start = std::min(max_start, text.size());
    end_bound = std::min(end_bound, text.size());
    // Line starts.
    std::vector<size_t> starts{0};
    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] == L'\n') starts.push_back(i + 1);
    }
    size_t line = 0;
    while (line + 1 < starts.size() && starts[line + 1] <= max_start) ++line;
    for (;; --line) {
      const size_t line_start = starts[line];
      const size_t line_end = line + 1 < starts.size() ? starts[line + 1] - 1 : text.size();
      const std::wstring_view subject(text.data() + line_start, line_end - line_start);
      bool found = false;
      SearchMatch best;
      size_t start = 0;
      while (start <= subject.size()) {
        const wchar_t* s = subject.data() == nullptr ? L"" : subject.data();
        const int rc = pcre2_match_16(code_, reinterpret_cast<PCRE2_SPTR16>(s), subject.size(), start, 0, data_,
                                      context_);
        if (rc == PCRE2_ERROR_NOMATCH) break;
        if (rc < 0) return SearchResult::kError;
        const PCRE2_SIZE* ov = pcre2_get_ovector_pointer_16(data_);
        const size_t ms = line_start + ov[0];
        const size_t me = line_start + ov[1];
        if (ms > max_start) break;
        if (me <= end_bound) {
          best = SearchMatch{ms, me};
          found = true;
        }
        if (ov[0] >= subject.size()) break;
        size_t units = 0;
        geditor::decode_at(subject, ov[0], &units);
        start = ov[0] + units;
      }
      if (found) {
        *out = best;
        return SearchResult::kFound;
      }
      if (line == 0) return SearchResult::kNotFound;
    }
  }

 private:
  pcre2_code_16* code_ = nullptr;
  pcre2_match_data_16* data_ = nullptr;
  pcre2_match_context_16* context_ = nullptr;
  pcre2_jit_stack_16* jit_stack_ = nullptr;
};

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

// Lines of ASCII, CJK, surrogate pairs, lone surrogates, combining marks and
// spaces; some lines longer than several windows.
std::wstring random_text(Rng& rng, size_t lines, size_t max_line) {
  static const wchar_t* const kPieces[] = {
      L"a",        L"b",          L"ab",     L"abc",    L"z",      L" ",      L"A",  L"\x6F22\x5B57",
      L"\xD83D\xDE00",  // U+1F600
      L"\xD800",        // a lone high surrogate
      L"\xDC00",        // a lone low surrogate
      L"e\x0301",       // a combining mark
      L"\xD83D\xDC68\x200D\xD83D\xDC69",  // ZWJ
      L"1", L"\t", L"\x3042", L"_"};
  std::wstring s;
  for (size_t l = 0; l < lines; ++l) {
    if (l > 0) s += L'\n';
    const size_t length = rng.below(4) == 0 ? rng.below(max_line) : rng.below(40);
    const size_t begin = s.size();
    while (s.size() - begin < length) s += kPieces[rng.below(std::size(kPieces))];
  }
  return s;
}

// The tree of `text` in pieces of random sizes (lines over several chunks
// are gathered into the matcher's buffer).
PieceTree tree_of(Rng& rng, const std::wstring& text) {
  PieceTree t;
  size_t at = 0;
  while (at < text.size()) {
    const size_t n = std::min(text.size() - at, rng.below(700) + 1);
    t.insert(t.length(), std::wstring_view(text).substr(at, n));
    at += n;
  }
  return t;
}

struct Pattern {
  const wchar_t* text;
  bool regexp;
};

const Pattern kPatterns[] = {
    {L".*", true},          {L"a*", true},          {L"\\w+", true},        {L"", true},
    {L"a|ab", true},        {L"(?<=a)b", true},     {L"(?<!a)b", true},     {L"\\b\\w", true},
    {L"^a", true},          {L"a$", true},          {L"x*", true},          {L".", true},
    {L"\\X", true},         {L"(?=b)", true},       {L"[^a]+", true},       {L"b+?", true},
    {L"\\p{Han}+", true},   {L".{3}", true},        {L"(?<=\\x{1F600}).", true},
    {L"\\s*", true},        {L"a(?=.*z)", true},    {L"(a)(?:b|$)", true},  {L"\\Ga", true},
    {L"a\\Kb", true},       {L"(*SKIP)a|b", true},  {L"b(*COMMIT)c|a", true},
    {L"a", false},          {L"ab", false},         {L"\x6F22", false},     {L"\xD83D\xDE00", false},
    {L"e\x0301", false},    {L"z", false},          {L"A", false},          {L".*", false},
};

}  // namespace

TEST(search_backward_windows_match_head) {
  size_t cases = 0;
  size_t different = 0;
  size_t found = 0;
  Rng rng(0xBAC4);
  for (int round = 0; round < 40; ++round) {
    const std::wstring text = random_text(rng, rng.below(6) + 1, round % 4 == 0 ? 3000 : 900);
    const PieceTree tree = tree_of(rng, text);
    CHECK(tree.text() == text);
    for (const Pattern& p : kPatterns) {
      for (const bool case_sensitive : {false, true}) {
        HeadPattern head(p.text, p.regexp, case_sensitive);
        SearchPattern now;
        const bool ok = now.set(p.text, p.regexp, case_sensitive);
        CHECK_EQ(ok, head.valid());
        if (!ok) continue;
        for (int q = 0; q < 12; ++q) {
          size_t max_start = q == 0 ? text.size() : rng.below(text.size() + 1);
          size_t end_bound = q % 3 == 0 ? text.size() : q % 3 == 1 ? max_start : rng.below(text.size() + 2);
          if (q == 11) max_start = text.size() + 5;  // past the end: clamped
          SearchMatch a{7, 7};
          SearchMatch b{7, 7};
          const SearchResult ra = now.find_backward(tree, max_start, end_bound, &a);
          const SearchResult rb = head.find_backward(text, max_start, end_bound, &b);
          ++cases;
          found += rb == SearchResult::kFound ? 1 : 0;
          if (ra != rb || (rb == SearchResult::kFound && !(a == b))) {
            ++different;
            if (different <= 5) {
              ::test::report_failure(__FILE__, __LINE__,
                                     "differs from HEAD: pattern " + ::test::to_display(std::wstring(p.text)) +
                                         " max_start " + std::to_string(max_start) + " end_bound " +
                                         std::to_string(end_bound) + ": " + std::to_string(a.start) + "-" +
                                         std::to_string(a.end) + " vs " + std::to_string(b.start) + "-" +
                                         std::to_string(b.end));
            }
          }
        }
      }
    }
  }
  std::printf("  [search backward] %zu cases compared (%zu found), %zu different\n", cases, found, different);
  CHECK_EQ(different, size_t{0});
}

// A long line (12K units: the scan of HEAD takes quadratic time for .*;
// geditor_perf measures 400K): from its end and further back, with the
// answer near max_start and far from it, the windows find what the scan
// finds.
TEST(search_backward_long_line_matches_head) {
  std::wstring line;
  while (line.size() < 12000) line += L"abcdefg \x6F22\xD83D\xDE00 ";
  line.resize(12000);
  const std::wstring text = L"first\n" + line + L"\nlast";
  PieceTree tree(text);
  size_t cases = 0;
  for (const Pattern& p : {Pattern{L".*", true}, Pattern{L"a*", true}, Pattern{L"\\w+", true},
                           Pattern{L"cdef", false}, Pattern{L"fir", false}, Pattern{L"(?<=g )\\S", true}}) {
    HeadPattern head(p.text, p.regexp, false);
    SearchPattern now;
    CHECK(now.set(p.text, p.regexp, false));
    const size_t end_of_line = 6 + line.size();
    for (const size_t max_start : {end_of_line, end_of_line - 1, size_t{6} + 1000, size_t{6} + 3}) {
      for (const size_t end_bound : {end_of_line, max_start, text.size()}) {
        SearchMatch a;
        SearchMatch b;
        const SearchResult ra = now.find_backward(tree, max_start, end_bound, &a);
        const SearchResult rb = head.find_backward(text, max_start, end_bound, &b);
        ++cases;
        CHECK(ra == rb);
        if (ra == SearchResult::kFound && rb == SearchResult::kFound) {
          CHECK_EQ(a.start, b.start);
          CHECK_EQ(a.end, b.end);
        }
      }
    }
  }
  std::printf("  [search backward] long line: %zu cases compared\n", cases);
}
