#include "search.h"

#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 16
#endif
#include <pcre2.h>

#include <algorithm>
#include <cwchar>
#include <iterator>

#include "unicode.h"

namespace geditor {
namespace {

size_t g_compiles = 0;

// The JIT stack of a pattern (PCRE2's default is 32K of machine stack).
constexpr size_t kJitStackStart = 32 * 1024;
constexpr size_t kJitStackMax = 1024 * 1024;

PCRE2_SPTR16 sptr(const wchar_t* s) {
  return reinterpret_cast<PCRE2_SPTR16>(s);
}

std::wstring error_message(int code) {
  wchar_t buf[256] = {};
  const int n = pcre2_get_error_message_16(code, reinterpret_cast<PCRE2_UCHAR16*>(buf), std::size(buf));
  return n > 0 ? std::wstring(buf, static_cast<size_t>(n)) : std::wstring(L"PCRE2 error ") + std::to_wstring(code);
}

// The offset after the code point at `i` (a surrogate pair is one).
size_t next_code_point(std::wstring_view s, size_t i) {
  size_t units = 0;
  decode_at(s, i, &units);
  return i + units;
}

// Calls f(line_start, line_text) for each logical line from `from` (a line
// start) to the end of the text, in order, until f returns false. A line in
// one chunk is passed in place; one split over chunks is gathered in `buf`.
template <typename F>
void scan_lines(const PieceTree& text, size_t from, std::wstring& buf, F&& f) {
  size_t line_start = from;
  bool pending = false;
  bool stopped = false;
  buf.clear();
  text.for_each_chunk(from, text.length() - from, [&](std::wstring_view chunk) {
    size_t i = 0;
    while (i < chunk.size()) {
      const wchar_t* nl = std::wmemchr(chunk.data() + i, L'\n', chunk.size() - i);
      if (nl == nullptr) {
        buf.append(chunk.substr(i));
        pending = true;
        return true;
      }
      const size_t j = static_cast<size_t>(nl - chunk.data());
      std::wstring_view line;
      if (pending) {
        buf.append(chunk.substr(i, j - i));
        line = buf;
      } else {
        line = chunk.substr(i, j - i);
      }
      if (!f(line_start, line)) {
        stopped = true;
        return false;
      }
      line_start += line.size() + 1;
      buf.clear();
      pending = false;
      i = j + 1;
    }
    return true;
  });
  // The last line (after the last "\n", possibly empty).
  if (!stopped) f(line_start, std::wstring_view(buf));
}

}  // namespace

bool pattern_has_upper_case(std::wstring_view pattern, bool regexp) {
  size_t i = 0;
  while (i < pattern.size()) {
    if (regexp && pattern[i] == L'\\') {
      // The escaped character, and a "{...}" right after it.
      i = i + 1 < pattern.size() ? next_code_point(pattern, i + 1) : pattern.size();
      if (i < pattern.size() && pattern[i] == L'{') {
        const size_t close = pattern.find(L'}', i);
        i = close == std::wstring_view::npos ? pattern.size() : close + 1;
      }
      continue;
    }
    size_t units = 0;
    const char32_t c = decode_at(pattern, i, &units);
    if (to_lower(c) != c) return true;
    i += units;
  }
  return false;
}

bool search_case_sensitive(CaseFold fold, std::wstring_view pattern, bool regexp) {
  switch (fold) {
    case CaseFold::kAlways: return false;
    case CaseFold::kNever: return true;
    case CaseFold::kSmart: break;
  }
  return pattern_has_upper_case(pattern, regexp);
}

size_t search_pattern_compiles() {
  return g_compiles;
}

SearchPattern::~SearchPattern() {
  clear();
  if (context_ != nullptr) pcre2_match_context_free_16(context_);
  if (jit_stack_ != nullptr) pcre2_jit_stack_free_16(jit_stack_);
}

void SearchPattern::clear() {
  if (data_ != nullptr) pcre2_match_data_free_16(data_);
  if (all_data_ != nullptr) pcre2_match_data_free_16(all_data_);
  if (code_ != nullptr) pcre2_code_free_16(code_);
  data_ = nullptr;
  all_data_ = nullptr;
  code_ = nullptr;
  compiled_once_ = false;
  expand_ready_ = false;
  pattern_.clear();
  error_.clear();
}

bool SearchPattern::ensure_context() {
  if (context_ != nullptr) return true;
  context_ = pcre2_match_context_create_16(nullptr);
  if (context_ == nullptr) return false;
  jit_stack_ = pcre2_jit_stack_create_16(kJitStackStart, kJitStackMax, nullptr);
  if (jit_stack_ != nullptr) pcre2_jit_stack_assign_16(context_, nullptr, jit_stack_);
  return true;
}

bool SearchPattern::set(std::wstring_view pattern, bool regexp, bool case_sensitive) {
  if (compiled_once_ && pattern == pattern_ && regexp == regexp_ && case_sensitive == case_sensitive_) {
    return valid();
  }
  clear();
  pattern_ = pattern;
  regexp_ = regexp;
  case_sensitive_ = case_sensitive;
  compiled_once_ = true;
  start_dependent_ = regexp && (pattern_.find(L"\\G") != std::wstring::npos ||
                                pattern_.find(L"\\K") != std::wstring::npos ||
                                pattern_.find(L"(*") != std::wstring::npos);
  if (!ensure_context()) {
    error_ = L"Out of memory";
    return false;
  }
  // UTF with MATCH_INVALID_UTF: a lone surrogate in the text is a character
  // that nothing matches, instead of an error for the whole line.
  uint32_t options = PCRE2_UTF | PCRE2_MATCH_INVALID_UTF;
  options |= regexp ? PCRE2_UCP : PCRE2_LITERAL;
  if (!case_sensitive) options |= PCRE2_CASELESS;
  int code = 0;
  PCRE2_SIZE offset = 0;
  ++g_compiles;
  code_ = pcre2_compile_16(sptr(pattern_.c_str()), pattern_.size(), options, &code, &offset, nullptr);
  if (code_ == nullptr) {
    error_ = error_message(code);
    return false;
  }
  pcre2_jit_compile_16(code_, PCRE2_JIT_COMPLETE);  // falls back to the interpreter on failure
  data_ = pcre2_match_data_create_from_pattern_16(code_, nullptr);
  all_data_ = pcre2_match_data_create_from_pattern_16(code_, nullptr);
  if (data_ == nullptr || all_data_ == nullptr) {
    clear();
    compiled_once_ = true;
    pattern_ = pattern;
    error_ = L"Out of memory";
    return false;
  }
  return true;
}

int SearchPattern::match(std::wstring_view subject, size_t start, unsigned options, pcre2_real_match_data_16* data) {
  // An empty view may have no data pointer; PCRE2 wants one.
  const wchar_t* s = subject.data() == nullptr ? L"" : subject.data();
  return pcre2_match_16(code_, sptr(s), subject.size(), start, options, data, context_);
}

void SearchPattern::set_match_error(int rc) {
  error_ = error_message(rc);
}

std::wstring_view SearchPattern::line_text(const PieceTree& text, size_t from, size_t to) {
  std::wstring_view view;
  size_t chunks = 0;
  buf_.clear();
  text.for_each_chunk(from, to - from, [&](std::wstring_view chunk) {
    if (chunks++ == 0) {
      view = chunk;
    } else {
      if (chunks == 2) buf_.assign(view);
      buf_.append(chunk);
    }
    return true;
  });
  return chunks > 1 ? std::wstring_view(buf_) : view;
}

SearchResult SearchPattern::find_forward(const PieceTree& text, size_t from, bool not_empty_at_from,
                                         SearchMatch* match_out, bool for_expand) {
  expand_ready_ = false;
  if (!valid()) return SearchResult::kError;
  from = std::min(from, text.length());
  const size_t first_start = text.line_start(text.line_of(from));
  SearchResult result = SearchResult::kNotFound;
  scan_lines(text, first_start, buf_, [&](size_t line_start, std::wstring_view line) {
    const bool first = line_start == first_start;
    const size_t start = first ? from - line_start : 0;
    const unsigned options = first && not_empty_at_from ? PCRE2_NOTEMPTY_ATSTART : 0;
    const int rc = match(line, start, options, data_);
    if (rc == PCRE2_ERROR_NOMATCH) return true;
    if (rc < 0) {
      set_match_error(rc);
      result = SearchResult::kError;
      return false;
    }
    const PCRE2_SIZE* ov = pcre2_get_ovector_pointer_16(data_);
    match_out->start = line_start + ov[0];
    match_out->end = line_start + ov[1];
    result = SearchResult::kFound;
    if (for_expand) {
      // pcre2_substitute (PCRE2_SUBSTITUTE_MATCHED) refuses a subject other
      // than the one matched ("substitute subject differs from prior match
      // call", measured with 10.49): match the same line again in a copy
      // that stays. It also wants the same start offset and match options
      // ("substitute start offset differs from prior match call",
      // "substitute options differ from prior match call").
      expand_subject_.assign(line);
      expand_line_start_ = line_start;
      expand_start_ = start;
      expand_options_ = options;
      expand_ready_ = match(expand_subject_, start, options, data_) == rc;
    }
    return false;
  });
  return result;
}

SearchResult SearchPattern::find_backward(const PieceTree& text, size_t max_start, size_t end_bound,
                                          SearchMatch* match_out) {
  expand_ready_ = false;  // data_ is reused below
  if (!valid()) return SearchResult::kError;
  max_start = std::min(max_start, text.length());
  end_bound = std::min(end_bound, text.length());
  for (size_t line = text.line_of(max_start);; --line) {
    const size_t line_start = text.line_start(line);
    const size_t line_end = text.line_end(line);
    // S1: the subject ends at end_bound (the search limit). As in Emacs, a
    // match does not extend past the limit, and "$" matches only at the real
    // end of a line, not at a limit inside it (PCRE2_NOTEOL; Emacs's "$"
    // looks at the character after). geditor's own: the limit is the end of
    // the PCRE2 subject, so "\b", "\B", "\z", "\Z" and look-ahead see it as
    // the end of the text (Emacs's "\'" does not match there). A line that
    // starts after the limit has no match that ends at or before it.
    if (line_start <= end_bound) {
      const bool bounded = end_bound < line_end;
      const unsigned options = bounded ? PCRE2_NOTEOL : 0;
      const std::wstring_view subject = line_text(text, line_start, bounded ? end_bound : line_end);
      const SearchResult r = start_dependent_ ? last_match_by_scan(subject, line_start, max_start, options, match_out)
                                              : last_match(subject, line_start, max_start, options, match_out);
      if (r != SearchResult::kNotFound) return r;
    }
    if (line == 0) return SearchResult::kNotFound;
  }
}

SearchResult SearchPattern::last_match_by_scan(std::wstring_view subject, size_t line_start, size_t max_start,
                                               unsigned options, SearchMatch* match_out) {
  bool found = false;
  SearchMatch best;
  size_t start = 0;
  while (start <= subject.size()) {
    const int rc = match(subject, start, options, data_);
    if (rc == PCRE2_ERROR_NOMATCH) break;
    if (rc < 0) {
      set_match_error(rc);
      return SearchResult::kError;
    }
    const PCRE2_SIZE* ov = pcre2_get_ovector_pointer_16(data_);
    const size_t s = line_start + ov[0];
    const size_t e = line_start + ov[1];
    if (s > max_start) break;
    best = SearchMatch{s, e};
    found = true;
    // The next match that starts after this one (matches may overlap).
    if (ov[0] >= subject.size()) break;
    start = next_code_point(subject, ov[0]);
  }
  if (!found) return SearchResult::kNotFound;
  *match_out = best;
  return SearchResult::kFound;
}

// The matches of the forward scan (last_match_by_scan) are the positions
// where a match attempt succeeds, each with the match of that attempt, and
// an attempt does not depend on where the scan started (without \G, \K and
// the verbs; the subject is always the whole line up to the limit, so
// look-behind and ^ see the same text). So a forward scan from any code
// point boundary `lo` finds exactly the scan's matches that start at or
// after `lo`: windows [lo, hi) back from max_start are scanned until one has
// a match (every match ends at or before the limit: the subject ends there).
SearchResult SearchPattern::last_match(std::wstring_view subject, size_t line_start, size_t max_start,
                                       unsigned options, SearchMatch* match_out) {
  // The first match of the line: none, or none at or before max_start, is
  // no match on the line (as for the scan).
  int rc = match(subject, 0, options, data_);
  if (rc == PCRE2_ERROR_NOMATCH) return SearchResult::kNotFound;
  if (rc < 0) {
    set_match_error(rc);
    return SearchResult::kError;
  }
  const size_t first = pcre2_get_ovector_pointer_16(data_)[0];
  if (line_start + first > max_start) return SearchResult::kNotFound;
  // Starts in [lo, hi) of the line are looked at; hi is one past max_start.
  size_t hi = std::min(max_start - line_start, subject.size()) + 1;
  size_t window = kBackwardWindow;
  for (;;) {
    size_t lo = first;
    if (hi - first > window) {
      lo = hi - window;
      // Not inside a surrogate pair (a code point boundary of the scan).
      if (is_low_surrogate(subject[lo]) && is_high_surrogate(subject[lo - 1])) --lo;
    }
    bool found = false;
    SearchMatch best;
    size_t start = lo;
    while (start <= subject.size()) {
      rc = match(subject, start, options, data_);
      if (rc == PCRE2_ERROR_NOMATCH) break;
      if (rc < 0) {
        set_match_error(rc);
        return SearchResult::kError;
      }
      const PCRE2_SIZE* ov = pcre2_get_ovector_pointer_16(data_);
      if (ov[0] >= hi) break;
      best = SearchMatch{line_start + ov[0], line_start + ov[1]};
      found = true;
      if (ov[0] >= subject.size()) break;
      start = next_code_point(subject, ov[0]);
    }
    if (found) {
      *match_out = best;
      return SearchResult::kFound;
    }
    if (lo == first) return SearchResult::kNotFound;
    hi = lo;
    window *= 2;
  }
}

void SearchPattern::find_all(const PieceTree& text, size_t from, size_t to, size_t max,
                             std::vector<SearchMatch>* out) {
  out->clear();
  if (!valid() || max == 0) return;
  to = std::min(to, text.length());
  from = std::min(from, to);
  const size_t last_line = text.line_of(to);
  for (size_t line = text.line_of(from); line <= last_line; ++line) {
    size_t begin = text.line_start(line);
    size_t end = text.line_end(line);
    unsigned options = PCRE2_NOTEMPTY;
    if (end - begin > kLongLine) {
      // A long line: only a window around the visible range.
      const size_t window_begin = from > kLongLineMargin ? from - kLongLineMargin : 0;
      const size_t window_end = to + kLongLineMargin;
      if (window_begin > begin) {
        begin = window_begin;
        options |= PCRE2_NOTBOL;
      }
      if (window_end < end) {
        end = window_end;
        options |= PCRE2_NOTEOL;
      }
    }
    const std::wstring_view subject = line_text(text, begin, end);
    size_t start = 0;
    while (start <= subject.size()) {
      const int rc = match(subject, start, options, all_data_);
      if (rc < 0) break;  // no more matches (or a match limit: no highlight)
      const PCRE2_SIZE* ov = pcre2_get_ovector_pointer_16(all_data_);
      const size_t s = begin + ov[0];
      const size_t e = begin + ov[1];
      if (s >= to) return;
      if (e > from) {
        out->push_back(SearchMatch{s, e});
        if (out->size() >= max) return;
      }
      start = ov[1] > ov[0] ? ov[1] : next_code_point(subject, ov[0]);
    }
  }
}

bool SearchPattern::expand(std::wstring_view replacement, std::wstring* out) {
  if (!regexp_) {
    out->assign(replacement);
    return true;
  }
  if (!valid() || !expand_ready_) {
    error_ = L"no match to replace";
    return false;
  }
  const std::wstring& subject = expand_subject_;
  const std::wstring repl(replacement);
  const uint32_t options = expand_options_ | PCRE2_SUBSTITUTE_MATCHED | PCRE2_SUBSTITUTE_REPLACEMENT_ONLY |
                           PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;
  out->assign(std::max<size_t>(repl.size() + 16, 64), L'\0');
  for (int attempt = 0; attempt < 2; ++attempt) {
    PCRE2_SIZE length = out->size();
    const int rc = pcre2_substitute_16(code_, sptr(subject.c_str()), subject.size(), expand_start_, options, data_,
                                       context_,
                                       sptr(repl.c_str()), repl.size(),
                                       reinterpret_cast<PCRE2_UCHAR16*>(out->data()), &length);
    if (rc >= 0) {
      out->resize(length);
      return true;
    }
    if (rc != PCRE2_ERROR_NOMEMORY || attempt == 1) {
      error_ = error_message(rc);
      out->clear();
      return false;
    }
    out->assign(length + 1, L'\0');  // OVERFLOW_LENGTH: the length needed
  }
  return false;
}

bool SearchPattern::expand_rest_of_line(std::wstring_view replacement, LineReplacement* out) {
  *out = LineReplacement();
  if (!valid() || !expand_ready_) {
    error_ = L"no match to replace";
    return false;
  }
  const std::wstring& s = expand_subject_;
  const size_t line_start = expand_line_start_;
  size_t copied = 0;  // the units of `s` already in out->text (or before it)
  bool ok = true;
  std::wstring piece;
  for (;;) {
    const PCRE2_SIZE* ov = pcre2_get_ovector_pointer_16(data_);
    const size_t ms = ov[0];
    const size_t me = ov[1];
    if (!expand(replacement, &piece)) {
      ok = false;
      break;
    }
    if (out->count == 0) {
      out->from = line_start + ms;
      copied = ms;
    }
    out->text.append(s, copied, ms - copied);
    out->text += piece;
    copied = me;
    ++out->count;
    out->to = line_start + me;
    out->point = out->text.size();
    out->last_empty = ms == me;
    // The next match of the line (data_ then describes it for expand()).
    const unsigned options = ms == me ? PCRE2_NOTEMPTY_ATSTART : 0;
    const int rc = match(s, me, options, data_);
    if (rc == PCRE2_ERROR_NOMATCH) break;
    if (rc < 0) {
      set_match_error(rc);
      ok = false;
      break;
    }
    expand_start_ = me;
    expand_options_ = options;
  }
  expand_ready_ = false;  // data_ no longer holds the match of find_forward
  return ok;
}

}  // namespace geditor
