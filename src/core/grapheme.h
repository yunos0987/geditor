// Extended grapheme clusters (Unicode UAX #29, rules GB1-GB999 including GB9c)
// and their width in cells (docs/PLAN.md 2 "cursor unit" and "column
// counting"). The properties come from src/core/grapheme_table.inc, which
// tools/gen_char_width.py generates from the same Unicode version as the
// width table. This is the one segmentation of geditor: layout, movement,
// deletion, the mouse, the display rows and the renderer all take their
// cluster boundaries and widths from here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "piece_tree.h"

namespace geditor {

// Grapheme_Cluster_Break values (the order of the generated table).
enum class GraphemeClass : uint8_t {
  kOther,
  kCR,
  kLF,
  kControl,
  kExtend,
  kZwj,
  kRegionalIndicator,
  kPrepend,
  kSpacingMark,
  kL,
  kV,
  kT,
  kLV,
  kLVT,
};

// Indic_Conjunct_Break values.
enum class IndicConjunctBreak : uint8_t { kNone, kLinker, kConsonant, kExtend };

GraphemeClass grapheme_class(char32_t c);
IndicConjunctBreak indic_conjunct_break(char32_t c);
bool is_extended_pictographic(char32_t c);
bool is_emoji_presentation(char32_t c);
bool is_emoji_modifier(char32_t c);
// The Emoji property (emoji-data.txt): characters that U+FE0F can show as
// emoji.
bool is_emoji(char32_t c);
// The cell width that the grapheme table holds for `c` (the cluster widths
// read it instead of searching the width table; tests check that the two
// agree for every code point).
int grapheme_table_width(char32_t c);
// Unicode version of the generated grapheme table, such as "18.0.0".
const char* grapheme_unicode_version();

// The segmentation state between two code points. next() takes the code
// points in order and says where clusters break.
class GraphemeBreaker {
 public:
  // True when a cluster boundary comes before `c` (always for the first code
  // point fed); then takes `c` in.
  bool next(char32_t c);
  // True when the state after `c` does not depend on the code points before
  // it (c is not Extend, ZWJ or a regional indicator, and does not have
  // Indic_Conjunct_Break=Extend). A backward search may start right after
  // such a code point, with the state of a fresh breaker that was fed `c`
  // alone.
  static bool context_free(char32_t c);
  // For walks over lone clusters (lone_cluster_width): true when one fed now
  // starts a cluster (the code point before is no Prepend character), and
  // the state after one (that of any lone cluster).
  bool lone_cluster_breaks() const { return ascii_breaks(); }
  static constexpr GraphemeBreaker after_lone_cluster() { return after_other(); }

  friend bool operator==(const GraphemeBreaker&, const GraphemeBreaker&) = default;

 private:
  friend class ClusterBuilder;
  // Printable ASCII after anything but a Prepend character starts a cluster
  // (GB999), and the state after it is that of any "Other" code point.
  bool ascii_breaks() const { return prev_ != static_cast<uint8_t>(GraphemeClass::kPrepend); }
  void take_ascii() { *this = after_other(); }
  static constexpr GraphemeBreaker after_other() {
    GraphemeBreaker b;
    b.prev_ = static_cast<uint8_t>(GraphemeClass::kOther);
    return b;
  }

  static constexpr uint8_t kStart = 0xFF;  // nothing fed yet (GB1)
  uint8_t prev_ = kStart;  // GraphemeClass of the last code point
  uint8_t ri_odd_ = 0;     // an odd number of regional indicators ends here (GB12 / GB13)
  uint8_t emoji_ = 0;      // 1: ExtPict Extend* ends here, 2: ExtPict Extend* ZWJ (GB11)
  uint8_t incb_ = 0;       // 1: InCB Linker, InCB Extend* ends here (GB9c of Unicode 18.0)
};

// The width rules of a cluster (PLAN 2 "column counting"): 2 cells when it
// is shown as an emoji (U+FE0F after its first code point, an emoji
// modifier, a ZWJ followed by an Extended_Pictographic character, a pair of
// regional indicators, or a first code point with Emoji_Presentation other
// than a regional indicator);
// otherwise the width of its first code point (char_width: 0, 1 or 2). A
// cluster of zero-width characters only is 0. TAB is 1 here; callers advance
// it to the next tab stop. U+FE0F counts after an Emoji character only (the
// characters of emoji-data.txt that it can show as emoji): "g" + U+FE0F
// stays text.
class ClusterWidth {
 public:
  void add(char32_t c);
  int width() const {
    if (count_ == 0 || all_zero_) return 0;
    return emoji_ ? 2 : first_width_;
  }
  // True when the cluster is shown as an emoji (one of the rules above).
  bool emoji() const { return count_ > 0 && !all_zero_ && emoji_; }

 private:
  friend class ClusterBuilder;
  // Starts again with printable ASCII `c` (1 cell).
  void start_ascii(char32_t c) { start_simple(c, 1); }
  // Starts again with `c` of `width` cells, which has no
  // Grapheme_Cluster_Break value and no emoji property in the table.
  void start_simple(char32_t c, int width) {
    first_ = c;
    first_width_ = width;
    all_zero_ = width == 0;
    emoji_ = false;
    previous_zwj_ = false;
    first_ri_ = false;
    count_ = 1;
  }

  int first_width_ = -1;      // -1: nothing added yet
  char32_t first_ = 0;
  bool all_zero_ = true;
  bool emoji_ = false;        // one of the emoji rules applies
  bool previous_zwj_ = false;
  bool first_ri_ = false;
  int count_ = 0;
};

// The unit `u` alone is a cluster when the unit after it is such a unit
// too and the state before it is lone_cluster_breaks(): printable ASCII and
// the BMP characters without a Grapheme_Cluster_Break value and without the
// properties of the table's flags (most letters, kana and kanji), as
// ClusterBuilder takes them. Returns its cell width (the cluster's width),
// or -1 for any other unit (surrogates, controls, TAB, marks, emoji...).
int lone_cluster_width(wchar_t u);
// lone_cluster_width of every UTF-16 unit, indexed by the unit (64K
// entries, made on the first call).
const int8_t* lone_cluster_widths();

// One cluster of a text: [offset, offset + length) in UTF-16 units.
struct Cluster {
  size_t offset = 0;
  size_t length = 0;
  char32_t first = 0;      // its first code point
  int width = 0;           // ClusterWidth (TAB: 1)
  int code_points = 0;
  GraphemeBreaker before;  // the segmentation state before its first code point
  bool emoji = false;      // shown as an emoji (ClusterWidth::emoji)
};

// Groups code points into clusters. Feed every code point of a text in order
// with its offset and length in UTF-16 units, then call finish().
class ClusterBuilder {
 public:
  ClusterBuilder() = default;
  // Starts at a cluster boundary whose segmentation state is `state`.
  explicit ClusterBuilder(const GraphemeBreaker& state) : state_(state) {}

  // Takes in `c`; true when the cluster before it is complete (in *done).
  bool feed(char32_t c, size_t offset, size_t units, Cluster* done) {
    const Cluster* completed = push(c, offset, units);
    if (completed != nullptr) *done = *completed;
    return completed != nullptr;
  }
  // True when a cluster was open (in *done).
  bool finish(Cluster* done) {
    const Cluster* completed = close();
    if (completed != nullptr) *done = *completed;
    return completed != nullptr;
  }

  // feed / finish without a copy: the completed cluster, valid until the
  // next call (nullptr: none). Printable ASCII that starts a cluster is
  // handled here inline (the common case of long lines).
  const Cluster* push(char32_t c, size_t offset, size_t units) {
    if (c < 0x20 || c >= 0x7F || !state_.ascii_breaks()) return push_general(c, offset, units);
    const GraphemeBreaker before = state_;
    state_.take_ascii();
    const Cluster* completed = complete_open();
    Cluster& next = slots_[current_];
    next.offset = offset;
    next.length = 1;
    next.first = c;
    next.code_points = 1;
    next.before = before;
    width_.start_ascii(c);
    open_ = true;
    return completed;
  }
  const Cluster* close() {
    const Cluster* completed = complete_open();
    open_ = false;
    return completed;
  }
  // Printable ASCII `c` at `offset` that the next code point (printable
  // ASCII too) is known to end: when `c` starts a cluster, *open receives
  // the open cluster (nullptr when there was none) and *single the
  // one-unit cluster of `c` (both valid until the next call). False
  // (nothing taken) after a Prepend character.
  bool take_ascii_single(char32_t c, size_t offset, const Cluster** open, const Cluster** single) {
    if (!state_.ascii_breaks()) return false;
    *open = complete_open();
    open_ = false;
    ascii_.offset = offset;
    ascii_.first = c;
    ascii_.before = state_;
    *single = &ascii_;
    state_.take_ascii();
    return true;
  }

 private:
  // Measures the open cluster and moves on to the other slot; the measured
  // cluster (nullptr when none was open).
  const Cluster* complete_open() {
    if (!open_) return nullptr;
    Cluster& done = slots_[current_];
    done.width = width_.width();
    done.emoji = width_.emoji();
    current_ ^= 1;
    return &done;
  }
  const Cluster* push_general(char32_t c, size_t offset, size_t units);

  GraphemeBreaker state_;
  bool open_ = false;
  // Two clusters: the open one (slots_[current_]) and the one completed last.
  Cluster slots_[2];
  int current_ = 0;
  // The one-unit clusters of take_ascii_single (only offset, first and
  // before change).
  Cluster ascii_{0, 1, 0, 1, 1, GraphemeBreaker(), false};
  ClusterWidth width_;
};

// Calls f(const Cluster&) for each cluster of `s` (offsets within `s`); f
// returns false to stop.
template <typename F>
void for_each_cluster(std::wstring_view s, F&& f) {
  ClusterBuilder builder;
  Cluster done;
  size_t i = 0;
  while (i < s.size()) {
    char32_t c = static_cast<char32_t>(s[i]);
    size_t units = 1;
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<char32_t>(s[i + 1]) - 0xDC00);
      units = 2;
    }
    if (builder.feed(c, i, units, &done) && !f(static_cast<const Cluster&>(done))) return;
    i += units;
  }
  if (builder.finish(&done)) f(static_cast<const Cluster&>(done));
}

// Calls f(const Cluster&) for each cluster of [begin, end) of `tree`.
// `begin` must be a cluster boundary whose segmentation state is `state`
// (GraphemeBreaker() at a line start) and `end` a boundary too (a line end or
// the end of the text). f returns false to stop; returns false when stopped.
// `scanned` (when not null) is increased by the UTF-16 units read.
template <typename F>
bool for_each_cluster(const PieceTree& tree, size_t begin, size_t end, const GraphemeBreaker& state,
                      size_t* scanned, F&& f) {
  if (end <= begin) return true;
  ClusterBuilder builder(state);
  bool stopped = false;
  wchar_t pending = 0;
  size_t offset = begin;
  auto feed = [&](char32_t c, size_t units) {
    const Cluster* done = builder.push(c, offset, units);
    if (done != nullptr && !f(*done)) return false;
    offset += units;
    return true;
  };
  tree.for_each_chunk(begin, end - begin, [&](std::wstring_view chunk) {
    const size_t n = chunk.size();
    for (size_t i = 0; i < n; ++i) {
      const wchar_t u = chunk[i];
      // Runs of printable ASCII (most of a typical long line): a character
      // followed by another one is a cluster of its own.
      if (pending == 0 && u >= 0x20 && u < 0x7F && i + 1 < n && chunk[i + 1] >= 0x20 && chunk[i + 1] < 0x7F) {
        const Cluster* open = nullptr;
        const Cluster* single = nullptr;
        if (builder.take_ascii_single(static_cast<char32_t>(u), offset, &open, &single)) {
          if ((open != nullptr && !f(*open)) || !f(*single)) {
            stopped = true;
            return false;
          }
          ++offset;
          continue;
        }
      }
      char32_t c = static_cast<char32_t>(u);
      size_t units = 1;
      if (pending != 0) {
        const wchar_t high = pending;
        pending = 0;
        if (u >= 0xDC00 && u <= 0xDFFF) {
          c = 0x10000 + ((static_cast<char32_t>(high) - 0xD800) << 10) + (c - 0xDC00);
          units = 2;
        } else if (!feed(static_cast<char32_t>(high), 1)) {  // a lone high surrogate
          stopped = true;
          return false;
        }
      }
      if (units == 1 && u >= 0xD800 && u <= 0xDBFF) {
        pending = u;
        continue;
      }
      if (!feed(c, units)) {
        stopped = true;
        return false;
      }
    }
    return true;
  });
  if (!stopped && pending != 0 && !feed(static_cast<char32_t>(pending), 1)) stopped = true;
  if (!stopped) {
    const Cluster* last = builder.close();
    if (last != nullptr && !f(*last)) stopped = true;
  }
  // Counted once here (a count per code point through the pointer costs the
  // walk of a long line about a tenth of its time): the units taken in, the
  // one read ahead of a stop included.
  if (scanned != nullptr) *scanned += offset - begin + (stopped ? 1 : 0);
  return !stopped;
}

// Clusters of a buffer. Each function looks back only to the nearest code
// point after which the state is context-free (or the line start) and forward
// to the end of the cluster: a few code points for ordinary text. `scanned`
// (when not null) is increased by the code points examined.
//
// The cluster boundary at or before `offset` (offset itself when it is one;
// 0 and the end of the text always are).
size_t cluster_start(const PieceTree& tree, size_t offset, size_t* scanned = nullptr);
// The boundary after the cluster that holds the unit at `offset` (the end of
// the text at or after it).
size_t next_cluster_offset(const PieceTree& tree, size_t offset, size_t* scanned = nullptr);
// The boundary before `offset` (0 at 0): the start of the cluster that holds
// the unit before `offset`.
size_t prev_cluster_offset(const PieceTree& tree, size_t offset, size_t* scanned = nullptr);
// The cluster that holds the unit at `offset` (offset < tree.length()).
Cluster cluster_at(const PieceTree& tree, size_t offset, size_t* scanned = nullptr);

}  // namespace geditor
