#include "grapheme.h"

#include "char_width.h"
#include "unicode.h"

namespace geditor {
namespace {

struct GraphemeRange {
  uint32_t first;
  uint32_t last;
};

#include "grapheme_table.inc"

constexpr uint8_t kClassMask = 0x0F;
constexpr uint8_t kExtPictBit = 0x10;
constexpr unsigned kIncbShift = 5;
constexpr uint8_t kEmojiPresentationBit = 0x80;

// The table value of `c`: the property byte and, in bits 8-9, the cell
// width (char_width, without a binary search).
uint16_t table_value(char32_t c) {
  if (c > 0x10FFFF) return 1u << 8;
  const uint32_t block = kGraphemeStage1[c >> kGraphemeBlockShift];
  return kGraphemeStage2[(block << kGraphemeBlockShift) + (c & ((1u << kGraphemeBlockShift) - 1))];
}

uint8_t properties(char32_t c) {
  // Printable ASCII: Other, no flags (the common case of the layout walk).
  if (c >= 0x20 && c < 0x7F) return 0;
  return static_cast<uint8_t>(table_value(c) & 0xFF);
}

int width_of_value(uint16_t v) {
  return (v >> 8) & 0x3;
}

GraphemeClass class_of(uint8_t p) {
  return static_cast<GraphemeClass>(p & kClassMask);
}

IndicConjunctBreak incb_of(uint8_t p) {
  return static_cast<IndicConjunctBreak>((p >> kIncbShift) & 0x3);
}

bool is_control_class(uint8_t cls) {
  return cls == static_cast<uint8_t>(GraphemeClass::kCR) || cls == static_cast<uint8_t>(GraphemeClass::kLF) ||
         cls == static_cast<uint8_t>(GraphemeClass::kControl);
}

}  // namespace

GraphemeClass grapheme_class(char32_t c) {
  return class_of(properties(c));
}

IndicConjunctBreak indic_conjunct_break(char32_t c) {
  return incb_of(properties(c));
}

bool is_extended_pictographic(char32_t c) {
  return (properties(c) & kExtPictBit) != 0;
}

bool is_emoji_presentation(char32_t c) {
  return (properties(c) & kEmojiPresentationBit) != 0;
}

bool is_emoji_modifier(char32_t c) {
  for (const GraphemeRange& r : kEmojiModifierRanges) {
    if (c >= r.first && c <= r.last) return true;
  }
  return false;
}

bool is_emoji(char32_t c) {
  size_t lo = 0;
  size_t hi = sizeof(kEmojiRanges) / sizeof(kEmojiRanges[0]);
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    if (c < kEmojiRanges[mid].first) {
      hi = mid;
    } else if (c > kEmojiRanges[mid].last) {
      lo = mid + 1;
    } else {
      return true;
    }
  }
  return false;
}

int grapheme_table_width(char32_t c) {
  return width_of_value(table_value(c));
}

const char* grapheme_unicode_version() {
  return kGraphemeUnicodeVersion;
}

// ---- GraphemeBreaker ----

bool GraphemeBreaker::next(char32_t c) {
  using G = GraphemeClass;
  if (c >= 0x20 && c < 0x7F) {
    // Printable ASCII (Other, no flags): only GB9b keeps it with the code
    // point before it. The common case of the layout walk.
    const bool brk = prev_ != static_cast<uint8_t>(G::kPrepend);
    prev_ = static_cast<uint8_t>(G::kOther);
    ri_odd_ = 0;
    emoji_ = 0;
    incb_ = 0;
    return brk;
  }
  const uint8_t p = properties(c);
  const uint8_t cls = p & kClassMask;
  const IndicConjunctBreak incb = incb_of(p);
  const bool ext_pict = (p & kExtPictBit) != 0;
  auto is = [](uint8_t value, G g) { return value == static_cast<uint8_t>(g); };

  bool brk = true;
  if (prev_ == kStart) {
    brk = true;  // GB1
  } else if (is(prev_, G::kCR) && is(cls, G::kLF)) {
    brk = false;  // GB3
  } else if (is_control_class(prev_) || is_control_class(cls)) {
    brk = true;  // GB4, GB5
  } else if (is(prev_, G::kL) && (is(cls, G::kL) || is(cls, G::kV) || is(cls, G::kLV) || is(cls, G::kLVT))) {
    brk = false;  // GB6
  } else if ((is(prev_, G::kLV) || is(prev_, G::kV)) && (is(cls, G::kV) || is(cls, G::kT))) {
    brk = false;  // GB7
  } else if ((is(prev_, G::kLVT) || is(prev_, G::kT)) && is(cls, G::kT)) {
    brk = false;  // GB8
  } else if (is(cls, G::kExtend) || is(cls, G::kZwj)) {
    brk = false;  // GB9
  } else if (is(cls, G::kSpacingMark)) {
    brk = false;  // GB9a
  } else if (is(prev_, G::kPrepend)) {
    brk = false;  // GB9b
  } else if (incb_ != 0 && incb == IndicConjunctBreak::kConsonant) {
    brk = false;  // GB9c (Unicode 18.0: Linker Extend* x Consonant)
  } else if (emoji_ == 2 && ext_pict) {
    brk = false;  // GB11
  } else if (is(prev_, G::kRegionalIndicator) && is(cls, G::kRegionalIndicator) && ri_odd_ != 0) {
    brk = false;  // GB12, GB13
  }

  // The state after c.
  if (is(cls, G::kRegionalIndicator)) {
    ri_odd_ = is(prev_, G::kRegionalIndicator) && ri_odd_ != 0 ? 0 : 1;
  } else {
    ri_odd_ = 0;
  }
  if (ext_pict) {
    emoji_ = 1;
  } else if (is(cls, G::kExtend) && emoji_ == 1) {
    emoji_ = 1;
  } else if (is(cls, G::kZwj) && emoji_ == 1) {
    emoji_ = 2;
  } else {
    emoji_ = 0;
  }
  switch (incb) {
    case IndicConjunctBreak::kLinker: incb_ = 1; break;
    case IndicConjunctBreak::kExtend: break;  // keeps 0 or 1
    case IndicConjunctBreak::kConsonant:
    case IndicConjunctBreak::kNone: incb_ = 0; break;
  }
  prev_ = cls;
  return brk;
}

bool GraphemeBreaker::context_free(char32_t c) {
  const uint8_t p = properties(c);
  const GraphemeClass cls = class_of(p);
  if (cls == GraphemeClass::kExtend || cls == GraphemeClass::kZwj || cls == GraphemeClass::kRegionalIndicator) {
    return false;
  }
  return incb_of(p) != IndicConjunctBreak::kExtend;
}

// ---- ClusterWidth ----

void ClusterWidth::add(char32_t c) {
  if (c >= 0x20 && c < 0x7F) {
    // Printable ASCII: 1 cell, no Emoji_Presentation.
    all_zero_ = false;
    if (count_ == 0) {
      first_width_ = 1;
      first_ = c;
    }
    previous_zwj_ = false;
    ++count_;
    return;
  }
  const uint16_t v = table_value(c);
  const int w = width_of_value(v);
  if (w != 0) all_zero_ = false;
  const uint8_t p = static_cast<uint8_t>(v & 0xFF);
  const bool ri = class_of(p) == GraphemeClass::kRegionalIndicator;
  if (count_ == 0) {
    first_width_ = w;
    first_ = c;
    first_ri_ = ri;
    // A lone regional indicator keeps the table's width (decision G2), though
    // it has Emoji_Presentation; a pair is 2 (below).
    if ((p & kEmojiPresentationBit) != 0 && !first_ri_) emoji_ = true;
  } else {
    // U+FE0F asks for the emoji presentation of an Emoji character only
    // ("g" + U+FE0F stays text).
    if (c == 0xFE0F && is_emoji(first_)) emoji_ = true;
    // U+FE0E right after the first code point asks for its text style, even
    // for an Emoji_Presentation character (PLAN 2: the first code point's
    // width, drawn in one colour).
    if (c == 0xFE0E && count_ == 1) emoji_ = false;
    if (previous_zwj_ && (p & kExtPictBit) != 0) emoji_ = true;
    if (first_ri_ && count_ == 1 && ri) emoji_ = true;
    if (!emoji_ && is_emoji_modifier(c)) emoji_ = true;
  }
  previous_zwj_ = c == 0x200D;
  ++count_;
}

// ---- ClusterBuilder ----

const Cluster* ClusterBuilder::push_general(char32_t c, size_t offset, size_t units) {
  const GraphemeBreaker before = state_;
  const uint16_t v = table_value(c);
  const bool simple = (v & 0xFF) == 0 && state_.ascii_breaks();
  bool brk = true;
  if (simple) {
    // GCB Other without flags (kana, kanji, most letters): like printable
    // ASCII, it starts a cluster and leaves the state of "Other".
    state_.take_ascii();
  } else {
    brk = state_.next(c);
    if (open_ && !brk) {
      Cluster& open = slots_[current_];
      open.length = offset + units - open.offset;
      ++open.code_points;
      width_.add(c);
      return nullptr;
    }
  }
  const Cluster* completed = complete_open();
  Cluster& next = slots_[current_];
  next.offset = offset;
  next.length = units;
  next.first = c;
  next.code_points = 1;
  next.before = before;
  if (simple) {
    width_.start_simple(c, width_of_value(v));
  } else {
    width_ = ClusterWidth();
    width_.add(c);
  }
  open_ = true;
  return completed;
}

// ---- Clusters of a PieceTree ----

namespace {

// The code point that ends at `offset` (offset > 0) and its length in units.
char32_t code_point_before(const PieceTree& tree, size_t offset, size_t* units) {
  const wchar_t low = tree.char_at(offset - 1);
  if (is_low_surrogate(low) && offset >= 2) {
    const wchar_t high = tree.char_at(offset - 2);
    if (is_high_surrogate(high)) {
      *units = 2;
      return combine_surrogates(high, low);
    }
  }
  *units = 1;
  return static_cast<char32_t>(low);
}

char32_t code_point_from(const PieceTree& tree, size_t offset, size_t* units) {
  const wchar_t high = tree.char_at(offset);
  if (is_high_surrogate(high) && offset + 1 < tree.length()) {
    const wchar_t low = tree.char_at(offset + 1);
    if (is_low_surrogate(low)) {
      *units = 2;
      return combine_surrogates(high, low);
    }
  }
  *units = 1;
  return static_cast<char32_t>(high);
}

// A position at or before `offset` whose segmentation state is known without
// looking further back: a line start (the state of the start of a text, so
// that it is a boundary) or the position after a context-free code point.
// That position may or may not be a boundary; feeding the code point there
// tells.
size_t restart_point(const PieceTree& tree, size_t offset, GraphemeBreaker* state, size_t* scanned) {
  size_t p = offset;
  for (;;) {
    if (p == 0) {
      *state = GraphemeBreaker();
      return p;
    }
    size_t units = 0;
    const char32_t c = code_point_before(tree, p, &units);
    if (scanned != nullptr) ++*scanned;
    if (c == U'\n') {
      // After a line feed every rule breaks, as at the start of the text.
      *state = GraphemeBreaker();
      return p;
    }
    if (GraphemeBreaker::context_free(c)) {
      GraphemeBreaker s;
      s.next(c);
      *state = s;
      return p;
    }
    p -= units;
  }
}

// The cluster holding the unit at `offset` (< length): its start, its end
// and the segmentation state before its start.
void bounds(const PieceTree& tree, size_t offset, size_t* start, size_t* end, GraphemeBreaker* before,
            size_t* scanned) {
  const size_t n = tree.length();
  // The unit at `offset` may be the second half of a surrogate pair.
  if (offset > 0 && is_low_surrogate(tree.char_at(offset)) && is_high_surrogate(tree.char_at(offset - 1))) {
    --offset;
  }
  size_t target = offset;
  bool have_end = false;
  // How many code points the next pass starts further back. It doubles with
  // every pass, so a long run that joins its neighbours (SpacingMark, Hangul
  // jamo, Prepend, ZWJ chains: their code points are context-free but do
  // not break) costs O(k) for k code points, not O(k^2) (review R1).
  size_t back = 1;
  for (;;) {
    GraphemeBreaker state;
    const size_t q = restart_point(tree, target, &state, scanned);
    // Feed from q until the first boundary after `offset` (or the end; once
    // the end is known, only up to `offset`).
    size_t p = q;
    bool found_start = false;
    while (p < n && !(have_end && p > offset)) {
      size_t units = 0;
      const char32_t c = code_point_from(tree, p, &units);
      if (scanned != nullptr) ++*scanned;
      const GraphemeBreaker state_before = state;
      const bool boundary = state.next(c);
      if (boundary && p > offset) {
        if (!have_end) {
          *end = p;
          have_end = true;
        }
        break;
      }
      if (boundary) {
        *start = p;
        *before = state_before;
        found_start = true;
      }
      p += units;
    }
    if (!have_end) {
      *end = n;
      have_end = true;
    }
    if (found_start) return;
    // No boundary in [q, offset]: the cluster also holds the code point
    // before q (q is no line start then). Look `back` code points further.
    target = q;
    for (size_t i = 0; i < back && target > 0; ++i) {
      size_t units = 0;
      code_point_before(tree, target, &units);
      target -= units;
      if (scanned != nullptr) ++*scanned;
    }
    back *= 2;
  }
}

}  // namespace

size_t cluster_start(const PieceTree& tree, size_t offset, size_t* scanned) {
  const size_t n = tree.length();
  if (offset >= n) return n;
  if (offset == 0) return 0;
  size_t start = 0;
  size_t end = 0;
  GraphemeBreaker before;
  bounds(tree, offset, &start, &end, &before, scanned);
  return start;
}

size_t next_cluster_offset(const PieceTree& tree, size_t offset, size_t* scanned) {
  const size_t n = tree.length();
  if (offset >= n) return n;
  size_t start = 0;
  size_t end = 0;
  GraphemeBreaker before;
  bounds(tree, offset, &start, &end, &before, scanned);
  return end;
}

size_t prev_cluster_offset(const PieceTree& tree, size_t offset, size_t* scanned) {
  if (offset == 0) return 0;
  const size_t n = tree.length();
  if (offset > n) offset = n;
  size_t start = 0;
  size_t end = 0;
  GraphemeBreaker before;
  bounds(tree, offset - 1, &start, &end, &before, scanned);
  return start;
}

Cluster cluster_at(const PieceTree& tree, size_t offset, size_t* scanned) {
  Cluster c;
  if (offset >= tree.length()) {
    c.offset = tree.length();
    return c;
  }
  size_t start = 0;
  size_t end = 0;
  GraphemeBreaker before;
  bounds(tree, offset, &start, &end, &before, scanned);
  // Measure it (its code points only).
  ClusterBuilder builder(before);
  size_t p = start;
  Cluster done;
  while (p < end) {
    size_t units = 0;
    const char32_t cp = code_point_from(tree, p, &units);
    builder.feed(cp, p, units, &done);
    p += units;
  }
  builder.finish(&c);
  return c;
}

}  // namespace geditor
