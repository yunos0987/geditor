// Cell-grid positions within a logical line (no wrapping yet). Columns are
// counted in cells: wide characters take 2, TAB advances to the next stop.
#pragma once

#include <cstddef>

#include "piece_tree.h"

namespace geditor {

// Column (cells) of `offset` from the start of its line.
int column_at(const PieceTree& tree, size_t offset, int tab_width);

// Offset in `line` of the character at `column`: the last position whose
// column does not exceed `column` (a wide character that spans `column` is
// not entered), clamped to the end of the line. Zero-width characters after
// that position are skipped so that the cursor does not land before them.
size_t offset_at_column(const PieceTree& tree, size_t line, int column, int tab_width);

// Offset of the next / previous code point (surrogate pairs are not split).
size_t next_char_offset(const PieceTree& tree, size_t offset);
size_t prev_char_offset(const PieceTree& tree, size_t offset);
// Code point starting at `offset` (0 at the end of the text).
char32_t code_point_at(const PieceTree& tree, size_t offset);

}  // namespace geditor
