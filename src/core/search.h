// Literal and regexp search on the piece tree (docs/PLAN.md 3 "search.*"):
// the one matcher behind isearch, the highlighting of matches and
// query-replace. Matching is PCRE2 (16-bit, UTF, JIT) per logical line
// (PLAN 2 "unit of search"): a pattern never matches across "\n", and no
// contiguous copy of the whole buffer is made. A line that lies in one chunk
// of the piece tree is matched in place; a line split over several chunks is
// copied into a reused buffer.
//
// Nothing here calls PCRE2 before the first set() (PLAN 4.1: PCRE2 is
// initialised lazily, not at start-up).
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "piece_tree.h"

struct pcre2_real_code_16;
struct pcre2_real_match_data_16;
struct pcre2_real_match_context_16;
struct pcre2_real_jit_stack_16;

namespace geditor {

struct SearchMatch {
  size_t start = 0;
  size_t end = 0;

  bool operator==(const SearchMatch&) const = default;
};

enum class SearchResult { kFound, kNotFound, kError };

// Smart case (PLAN 2 "case"): true when `pattern` has an upper-case letter
// (a character that to_lower changes). In a regexp the character after a
// backslash and a "{...}" right after it (\p{Lu}, \x{1F600}) do not count.
bool pattern_has_upper_case(std::wstring_view pattern, bool regexp);
// Whether a search is case-sensitive under search.case_fold.
bool search_case_sensitive(CaseFold fold, std::wstring_view pattern, bool regexp);

// The number of patterns compiled by this process (a test hook for "PCRE2
// is not initialised at start-up"; also in the headless state).
size_t search_pattern_compiles();

class SearchPattern {
 public:
  SearchPattern() = default;
  ~SearchPattern();
  SearchPattern(const SearchPattern&) = delete;
  SearchPattern& operator=(const SearchPattern&) = delete;

  // Compiles `pattern` unless it is the pattern already compiled. A literal
  // pattern matches its text; a regexp is PCRE2 syntax with Unicode
  // properties (\w matches kanji). False (with error()) for an invalid
  // regexp; an empty pattern is valid and matches the empty string.
  bool set(std::wstring_view pattern, bool regexp, bool case_sensitive);
  void clear();
  bool valid() const { return code_ != nullptr; }
  const std::wstring& pattern() const { return pattern_; }
  bool regexp() const { return regexp_; }
  bool case_sensitive() const { return case_sensitive_; }
  // The PCRE2 message of the last failure: "missing closing parenthesis".
  const std::wstring& error() const { return error_; }

  // The first match that starts at or after `from` (on `from`'s line the
  // text before it is seen by ^ and look-behind). `not_empty_at_from`
  // refuses an empty match at `from` itself. kError: PCRE2 stopped (a match
  // limit), with error(). `for_expand`: expand() may follow (the matched
  // line is kept: pcre2_substitute wants the subject of the match itself).
  SearchResult find_forward(const PieceTree& text, size_t from, bool not_empty_at_from, SearchMatch* match,
                            bool for_expand = false);
  // The match with the greatest start <= `max_start` that ends at or before
  // `end_bound`.
  SearchResult find_backward(const PieceTree& text, size_t max_start, size_t end_bound, SearchMatch* match);
  // The non-empty matches that overlap [from, to), at most `max` of them, in
  // order (for highlighting). Lines longer than kLongLine are matched only
  // in a window of kLongLineMargin units around [from, to).
  void find_all(const PieceTree& text, size_t from, size_t to, size_t max, std::vector<SearchMatch>* out);
  static constexpr size_t kLongLine = 1 << 16;
  static constexpr size_t kLongLineMargin = 4096;

  // The replacement text for the match that the last find_forward with
  // `for_expand` returned: a regexp expands $1, ${1}, $$
  // and ${name} with pcre2_substitute; a literal pattern inserts
  // `replacement` as it is. False with error() for an invalid replacement
  // ("unknown substring" for $9 without a group 9).
  bool expand(std::wstring_view replacement, std::wstring* out);

  // query-replace "!" on one line (M5 retry R3): the match of the last
  // find_forward with `for_expand` and every later match of its line, found
  // on the copy of the line as it was (a replacement makes no new match;
  // after an empty match the next one may not be empty at the same place)
  // and expanded as expand() does. `out` is one edit: [from, to) of the
  // text becomes `text`. False with error() when an expansion or a match
  // fails; `out` then holds the replacements before that one (count may be
  // 0).
  struct LineReplacement {
    size_t from = 0;
    size_t to = 0;
    std::wstring text;
    size_t count = 0;
    size_t point = 0;         // in `text`: after the last replacement
    bool last_empty = false;  // the last match was empty
  };
  bool expand_rest_of_line(std::wstring_view replacement, LineReplacement* out);

 private:
  // pcre2_match on `subject`; > 0 found (ovector in `data`), PCRE2's
  // PCRE2_ERROR_NOMATCH or another negative error code.
  int match(std::wstring_view subject, size_t start, unsigned options, pcre2_real_match_data_16* data);
  bool ensure_context();
  void set_match_error(int rc);
  // [from, to) of `text` (within one line) as one view: in place when it
  // lies in one chunk, else copied into buf_.
  std::wstring_view line_text(const PieceTree& text, size_t from, size_t to);

  pcre2_real_code_16* code_ = nullptr;
  pcre2_real_match_data_16* data_ = nullptr;      // find_forward / find_backward
  pcre2_real_match_data_16* all_data_ = nullptr;  // find_all
  pcre2_real_match_context_16* context_ = nullptr;
  pcre2_real_jit_stack_16* jit_stack_ = nullptr;
  std::wstring pattern_;
  bool regexp_ = false;
  bool case_sensitive_ = false;
  bool compiled_once_ = false;  // pattern_ / regexp_ / case_sensitive_ describe code_ (or its failure)
  std::wstring error_;
  std::wstring buf_;
  // The line of the last find_forward with `for_expand`, matched again in
  // place so that data_ refers to it; false when there is none.
  std::wstring expand_subject_;
  size_t expand_line_start_ = 0;  // the offset of that line in the text
  size_t expand_start_ = 0;     // the start offset of that match
  unsigned expand_options_ = 0;  // and its match options
  bool expand_ready_ = false;
};

}  // namespace geditor
