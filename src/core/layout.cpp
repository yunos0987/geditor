#include "layout.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "char_width.h"
#include "unicode.h"

namespace geditor {
namespace {

// Calls f(code_point, offset, units) for each code point in [begin, end).
// f returns false to stop.
template <typename F>
void for_each_code_point(const PieceTree& tree, size_t begin, size_t end, F&& f) {
  if (end <= begin) return;
  wchar_t pending = 0;
  size_t offset = begin;
  bool stop = false;
  tree.for_each_chunk(begin, end - begin, [&](std::wstring_view chunk) {
    for (const wchar_t w : chunk) {
      if (pending != 0) {
        const wchar_t high = pending;
        pending = 0;
        if (is_low_surrogate(w)) {
          if (!f(combine_surrogates(high, w), offset, size_t{2})) {
            stop = true;
            return false;
          }
          offset += 2;
          continue;
        }
        if (!f(static_cast<char32_t>(high), offset, size_t{1})) {
          stop = true;
          return false;
        }
        offset += 1;
      }
      if (is_high_surrogate(w)) {
        pending = w;
        continue;
      }
      if (!f(static_cast<char32_t>(w), offset, size_t{1})) {
        stop = true;
        return false;
      }
      offset += 1;
    }
    return true;
  });
  if (!stop && pending != 0) f(static_cast<char32_t>(pending), offset, size_t{1});
}

}  // namespace

int column_at(const PieceTree& tree, size_t offset, int tab_width) {
  const size_t start = tree.line_start(tree.line_of(offset));
  int column = 0;
  for_each_code_point(tree, start, offset, [&](char32_t c, size_t, size_t) {
    column += cell_advance(c, column, tab_width);
    return true;
  });
  return column;
}

size_t offset_at_column(const PieceTree& tree, size_t line, int column, int tab_width) {
  const size_t start = tree.line_start(line);
  const size_t end = tree.line_end(line);
  size_t result = end;
  int col = 0;
  for_each_code_point(tree, start, end, [&](char32_t c, size_t off, size_t) {
    const int w = cell_advance(c, col, tab_width);
    if (col + w > column || (w > 0 && col == column)) {
      result = off;
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

int cells_of(char32_t c) {
  if (c >= 0x20 && c < 0x7F) return 1;
  return char_width(c);
}

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
  auto is_space = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'　'; };
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
      // The states before the edit stay valid. A state at the edit offset
      // itself is dropped too: an inserted low surrogate could join the
      // character before it.
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
      c.states.assign(1, State{start, 0, 0, 0});
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
  c.states.push_back(State{start, 0, 0, 0});
  c.used = ++clock_;
  caches_.push_back(std::move(c));
  return &caches_.back();
}

size_t WrapLayout::trailing_start(const PieceTree& tree, size_t line) const {
  sync(tree);
  const size_t start = tree.line_start(line);
  const size_t end = tree.line_end(line);
  auto is_space = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'　'; };
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
  return entry->has_text ? entry->last_text + 1 : start;
}

size_t WrapLayout::checkpoint_count(size_t line) const {
  for (const LineCache& c : caches_) {
    if (c.line == line) return c.states.size();
  }
  return 0;
}

WrapLayout::State WrapLayout::line_start_state(const PieceTree& tree, size_t line) const {
  return State{tree.line_start(line), 0, 0, 0};
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

template <typename F>
WrapLayout::State WrapLayout::walk(const PieceTree& tree, LineCache* cache, State s, size_t line_end,
                                   F&& f) const {
  bool stopped = false;
  auto step = [&](char32_t c, size_t offset, size_t units) {
    if (cache != nullptr && offset >= cache->states.back().offset + kCheckpointInterval) {
      cache->states.push_back(State{offset, s.row, s.column, s.logical});
    }
    ++scanned_;
    int w = 0;
    int logical_w = 0;
    auto row_break = [&] {
      if (cache != nullptr) {
        cache->last_break = State{offset, s.row, s.column, s.logical};
        cache->has_break = true;
      }
      ++s.row;
      s.column = 0;
    };
    if (c == U'\t') {
      if (width_ > 0 && s.column >= width_) row_break();
      w = tab_width_ - s.column % tab_width_;
      if (width_ > 0) w = std::min(w, width_ - s.column);
      logical_w = tab_width_ - static_cast<int>(s.logical % tab_width_);
    } else {
      w = cells_of(c);
      if (width_ > 0 && w > 0 && s.column > 0 && s.column + w > width_) row_break();
      logical_w = w;
    }
    s.offset = offset;
    const bool go_on = f(c, offset, s.row, s.column, w, s.logical);
    s.column += w;
    s.logical += logical_w;
    s.offset = offset + units;
    return go_on;
  };
  if (s.offset < line_end) {
    wchar_t pending = 0;
    size_t offset = s.offset;
    tree.for_each_chunk(s.offset, line_end - s.offset, [&](std::wstring_view chunk) {
      for (const wchar_t u : chunk) {
        if (pending != 0) {
          const wchar_t high = pending;
          pending = 0;
          if (is_low_surrogate(u)) {
            if (!step(combine_surrogates(high, u), offset, 2)) {
              stopped = true;
              return false;
            }
            offset += 2;
            continue;
          }
          if (!step(static_cast<char32_t>(high), offset, 1)) {
            stopped = true;
            return false;
          }
          offset += 1;
        }
        if (is_high_surrogate(u)) {
          pending = u;
          continue;
        }
        if (!step(static_cast<char32_t>(u), offset, 1)) {
          stopped = true;
          return false;
        }
        offset += 1;
      }
      return true;
    });
    if (!stopped && pending != 0 && !step(static_cast<char32_t>(pending), offset, 1)) stopped = true;
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
  const State end = walk(tree, cache, from, tree.line_end(line),
                         [](char32_t, size_t, size_t, int, int, int64_t) { return true; });
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
  walk(tree, cache, from, tree.line_end(line), [&](char32_t, size_t, size_t r, int, int, int64_t) {
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
  const State end = walk(tree, cache, from, tree.line_end(line),
                         [&](char32_t, size_t off, size_t r, int c, int, int64_t lg) {
                           if (off < offset) return true;
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
  const State end = walk(tree, cache, from, tree.line_end(line),
                         [&](char32_t, size_t off, size_t r, int, int, int64_t) {
                           if (r < row) return true;
                           result = off;
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
  walk(tree, cache, from, line_end, [&](char32_t, size_t off, size_t r, int c, int w, int64_t) {
    if (r < row) return true;
    if (r > row) {
      next_row_reached = true;
      return false;
    }
    if (c + w > column || (w > 0 && c == column)) {
      result = off;
      return false;
    }
    if (w > 0) last_base = off;
    return true;
  });
  if (result != kNone) return result;
  if (!next_row_reached) return line_end;
  // Past the end of a row that continues on the next one: its last character.
  if (last_base != kNone) return last_base;
  return std::max(tree.line_start(line), prev_char_offset(tree, from.offset));
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
  const State end = walk(tree, cache, from, tree.line_end(line),
                         [&](char32_t ch, size_t off, size_t r, int c, int w, int64_t) {
                           if (r < row) return true;
                           if (r > row) {
                             info.complete = true;
                             info.last = false;
                             info.end_column = column_end;
                             info.end = off;
                             return false;
                           }
                           if (c >= to) {
                             stopped_at_to = true;
                             return false;
                           }
                           if (c + w > from_column || (w == 0 && c >= from_column)) {
                             out->push_back(RowChar{ch, off, c, w});
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
