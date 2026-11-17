// Cell-grid positions within a logical line, and the wrapping of logical
// lines into screen rows (docs/PLAN.md 4.5 / 4.7). The unit is the extended
// grapheme cluster (grapheme.h): columns are counted in cells per cluster
// (ClusterWidth), TAB advances to the next stop, and no position, row break
// or column falls inside a cluster.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "grapheme.h"
#include "piece_tree.h"

namespace geditor {

// Column (cells) of `offset` from the start of its line: the cells of the
// clusters that start before it.
int column_at(const PieceTree& tree, size_t offset, int tab_width);

// Offset in `line` of the cluster at `column`: the last cluster start whose
// column does not exceed `column` (a wide cluster that spans `column` is not
// entered), clamped to the end of the line. Zero-width clusters after that
// position are skipped so that the cursor does not land before them.
size_t offset_at_column(const PieceTree& tree, size_t line, int column, int tab_width);

// Offset of the next / previous code point (surrogate pairs are not split).
// Cursor motion uses the cluster functions of grapheme.h instead.
size_t next_char_offset(const PieceTree& tree, size_t offset);
size_t prev_char_offset(const PieceTree& tree, size_t offset);
// Code point starting at `offset` (0 at the end of the text).
char32_t code_point_at(const PieceTree& tree, size_t offset);

// The whitespace of trailing-whitespace marks (PLAN 4.7): space, TAB and
// U+3000. The one definition for WrapLayout::trailing_start, its updates
// after edits, trailing_whitespace_start and build_display_row (deferred fix
// E3).
inline bool is_trailing_whitespace(char32_t c) {
  return c == U' ' || c == U'\t' || c == U'\x3000';
}

// A screen row: row `row` (0-based) of logical line `line`.
struct RowPos {
  size_t line = 0;
  size_t row = 0;

  friend bool operator==(const RowPos&, const RowPos&) = default;
  friend bool operator<(const RowPos& a, const RowPos& b) {
    return a.line != b.line ? a.line < b.line : a.row < b.row;
  }
};

// One cluster of a screen row.
struct RowChar {
  char32_t c = 0;      // its first code point
  size_t offset = 0;
  int column = 0;      // cell column within the row
  int width = 0;       // cells (0 for zero-width clusters)
  size_t length = 1;   // UTF-16 units of the cluster
  int code_points = 1;
  bool emoji = false;  // shown as an emoji (Cluster::emoji)
};

// What row_chars found about the row.
struct RowInfo {
  bool complete = false;  // the walk reached the end of the row
  // Valid when complete:
  bool last = false;      // the row ends at the end of the logical line
  int end_column = 0;     // column after the row's last character
  size_t end = 0;         // offset after the row's last character
};

// Wrapping of logical lines into screen rows on the cell grid, the one
// implementation used by drawing, vertical motion, scrolling and the scroll
// bar. Width 0 means no wrapping (one row per logical line).
//
// Rules (decision E3, by cluster since M3c): wrapping is per cluster on cell
// boundaries; a cluster goes to the next row when it does not fit in the
// cells left on its row (a 2-cell cluster with 1 cell left goes to the next
// row); zero-width clusters stay on the row of the cluster before them; a TAB
// never starts a new row unless its row is full, and is cut to the cells left
// (tab stops count from the start of the row); a row always holds at least
// one cluster, so a cluster wider than the width gets a row of its own.
//
// Long lines (decision of PLAN 4.7): for a logical line of at least
// kCheckpointInterval units the wrap state is recorded every
// kCheckpointInterval units (at the next cluster start) as the line is
// walked, and later walks start from the nearest recorded state instead of
// the start of the line. A state holds the segmentation state too, so
// segmentation runs only from the recorded state to the rows needed. Edits
// reported through note_edit keep the states before the edit; any other
// change of the text (seen through PieceTree::revision) drops them all.
// Runs of lone clusters (grapheme.h lone_cluster_width: most text) that a
// walk only passes over are placed unit by unit without building clusters,
// with the same rows, columns and recorded states as the general walk
// (tests/test_layout_lone.cpp compares the two).
class WrapLayout {
 public:
  static constexpr size_t kCheckpointInterval = 65536;

  // Sets the wrap width (0: none) and the tab width; drops the recorded
  // states when either changes.
  void configure(int width, int tab_width);
  int width() const { return width_; }
  int tab_width() const { return tab_width_; }
  // Called after each change of `tree` (one insert or erase at `offset`).
  // With the sizes (`inserted` units inserted, or `erased` units erased)
  // the trailing-whitespace values are updated from the edit alone.
  void note_edit(const PieceTree& tree, size_t offset);
  void note_edit(const PieceTree& tree, size_t offset, size_t inserted, size_t erased);

  // Rows of logical line `line` (at least 1).
  size_t row_count(const PieceTree& tree, size_t line) const;
  bool has_row(const PieceTree& tree, size_t line, size_t row) const;
  // The screen row of `offset` and its cell column within that row. The
  // offset of a row's first character belongs to that row; the end of a
  // line belongs to its last row (column = the width when that row is full,
  // the reserved cell of PLAN 4.5). `logical_column` (when not null) receives
  // the column from the start of the logical line (as column_at).
  RowPos row_of(const PieceTree& tree, size_t offset, int* column,
                int64_t* logical_column = nullptr) const;
  // Offset of the first character of a row (rows past the end: the last row).
  size_t row_start(const PieceTree& tree, size_t line, size_t row) const;
  // The offset for goal column `column` in a row: the character whose cells
  // cover the column, or the first character at or after it when none covers
  // it (zero-width characters at the column are passed over, as
  // offset_at_column). Past the end of the line's last row: the end of the
  // line. Past the end of any other row: its last character (the offset after
  // it would be the next row's start).
  size_t offset_at(const PieceTree& tree, size_t line, size_t row, int column) const;
  // The characters of a row whose cells reach into columns [from, to), in
  // order. The walk stops at column `to`; RowInfo says whether it reached the
  // end of the row.
  RowInfo row_chars(const PieceTree& tree, size_t line, size_t row, int from, int to,
                    std::vector<RowChar>* out) const;

  // The next / previous screen row; false at the end / start of the buffer.
  bool next_row(const PieceTree& tree, RowPos* pos) const;
  bool previous_row(const PieceTree& tree, RowPos* pos) const;
  // Moves up to `n` rows forward / backward; returns the rows moved.
  size_t forward_rows(const PieceTree& tree, RowPos* pos, size_t n) const;
  size_t backward_rows(const PieceTree& tree, RowPos* pos, size_t n) const;
  // The last screen row of the buffer.
  RowPos last_row(const PieceTree& tree) const;

  // UTF-16 units walked (and segmented) so far (tests check that long lines
  // use the recorded states) and the states recorded for `line`.
  size_t scanned() const { return scanned_; }
  void reset_scanned() const { scanned_ = 0; }
  size_t checkpoint_count(size_t line) const;

  // Tests only: walks every cluster through the general cluster walk
  // (for_each_cluster), without the run of lone clusters (walk), so that
  // both can be compared.
  void use_general_walk_for_testing(bool general) { general_walk_ = general; }

  // Offset where the trailing whitespace (spaces, TABs, U+3000) of `line`
  // starts, on a cluster boundary; the line end when there is none. Kept per
  // line (up to 8 lines)
  // until the text changes: an edit reported through note_edit is looked
  // at from the line end back to the edit only, so drawing a row never
  // walks the whole whitespace tail of a long line again. Counted in
  // scanned().
  size_t trailing_start(const PieceTree& tree, size_t line) const;

  // Internal wrap state before the cluster at `offset` (exposed for the
  // implementation's helpers).
  struct State {
    size_t offset = 0;
    size_t row = 0;
    int column = 0;
    int64_t logical = 0;       // column from the start of the logical line
    GraphemeBreaker breaker;   // segmentation state before `offset`

    friend bool operator==(const State&, const State&) = default;
  };
  // Tests only: the recorded states of `line` (nullptr when it has none).
  const std::vector<State>* checkpoints_for_testing(size_t line) const;

 private:
  struct LineCache {
    size_t line = 0;
    size_t start = 0;
    size_t end = 0;                 // line end at the last use
    std::vector<State> states;      // states[0] is the line start
    bool complete = false;          // the walk reached the line end
    State end_state;                // valid when complete
    // The state before the first character of the row after the last row
    // break walked over: drawing consecutive rows starts each walk there.
    bool has_break = false;
    State last_break;
    uint64_t used = 0;
  };

  // Drops every cache when the tree changed without note_edit.
  void sync(const PieceTree& tree) const;
  // The wrap states of note_edit: keeps those before `offset`.
  void note_wrap_edit(size_t offset);
  // The cache of a long line (nullptr for short lines).
  LineCache* cache_for(const PieceTree& tree, size_t line) const;
  // The clusters a walk's f returns true for without doing anything: those
  // that end at or before `end` and are placed on a row before `row`.
  struct Skip {
    size_t end = ~size_t{0};
    size_t row = ~size_t{0};
  };
  // Walks from `from` to the line end (or until f returns false), calling
  // f(cluster, row, column, width, logical) for each cluster after its row
  // break has been decided; records states into `cache`. Returns the state
  // after the walk. Runs of lone clusters (lone_cluster_width) that `skip`
  // covers are placed by skip_lone_clusters without calling f; everything
  // else goes through for_each_cluster.
  template <typename F>
  State walk(const PieceTree& tree, LineCache* cache, State from, size_t line_end, Skip skip, F&& f) const;
  // Places the lone clusters from s->offset on, while `skip` covers them and
  // the unit after each one is lone too (within one chunk of the tree), as
  // walk's step would place them; records states into `cache`.
  void skip_lone_clusters(const PieceTree& tree, LineCache* cache, State* s, size_t line_end, Skip skip) const;
  // The latest recorded state of `line` that is not after (row, column).
  State seek(const PieceTree& tree, size_t line, LineCache* cache, size_t row, int column) const;
  State line_start_state(const PieceTree& tree, size_t line) const;

  int width_ = 0;
  int tab_width_ = 4;
  struct TrailingCache {
    size_t line = 0;
    size_t start = 0;
    size_t end = 0;                // line end (valid while edited == ~0)
    bool has_text = false;         // the line has a non-whitespace character
    size_t last_text = 0;          // offset of the last one (when has_text)
    size_t edited = ~size_t{0};    // lowest offset edited since (none: ~0)
    uint64_t used = 0;
  };

  mutable uint64_t revision_ = ~uint64_t{0};
  mutable std::vector<LineCache> caches_;
  mutable std::vector<TrailingCache> trailing_;
  mutable uint64_t clock_ = 0;
  mutable size_t scanned_ = 0;
  bool general_walk_ = false;
};

}  // namespace geditor
