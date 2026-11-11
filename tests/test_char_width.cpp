// Character widths (generated table) and cell layout.

#include <cstring>

#include "char_width.h"
#include "layout.h"
#include "test_harness.h"

TEST(char_width_samples) {
  CHECK_EQ(geditor::char_width(U'a'), 1);
  CHECK_EQ(geditor::char_width(U'\x3042'), 2);    // HIRAGANA LETTER A
  CHECK_EQ(geditor::char_width(U'\xFF21'), 2);    // FULLWIDTH LATIN CAPITAL LETTER A
  CHECK_EQ(geditor::char_width(U'\xFF71'), 1);    // HALFWIDTH KATAKANA LETTER A
  CHECK_EQ(geditor::char_width(U'\U0001F600'), 2);
  CHECK_EQ(geditor::char_width(U'\x0301'), 0);    // COMBINING ACUTE ACCENT (Mn)
  CHECK_EQ(geditor::char_width(U'\x200D'), 0);    // ZERO WIDTH JOINER
  CHECK_EQ(geditor::char_width(U'\x200B'), 0);
  CHECK_EQ(geditor::char_width(U'\xFE0F'), 0);    // VARIATION SELECTOR-16
  CHECK_EQ(geditor::char_width(U'\x0001'), 2);    // C0 control in caret notation
  CHECK_EQ(geditor::char_width(U'\x007F'), 2);
  CHECK_EQ(geditor::char_width(U'\x2460'), 1);    // CIRCLED DIGIT ONE (ambiguous)
  CHECK_EQ(geditor::char_width(U'\x4E00'), 2);
}

TEST(char_width_tab_advances_to_next_stop) {
  CHECK_EQ(geditor::cell_advance(U'\t', 0, 4), 4);
  CHECK_EQ(geditor::cell_advance(U'\t', 3, 4), 1);
  CHECK_EQ(geditor::cell_advance(U'\t', 4, 4), 4);
  CHECK_EQ(geditor::cell_advance(U'\t', 5, 8), 3);
  CHECK_EQ(geditor::cell_advance(U'a', 3, 4), 1);
}

TEST(char_width_table_records_unicode_version) {
  CHECK(std::strlen(geditor::char_width_unicode_version()) >= 5);
  CHECK_EQ(std::string(geditor::char_width_unicode_version()), std::string("18.0.0"));
}

TEST(layout_column_counts_cells) {
  const geditor::PieceTree t(L"a\x3042\tb\n\U0001F600x");
  CHECK_EQ(geditor::column_at(t, 0, 4), 0);
  CHECK_EQ(geditor::column_at(t, 1, 4), 1);
  CHECK_EQ(geditor::column_at(t, 2, 4), 3);
  CHECK_EQ(geditor::column_at(t, 3, 4), 4);   // tab from 3 to 4
  CHECK_EQ(geditor::column_at(t, 4, 4), 5);
  CHECK_EQ(geditor::column_at(t, 7, 4), 2);   // after the surrogate pair
  CHECK_EQ(geditor::column_at(t, 3, 8), 8);
}

TEST(layout_offset_at_column_does_not_enter_wide_char) {
  const geditor::PieceTree t(L"a\x3042" L"bc\nxe\x0301z");
  CHECK_EQ(geditor::offset_at_column(t, 0, 0, 4), size_t{0});
  CHECK_EQ(geditor::offset_at_column(t, 0, 1, 4), size_t{1});
  CHECK_EQ(geditor::offset_at_column(t, 0, 2, 4), size_t{1});  // inside the wide char
  CHECK_EQ(geditor::offset_at_column(t, 0, 3, 4), size_t{2});
  CHECK_EQ(geditor::offset_at_column(t, 0, 99, 4), size_t{4});
  // Column 2 on line 1 is after "xe" + combining mark.
  CHECK_EQ(geditor::offset_at_column(t, 1, 2, 4), size_t{8});
}
