// Character cell widths (East Asian Width F/W = 2), generated from the UCD by
// tools/gen_char_width.py into char_width_table.inc.
#pragma once

namespace geditor {

// Width in cells: 0, 1 or 2. TAB is 1 here; use cell_advance for tab stops.
int char_width(char32_t c);

// Cells that `c` occupies when it starts at `column`. TAB advances to the next
// multiple of `tab_width`.
int cell_advance(char32_t c, int column, int tab_width);

// Unicode version of the generated table, such as "18.0.0".
const char* char_width_unicode_version();

}  // namespace geditor
