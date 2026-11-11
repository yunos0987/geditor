#include "char_width.h"

#include <cstddef>
#include <cstdint>

namespace geditor {
namespace {

struct CharWidthRange {
  uint32_t first;
  uint32_t last;
  uint8_t width;
};

#include "char_width_table.inc"

}  // namespace

int char_width(char32_t c) {
  if (c >= 0x20 && c < 0x7F) return 1;
  size_t lo = 0;
  size_t hi = sizeof(kCharWidthRanges) / sizeof(kCharWidthRanges[0]);
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    const CharWidthRange& r = kCharWidthRanges[mid];
    if (c < r.first) {
      hi = mid;
    } else if (c > r.last) {
      lo = mid + 1;
    } else {
      return r.width;
    }
  }
  return 1;
}

int cell_advance(char32_t c, int column, int tab_width) {
  if (c == U'\t') {
    const int w = tab_width > 0 ? tab_width : 1;
    return w - column % w;
  }
  return char_width(c);
}

const char* char_width_unicode_version() {
  return kUnicodeVersion;
}

}  // namespace geditor
