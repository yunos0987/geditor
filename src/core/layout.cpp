#include "layout.h"

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

}  // namespace geditor
