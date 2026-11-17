#include "layout.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "char_width.h"
#include "unicode.h"

namespace geditor {
namespace {

// Cells of cluster `c` starting at `column` (TAB to the next stop).
int cluster_advance(const Cluster& c, int column, int tab_width) {
  if (c.first == U'\t') return cell_advance(U'\t', column, tab_width);
  return c.width;
}

}  // namespace

int column_at(const PieceTree& tree, size_t offset, int tab_width) {
  const size_t line = tree.line_of(offset);
  const size_t start = tree.line_start(line);
  int column = 0;
  for_each_cluster(tree, start, tree.line_end(line), GraphemeBreaker(), nullptr, [&](const Cluster& c) {
    if (c.offset >= offset) return false;
    column += cluster_advance(c, column, tab_width);
    return true;
  });
  return column;
}

size_t offset_at_column(const PieceTree& tree, size_t line, int column, int tab_width) {
  const size_t start = tree.line_start(line);
  const size_t end = tree.line_end(line);
  size_t result = end;
  int col = 0;
  for_each_cluster(tree, start, end, GraphemeBreaker(), nullptr, [&](const Cluster& c) {
    const int w = cluster_advance(c, col, tab_width);
    if (col + w > column || (w > 0 && col == column)) {
      result = c.offset;
      return false;
    }
    col += w;
    return true;
  });
  return result;
}

size_t next_char_offset(const PieceTree& tree, size_t offset) {
  const size_t n = tree.length();
  if (offset >= n) return n;
  if (is_high_surrogate(tree.char_at(offset)) && offset + 1 < n &&
      is_low_surrogate(tree.char_at(offset + 1))) {
    return offset + 2;
  }
  return offset + 1;
}

size_t prev_char_offset(const PieceTree& tree, size_t offset) {
  if (offset == 0) return 0;
  if (offset >= 2 && is_low_surrogate(tree.char_at(offset - 1)) &&
      is_high_surrogate(tree.char_at(offset - 2))) {
    return offset - 2;
  }
  return offset - 1;
}

char32_t code_point_at(const PieceTree& tree, size_t offset) {
  if (offset >= tree.length()) return 0;
  const wchar_t c = tree.char_at(offset);
  if (is_high_surrogate(c) && offset + 1 < tree.length()) {
    const wchar_t low = tree.char_at(offset + 1);
    if (is_low_surrogate(low)) return combine_surrogates(c, low);
  }
  return static_cast<char32_t>(c);
}

// ---- WrapLayout ----

namespace {

constexpr size_t kMaxLineCaches = 8;
constexpr size_t kNone = ~size_t{0};

}  // namespace

void WrapLayout::configure(int width, int tab_width) {
  width = std::max(0, width);
  tab_width = std::max(1, tab_width);
  if (width == width_ && tab_width == tab_width_) return;
  width_ = width;
  tab_width_ = tab_width;
  caches_.clear();
}

void WrapLayout::sync(const PieceTree& tree) const {
  if (tree.revision() == revision_) return;
  caches_.clear();
  trailing_.clear();
  revision_ = tree.revision();
}

void WrapLayout::note_edit(const PieceTree& tree, size_t offset) {
  if (revision_ + 1 != tree.revision()) {
    caches_.clear();
    trailing_.clear();
    revision_ = tree.revision();
    return;
  }
  revision_ = tree.revision();
  for (size_t i = 0; i < trailing_.size();) {
    TrailingCache& t = trailing_[i];
    if (offset < t.start) {
      trailing_.erase(trailing_.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    // Text before `offset` is unchanged; after it, anything may be. An edit
    // after the line's end leaves the line alone, and the walk back from the
    // line end stops at the line end then.
    t.edited = std::min(t.edited, offset);
    ++i;
  }
  note_wrap_edit(offset);
}

void WrapLayout::note_edit(const PieceTree& tree, size_t offset, size_t inserted, size_t erased) {
  if (revision_ + 1 != tree.revision()) {
    note_edit(tree, offset);  // drops everything
    return;
  }
  revision_ = tree.revision();
  auto is_space = [](wchar_t c) { return is_trailing_whitespace(c); };
  for (size_t i = 0; i < trailing_.size();) {
    TrailingCache& t = trailing_[i];
    if (offset < t.start) {
      trailing_.erase(trailing_.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    ++i;
    if (t.edited != ~size_t{0}) {
      t.edited = std::min(t.edited, offset);  // already waiting for a walk
      continue;
    }
    if (offset > t.end) continue;  // after the line end: the line is unchanged
    if (inserted > 0) {
      // The inserted text is [offset, offset + inserted) now.
      bool line_feed = false;
      size_t last = ~size_t{0};
      for (size_t k = 0; k < inserted; ++k) {
        const wchar_t c = tree.char_at(offset + k);
        ++scanned_;
        if (c == L'\n') {
          line_feed = true;
          break;
        }
        if (!is_space(c)) last = offset + k;
      }
      if (line_feed) {
        t.edited = offset;  // the line was split: walk later
        continue;
      }
      if (t.has_text && t.last_text >= offset) t.last_text += inserted;
      if (last != ~size_t{0} && (!t.has_text || last > t.last_text)) {
        t.has_text = true;
        t.last_text = last;
      }
      t.end += inserted;
      continue;
    }
    if (offset + erased > t.end) {
      t.edited = offset;  // the line end was erased: joined lines, walk later
      continue;
    }
    t.end -= erased;
    if (!t.has_text || t.last_text < offset) continue;
    if (t.last_text >= offset + erased) {
      t.last_text -= erased;
      continue;
    }
    // The last character of text was erased: walk back from the edit over
    // the whitespace before it (the text before the edit is unchanged).
    size_t p = offset;
    while (p > t.start && is_space(tree.char_at(p - 1))) {
      --p;
      ++scanned_;
    }
    t.has_text = p > t.start;
    t.last_text = t.has_text ? p - 1 : 0;
  }
  note_wrap_edit(offset);
}

void WrapLayout::note_wrap_edit(size_t offset) {
  for (size_t i = 0; i < caches_.size();) {
    LineCache& c = caches_[i];
    if (offset < c.start) {
      // Its start (and maybe its line number) moved.
      caches_.erase(caches_.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    if (offset <= c.end) {
      // The states before the edit stay valid: a state is taken before the
      // row break of its cluster is decided, and the segmentation state
      // before a position depends on the text before it only. A state at
      // the edit offset itself is dropped too: inserted text (a combining
      // mark, a low surrogate) could join the cluster before it.
      while (c.states.size() > 1 && c.states.back().offset >= offset) c.states.pop_back();
      if (c.has_break && c.last_break.offset >= offset) c.has_break = false;
      c.complete = false;
    }
    ++i;
  }
}

WrapLayout::LineCache* WrapLayout::cache_for(const PieceTree& tree, size_t line) const {
  const size_t start = tree.line_start(line);
  const size_t end = tree.line_end(line);
  if (end - start < kCheckpointInterval) return nullptr;
  for (LineCache& c : caches_) {
    if (c.line != line) continue;
    if (c.start != start) {
      c.start = start;
      c.states.assign(1, State{start, 0, 0, 0, GraphemeBreaker()});
      c.complete = false;
      c.has_break = false;
    }
    c.end = end;
    c.used = ++clock_;
    return &c;
  }
  if (caches_.size() >= kMaxLineCaches) {
    auto oldest = std::min_element(caches_.begin(), caches_.end(),
                                   [](const LineCache& a, const LineCache& b) { return a.used < b.used; });
    caches_.erase(oldest);
  }
  LineCache c;
  c.line = line;
  c.start = start;
  c.end = end;
  c.states.push_back(State{start, 0, 0, 0, GraphemeBreaker()});
  c.used = ++clock_;
  caches_.push_back(std::move(c));
  return &caches_.back();
}

size_t WrapLayout::trailing_start(const PieceTree& tree, size_t line) const {
  sync(tree);
  const size_t start = tree.line_start(line);
  const size_t end = tree.line_end(line);
  auto is_space = [](wchar_t c) { return is_trailing_whitespace(c); };
  // Walks back from `from` over whitespace, not below `stop`; returns the
  // offset after the last non-whitespace character, or `stop`.
  auto back = [&](size_t from, size_t stop) {
    size_t p = from;
    while (p > stop && is_space(tree.char_at(p - 1))) {
      --p;
      ++scanned_;
    }
    return p;
  };
  TrailingCache* entry = nullptr;
  for (TrailingCache& t : trailing_) {
    if (t.line == line && t.start == start) entry = &t;
  }
  // A line end that does not match the tracked one: walk the whole line.
  if (entry != nullptr && entry->edited == ~size_t{0} && entry->end != end) entry->edited = start;
  if (entry == nullptr) {
    if (trailing_.size() >= kMaxLineCaches) {
      trailing_.erase(std::min_element(trailing_.begin(), trailing_.end(),
                                       [](const TrailingCache& a, const TrailingCache& b) { return a.used < b.used; }));
    }
    TrailingCache t;
    t.line = line;
    t.start = start;
    t.end = end;
    const size_t p = back(end, start);
    t.has_text = p > start;
    t.last_text = t.has_text ? p - 1 : 0;
    trailing_.push_back(t);
    entry = &trailing_.back();
  } else if (entry->edited != ~size_t{0}) {
    // Only [edited, end) may have changed: look there first.
    const size_t edited = std::min(entry->edited, end);
    const size_t p = back(end, edited);
    if (p > edited) {
      entry->has_text = true;
      entry->last_text = p - 1;
    } else if (entry->has_text && entry->last_text >= edited) {
      // [edited, end) is all whitespace now, and the last character of text
      // was at or after the edit: the text before the edit is unchanged, so
      // walk back from there. (Without text before, or with the last one
      // before the edit, nothing before the edit changed: keep it.)
      const size_t q = back(edited, start);
      entry->has_text = q > start;
      entry->last_text = entry->has_text ? q - 1 : 0;
    }
    entry->edited = ~size_t{0};
    entry->end = end;
  }
  entry->used = ++clock_;
  if (!entry->has_text) return start;
  // On a cluster boundary: whitespace inside the cluster of the last
  // character of text (after a Prepend character) is not trailing.
  return next_cluster_offset(tree, entry->last_text);
}

size_t WrapLayout::checkpoint_count(size_t line) const {
  for (const LineCache& c : caches_) {
    if (c.line == line) return c.states.size();
  }
  return 0;
}

const std::vector<WrapLayout::State>* WrapLayout::checkpoints_for_testing(size_t line) const {
  for (const LineCache& c : caches_) {
    if (c.line == line) return &c.states;
  }
  return nullptr;
}

WrapLayout::State WrapLayout::line_start_state(const PieceTree& tree, size_t line) const {
  return State{tree.line_start(line), 0, 0, 0, GraphemeBreaker()};
}

WrapLayout::State WrapLayout::seek(const PieceTree& tree, size_t line, LineCache* cache, size_t row,
                                   int column) const {
  if (cache == nullptr) return line_start_state(tree, line);
  // States are in increasing (row, column) order: the last one not after
  // (row, column).
  auto before = [](const std::pair<size_t, int>& key, const State& s) {
    return key.first != s.row ? key.first < s.row : key.second < s.column;
  };
  const std::pair<size_t, int> key{row, column};
  const auto it = std::upper_bound(cache->states.begin(), cache->states.end(), key, before);
  const State found = it == cache->states.begin() ? cache->states.front() : *(it - 1);
  if (cache->has_break && !before(key, cache->last_break) && cache->last_break.offset > found.offset) {
    return cache->last_break;
  }
  return found;
}

void WrapLayout::skip_lone_clusters(const PieceTree& tree, LineCache* cache, State* s, size_t line_end,
                                    Skip skip) const {
  if (s->offset >= line_end || !s->breaker.lone_cluster_breaks()) return;
  const size_t begin = s->offset;
  size_t next_checkpoint = cache != nullptr ? cache->states.back().offset + kCheckpointInterval : ~size_t{0};
  constexpr GraphemeBreaker kAfter = GraphemeBreaker::after_lone_cluster();
  const int8_t* const widths = lone_cluster_widths();
  auto width_of = [widths](wchar_t u) { return static_cast<int>(widths[static_cast<uint16_t>(u)]); };
  // The state in locals (written back below); `breaker` is s->breaker
  // until the first cluster is placed.
  size_t offset = s->offset;
  size_t row = s->row;
  int column = s->column;
  int64_t logical = s->logical;
  GraphemeBreaker breaker = s->breaker;
  const int width = width_;
  // The first chunk only: the unit after the last one of a chunk is not at
  // hand, and the general walk takes that one.
  std::wstring_view chunk;
  tree.for_each_chunk(offset, line_end - offset, [&chunk](std::wstring_view c) {
    chunk = c;
    return false;
  });
  const wchar_t* const units = chunk.data();
  const size_t n = chunk.size();
  int next_w = n == 0 ? -1 : width_of(units[0]);
  for (size_t i = 0; i + 1 < n; ++i) {
    const int w = next_w;
    next_w = width_of(units[i + 1]);
    if (w < 0 || next_w < 0) break;
    // As walk's step: the state is recorded before the row break.
    const bool row_break = width > 0 && w > 0 && column > 0 && column + w > width;
    if (offset + 1 > skip.end || row + (row_break ? 1 : 0) >= skip.row) break;
    if (offset >= next_checkpoint) [[unlikely]] {
      cache->states.push_back(State{offset, row, column, logical, breaker});
      next_checkpoint = offset + kCheckpointInterval;
    }
    if (row_break) [[unlikely]] {
      if (cache != nullptr) {
        cache->last_break = State{offset, row, column, logical, breaker};
        cache->has_break = true;
      }
      ++row;
      column = 0;
    }
    column += w;
    logical += w;
    ++offset;
    breaker = kAfter;
  }
  s->offset = offset;
  s->row = row;
  s->column = column;
  s->logical = logical;
  s->breaker = breaker;
  scanned_ += offset - begin;
}

template <typename F>
WrapLayout::State WrapLayout::walk(const PieceTree& tree, LineCache* cache, State s, size_t line_end, Skip skip,
                                   F&& f) const {
  auto step = [&](const Cluster& cl) {
    if (cache != nullptr && cl.offset >= cache->states.back().offset + kCheckpointInterval) {
      cache->states.push_back(State{cl.offset, s.row, s.column, s.logical, cl.before});
    }
    int w = 0;
    int logical_w = 0;
    auto row_break = [&] {
      if (cache != nullptr) {
        cache->last_break = State{cl.offset, s.row, s.column, s.logical, cl.before};
        cache->has_break = true;
      }
      ++s.row;
      s.column = 0;
    };
    if (cl.first == U'\t') {
      // A TAB is a cluster of its own (GB4 / GB5).
      if (width_ > 0 && s.column >= width_) row_break();
      w = tab_width_ - s.column % tab_width_;
      if (width_ > 0) w = std::min(w, width_ - s.column);
      logical_w = tab_width_ - static_cast<int>(s.logical % tab_width_);
    } else {
      w = cl.width;
      if (width_ > 0 && w > 0 && s.column > 0 && s.column + w > width_) row_break();
      logical_w = w;
    }
    // (s.breaker is the state of the walk's start only: the states recorded
    // above take cl.before.)
    s.offset = cl.offset;
    const bool go_on = f(cl, s.row, s.column, w, s.logical);
    s.column += w;
    s.logical += logical_w;
    s.offset = cl.offset + cl.length;
    return go_on;
  };
  bool stopped = false;
  if (general_walk_) {
    stopped = s.offset < line_end && !for_each_cluster(tree, s.offset, line_end, s.breaker, &scanned_, step);
  } else {
    // Lone clusters that f would only pass over are placed directly; the
    // general walk takes the rest, and hands back to them at a lone cluster
    // after kGeneralRun clusters (not at every one: each restart looks the
    // offset up in the tree).
    constexpr size_t kGeneralRun = 64;
    while (s.offset < line_end) {
      skip_lone_clusters(tree, cache, &s, line_end, skip);
      if (s.offset >= line_end) break;
      size_t run = 0;
      bool resume = false;
      const bool done = for_each_cluster(tree, s.offset, line_end, s.breaker, &scanned_, [&](const Cluster& cl) {
        if (++run > kGeneralRun && cl.length == 1 && cl.offset + 1 <= skip.end && s.row < skip.row &&
            cl.before.lone_cluster_breaks() && lone_cluster_width(static_cast<wchar_t>(cl.first)) >= 0) {
          resume = true;
          s.offset = cl.offset;
          s.breaker = cl.before;
          return false;
        }
        return step(cl);
      });
      if (done) break;
      if (!resume) {
        stopped = true;
        break;
      }
    }
  }
  if (!stopped && cache != nullptr) {
    cache->complete = true;
    cache->end_state = s;
  }
  return s;
}

size_t WrapLayout::row_count(const PieceTree& tree, size_t line) const {
  sync(tree);
  LineCache* cache = cache_for(tree, line);
  if (cache != nullptr && cache->complete) return cache->end_state.row + 1;
  const State from = cache != nullptr ? cache->states.back() : line_start_state(tree, line);
  const State end = walk(tree, cache, from, tree.line_end(line), Skip{},
                         [](const Cluster&, size_t, int, int, int64_t) { return true; });
  return end.row + 1;
}

bool WrapLayout::has_row(const PieceTree& tree, size_t line, size_t row) const {
  if (row == 0) return true;
  sync(tree);
  LineCache* cache = cache_for(tree, line);
  if (cache != nullptr) {
    if (cache->complete) return row <= cache->end_state.row;
    // A recorded state on a row means a character was placed on it; the
    // last row break walked over starts row last_break.row + 1.
    if (cache->states.back().row >= row) return true;
    if (cache->has_break && cache->last_break.row + 1 >= row) return true;
  }
  const State from = cache != nullptr ? cache->states.back() : line_start_state(tree, line);
  bool found = false;
  walk(tree, cache, from, tree.line_end(line), Skip{~size_t{0}, row}, [&](const Cluster&, size_t r, int, int, int64_t) {
    if (r < row) return true;
    found = true;
    return false;
  });
  return found;
}

RowPos WrapLayout::row_of(const PieceTree& tree, size_t offset, int* column, int64_t* logical_column) const {
  sync(tree);
  offset = std::min(offset, tree.length());
  const size_t line = tree.line_of(offset);
  LineCache* cache = cache_for(tree, line);
  State from = line_start_state(tree, line);
  if (cache != nullptr) {
    const auto it = std::upper_bound(cache->states.begin(), cache->states.end(), offset,
                                     [](size_t off, const State& s) { return off < s.offset; });
    from = it == cache->states.begin() ? cache->states.front() : *(it - 1);
    if (cache->has_break && cache->last_break.offset <= offset && cache->last_break.offset > from.offset) {
      from = cache->last_break;
    }
    if (cache->complete && offset >= cache->end_state.offset) from = cache->end_state;
  }
  bool found = false;
  RowPos pos{line, 0};
  int col = 0;
  int64_t logical = 0;
  const State end = walk(tree, cache, from, tree.line_end(line), Skip{offset, ~size_t{0}},
                         [&](const Cluster& cl, size_t r, int c, int, int64_t lg) {
                           // An offset inside a cluster maps to its start.
                           if (cl.offset + cl.length <= offset) return true;
                           found = true;
                           pos.row = r;
                           col = c;
                           logical = lg;
                           return false;
                         });
  if (!found) {
    pos.row = end.row;
    col = end.column;
    logical = end.logical;
  }
  if (column != nullptr) *column = col;
  if (logical_column != nullptr) *logical_column = logical;
  return pos;
}

size_t WrapLayout::row_start(const PieceTree& tree, size_t line, size_t row) const {
  sync(tree);
  if (row == 0) return tree.line_start(line);
  LineCache* cache = cache_for(tree, line);
  const State from = seek(tree, line, cache, row - 1, std::numeric_limits<int>::max());
  size_t result = kNone;
  const State end = walk(tree, cache, from, tree.line_end(line), Skip{~size_t{0}, row},
                         [&](const Cluster& cl, size_t r, int, int, int64_t) {
                           if (r < row) return true;
                           result = cl.offset;
                           return false;
                         });
  if (result != kNone) return result;
  // No such row: the start of the last one.
  return end.row < row ? row_start(tree, line, end.row) : tree.line_end(line);
}

size_t WrapLayout::offset_at(const PieceTree& tree, size_t line, size_t row, int column) const {
  sync(tree);
  LineCache* cache = cache_for(tree, line);
  const State from = seek(tree, line, cache, row, column);
  const size_t line_end = tree.line_end(line);
  size_t result = kNone;
  size_t last_base = kNone;
  bool next_row_reached = false;
  walk(tree, cache, from, line_end, Skip{~size_t{0}, row}, [&](const Cluster& cl, size_t r, int c, int w, int64_t) {
    if (r < row) return true;
    if (r > row) {
      next_row_reached = true;
      return false;
    }
    if (c + w > column || (w > 0 && c == column)) {
      result = cl.offset;
      return false;
    }
    if (w > 0) last_base = cl.offset;
    return true;
  });
  if (result != kNone) return result;
  if (!next_row_reached) return line_end;
  // Past the end of a row that continues on the next one: its last cluster.
  if (last_base != kNone) return last_base;
  return std::max(tree.line_start(line), prev_cluster_offset(tree, from.offset));
}

RowInfo WrapLayout::row_chars(const PieceTree& tree, size_t line, size_t row, int from_column, int to,
                              std::vector<RowChar>* out) const {
  sync(tree);
  out->clear();
  LineCache* cache = cache_for(tree, line);
  const State from = seek(tree, line, cache, row, from_column);
  RowInfo info;
  int column_end = from.row == row ? from.column : 0;
  bool stopped_at_to = false;
  const State end = walk(tree, cache, from, tree.line_end(line), Skip{~size_t{0}, row},
                         [&](const Cluster& cl, size_t r, int c, int w, int64_t) {
                           if (r < row) return true;
                           if (r > row) {
                             info.complete = true;
                             info.last = false;
                             info.end_column = column_end;
                             info.end = cl.offset;
                             return false;
                           }
                           if (c >= to) {
                             stopped_at_to = true;
                             return false;
                           }
                           if (c + w > from_column || (w == 0 && c >= from_column)) {
                             out->push_back(RowChar{cl.first, cl.offset, c, w, cl.length, cl.code_points, cl.emoji});
                           }
                           column_end = c + w;
                           return true;
                         });
  if (info.complete || stopped_at_to) return info;
  info.complete = true;
  info.last = true;
  info.end_column = end.row == row ? end.column : 0;
  info.end = end.offset;
  return info;
}

bool WrapLayout::next_row(const PieceTree& tree, RowPos* pos) const {
  if (has_row(tree, pos->line, pos->row + 1)) {
    ++pos->row;
    return true;
  }
  if (pos->line + 1 >= tree.line_count()) return false;
  ++pos->line;
  pos->row = 0;
  return true;
}

bool WrapLayout::previous_row(const PieceTree& tree, RowPos* pos) const {
  if (pos->row > 0) {
    --pos->row;
    return true;
  }
  if (pos->line == 0) return false;
  --pos->line;
  pos->row = row_count(tree, pos->line) - 1;
  return true;
}

size_t WrapLayout::forward_rows(const PieceTree& tree, RowPos* pos, size_t n) const {
  size_t moved = 0;
  while (moved < n) {
    const size_t remaining = n - moved;
    // Long lines: look only as far as needed instead of counting all rows.
    if (has_row(tree, pos->line, pos->row + remaining)) {
      pos->row += remaining;
      return n;
    }
    const size_t rows = row_count(tree, pos->line);
    moved += rows - 1 - std::min(pos->row, rows - 1);
    pos->row = rows - 1;
    if (pos->line + 1 >= tree.line_count()) return moved;
    ++pos->line;
    pos->row = 0;
    ++moved;
  }
  return moved;
}

size_t WrapLayout::backward_rows(const PieceTree& tree, RowPos* pos, size_t n) const {
  size_t moved = 0;
  while (moved < n) {
    const size_t remaining = n - moved;
    if (pos->row >= remaining) {
      pos->row -= remaining;
      return n;
    }
    moved += pos->row;
    pos->row = 0;
    if (pos->line == 0) return moved;
    --pos->line;
    pos->row = row_count(tree, pos->line) - 1;
    ++moved;
  }
  return moved;
}

RowPos WrapLayout::last_row(const PieceTree& tree) const {
  const size_t line = tree.line_count() - 1;
  return RowPos{line, row_count(tree, line) - 1};
}

}  // namespace geditor
