// Movement and editing by grapheme cluster ([M3c-3], decisions G3 and G4):
// every command steps, deletes and selects whole clusters, and positions
// that come from outside snap to the cluster boundary at or before them.

#include <string>
#include <vector>

#include "fake_host.h"
#include "grapheme.h"
#include "layout.h"

using test::Session;

namespace {

// The samples of [C3], each one cluster.
const std::wstring kVs16 = L"☺️";                                          // 2 units, 2 cells
const std::wstring kFamily = L"\U0001F468‍\U0001F469‍\U0001F467";          // 8 units, 2 cells
const std::wstring kFlag = L"\U0001F1EF\U0001F1F5";                                  // 4 units, 2 cells
const std::wstring kKeycap = L"#️⃣";                                       // 3 units, 2 cells
const std::wstring kSkin = L"\U0001F44D\U0001F3FD";                                  // 4 units, 2 cells
const std::wstring kKa = L"が";                                            // 2 units, 2 cells
const std::wstring kAcute = L"é";                                              // 2 units, 1 cell
const std::wstring kHangul = L"각";                                  // 3 units, 2 cells
const std::wstring kDeva = L"क्ष";                                    // 3 units, 1 cell

std::vector<std::wstring> samples() {
  return {kVs16, kFamily, kFlag, kKeycap, kSkin, kKa, kAcute, kHangul, kDeva};
}

}  // namespace

// ---- C-f / C-b ----

TEST(cluster_forward_char_steps_over_each_sample) {
  for (const std::wstring& c : samples()) {
    Session s;
    s.load(L"a" + c + L"b");
    s.keys(L"C-f");
    CHECK_EQ(s.point(), size_t{1});
    s.keys(L"C-f");
    CHECK_EQ(s.point(), 1 + c.size());
    s.keys(L"<right>");
    CHECK_EQ(s.point(), 2 + c.size());
  }
}

TEST(cluster_backward_char_steps_over_each_sample) {
  for (const std::wstring& c : samples()) {
    Session s;
    s.load(L"a" + c + L"b");
    s.keys(L"M->");
    s.keys(L"C-b");
    CHECK_EQ(s.point(), 1 + c.size());
    s.keys(L"C-b");
    CHECK_EQ(s.point(), size_t{1});
    s.keys(L"<left>");
    CHECK_EQ(s.point(), size_t{0});
  }
}

TEST(cluster_char_motion_with_prefix_counts) {
  Session s;
  s.load(L"a" + kFamily + kFlag + kAcute + L"b");
  s.keys(L"M-3 C-f");
  CHECK_EQ(s.point(), 1 + kFamily.size() + kFlag.size());
  s.keys(L"M-2 C-b");
  CHECK_EQ(s.point(), size_t{1});
  s.keys(L"M-- C-b");  // -1: forward
  CHECK_EQ(s.point(), 1 + kFamily.size());
}

// ---- DEL / C-d and undo ----

TEST(cluster_delete_backward_char_deletes_one_cluster_and_undo_restores_it) {
  for (const std::wstring& c : samples()) {
    Session s;
    s.load(L"a" + c + L"b");
    s.set_point(1 + c.size());
    s.keys(L"DEL");
    CHECK_EQ(s.text(), std::wstring(L"ab"));
    CHECK_EQ(s.point(), size_t{1});
    s.keys(L"C-/");
    CHECK_EQ(s.text(), L"a" + c + L"b");
    CHECK_EQ(s.point(), 1 + c.size());
  }
}

TEST(cluster_delete_char_deletes_one_cluster_and_undo_restores_it) {
  for (const std::wstring& c : samples()) {
    Session s;
    s.load(L"a" + c + L"b");
    s.set_point(1);
    s.keys(L"C-d");
    CHECK_EQ(s.text(), std::wstring(L"ab"));
    s.keys(L"C-_");
    CHECK_EQ(s.text(), L"a" + c + L"b");
  }
}

TEST(cluster_delete_with_prefix_counts) {
  Session s;
  s.load(L"a" + kVs16 + kKeycap + kHangul + L"b");
  s.set_point(1);
  s.keys(L"M-2 C-d");
  CHECK_EQ(s.text(), L"a" + kHangul + L"b");
  s.keys(L"M->");
  s.keys(L"M-2 DEL");
  CHECK_EQ(s.text(), std::wstring(L"a"));
  // More clusters than there are: nothing is deleted (as Emacs).
  s.keys(L"M-5 DEL");
  CHECK_EQ(s.text(), std::wstring(L"a"));
  CHECK_EQ(s.message(), std::wstring(L"Beginning of buffer"));
}

// ---- G4: a combining mark typed after its base joins its cluster ----

TEST(cluster_typed_combining_mark_joins_the_base_and_del_deletes_both) {
  Session s;
  s.load(L"x");
  s.keys(L"M->");
  s.type(L"e");
  s.type(L"́");
  CHECK_EQ(s.text(), std::wstring(L"xé"));
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"DEL");
  CHECK_EQ(s.text(), std::wstring(L"x"));
}

TEST(cluster_ime_variation_selector_joins_the_base) {
  // The IME commits U+263A, then U+FE0F (or both at once): one cluster.
  Session s;
  s.load(L"");
  s.ed.insert_text(L"☺");
  s.ed.insert_text(L"️");
  CHECK_EQ(s.ed.point_column(), 2);
  s.keys(L"DEL");
  CHECK_EQ(s.text(), std::wstring());
  s.ed.insert_text(kVs16 + L"a");
  s.keys(L"C-b DEL");
  CHECK_EQ(s.text(), std::wstring(L"a"));
}

TEST(cluster_deleting_what_separates_a_mark_from_its_base_keeps_point_on_a_boundary) {
  // DEL of the TAB between "e" and U+0301 joins them into one cluster:
  // point (1, now inside it) goes back to the boundary before it.
  Session s;
  s.load(L"e\t́");
  s.set_point(2);
  CHECK_EQ(s.point(), size_t{2});
  s.keys(L"DEL");
  CHECK_EQ(s.text(), std::wstring(L"é"));
  CHECK_EQ(s.point(), size_t{0});
}

TEST(cluster_quoted_insert_of_a_mark_joins_the_base) {
  // C-q then a combining mark typed as a key: inserted into the cluster
  // before point, as self-insert does.
  Session s;
  s.load(L"a");
  s.keys(L"M-> C-q");
  s.type(L"̈");
  CHECK_EQ(s.text(), std::wstring(L"ä"));
  s.keys(L"C-b");
  CHECK_EQ(s.point(), size_t{0});
}

// ---- Word motion, kill-word, C-k ----

TEST(cluster_word_motion_edges) {
  Session s;
  s.load(L"café がき " + kFamily + L" end");
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{5});  // after the accent, not between
  s.keys(L"M-f");
  CHECK_EQ(s.point(), size_t{9});  // the voiced mark stays in the word
  s.keys(L"M-f");
  CHECK_EQ(s.point(), 10 + kFamily.size() + 4);  // the emoji is no word character
  s.keys(L"M-b M-b");
  CHECK_EQ(s.point(), size_t{6});
  s.keys(L"M-b");
  CHECK_EQ(s.point(), size_t{0});
}

TEST(cluster_kill_word_and_backward_kill_word) {
  Session s;
  s.load(L"café au lait́");
  s.keys(L"M-d");
  CHECK_EQ(s.text(), std::wstring(L" au lait́"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"café"));
  s.keys(L"M->");
  s.keys(L"M-DEL");
  CHECK_EQ(s.text(), std::wstring(L" au "));
  CHECK_EQ(s.host.clipboard, std::wstring(L"lait́"));
}

TEST(cluster_kill_line_keeps_clusters_whole) {
  Session s;
  s.load(L"a" + kFamily + kFlag + L"\nb");
  s.set_point(1);
  s.keys(L"C-k");
  CHECK_EQ(s.host.clipboard, kFamily + kFlag);
  CHECK_EQ(s.text(), std::wstring(L"a\nb"));
}

TEST(cluster_transpose_chars_moves_whole_clusters) {
  Session s;
  s.load(L"a" + kVs16);
  s.keys(L"C-e C-t");
  CHECK_EQ(s.text(), kVs16 + L"a");
}

// ---- Selection ----

TEST(cluster_region_ends_are_on_boundaries) {
  Session s;
  s.load(L"a" + kFamily + kAcute + L"b");
  s.set_point(1);
  s.keys(L"C-SPC C-f C-f");
  CHECK_EQ(s.ed.buffer().mark, size_t{1});
  CHECK_EQ(s.point(), 1 + kFamily.size() + kAcute.size());
  s.keys(L"M-w");
  CHECK_EQ(s.host.clipboard, kFamily + kAcute);
  // A position from outside inside a cluster snaps.
  s.set_point(3);
  CHECK_EQ(s.point(), size_t{1});
}

// ---- Vertical motion: goal column in cells ----

TEST(cluster_goal_column_counts_cells) {
  Session s;
  s.load(L"a" + kVs16 + L"b\nabcd");
  s.set_point(1 + kVs16.size());  // before "b": column 3
  CHECK_EQ(s.ed.point_column(), 3);
  s.keys(L"C-n");
  CHECK_EQ(s.point(), 1 + kVs16.size() + 2 + 3);  // before "d"
  s.keys(L"C-b C-p");  // column 2 is inside the emoji: its start
  CHECK_EQ(s.point(), size_t{1});
}

// ---- Mouse ----

TEST(cluster_click_on_each_cell_of_a_two_cell_cluster) {
  for (const std::wstring& c : {kVs16, kFamily, kFlag, kKeycap, kSkin, kKa, kHangul}) {
    Session s;
    s.load(L"a" + c + L"b");
    CHECK(s.ed.mouse_press(s.ed.offset_at_screen(0, 1), false));
    CHECK_EQ(s.point(), size_t{1});
    s.ed.mouse_release();
    CHECK(s.ed.mouse_press(s.ed.offset_at_screen(0, 2), false));
    CHECK_EQ(s.point(), size_t{1});  // the second cell: still before the cluster
    s.ed.mouse_release();
    CHECK(s.ed.mouse_press(s.ed.offset_at_screen(0, 3), false));
    CHECK_EQ(s.point(), 1 + c.size());
    s.ed.mouse_release();
  }
}

TEST(cluster_mouse_offsets_inside_a_cluster_snap) {
  Session s;
  s.load(L"a" + kFamily + L"b");
  for (size_t off = 2; off < 1 + kFamily.size(); ++off) {
    CHECK(s.ed.mouse_press(off, false));
    CHECK_EQ(s.point(), size_t{1});
    s.ed.mouse_release();
  }
  // Shift+click and drag ends too.
  CHECK(s.ed.mouse_press(0, false));
  CHECK(s.ed.mouse_drag(4));
  CHECK_EQ(s.point(), size_t{1});
  s.ed.mouse_release();
  CHECK(s.ed.mouse_press(5, true));
  CHECK_EQ(s.point(), size_t{1});
}

TEST(cluster_double_click_selects_whole_clusters) {
  Session s;
  s.load(L"café " + kFamily + L" x");
  CHECK(s.ed.mouse_double_click(s.ed.offset_at_screen(0, 3)));
  CHECK_EQ(s.ed.buffer().mark, size_t{0});
  CHECK_EQ(s.point(), size_t{5});
  // The emoji is no word character: the one cluster.
  CHECK(s.ed.mouse_double_click(s.ed.offset_at_screen(0, 6)));
  CHECK_EQ(s.ed.buffer().mark, size_t{6});
  CHECK_EQ(s.point(), 6 + kFamily.size());
}

// ---- Snapping of positions from the host (G3) ----

TEST(cluster_set_point_snaps_to_the_boundary_at_or_before) {
  Session s;
  s.load(L"a" + kFlag + L"b\n" + kAcute);
  s.set_point(2);
  CHECK_EQ(s.point(), size_t{1});
  s.set_point(4);
  CHECK_EQ(s.point(), size_t{1});
  s.set_point(5);
  CHECK_EQ(s.point(), size_t{5});
  // +LINE goes to a line start, which always is a boundary (a lone mark at
  // a line start is a cluster of its own).
  const size_t line1 = s.ed.buffer().text.line_start(1);
  s.set_point(line1);
  CHECK_EQ(s.point(), line1);
  s.set_point(line1 + 1);
  CHECK_EQ(s.point(), line1);
}

TEST(cluster_yank_of_a_leading_mark_keeps_mark_on_a_boundary) {
  Session s;
  s.load(L"e");
  s.keys(L"M->");
  s.host.clipboard = L"́x";
  s.host.clipboard_has_text = true;
  s.keys(L"C-y");
  CHECK_EQ(s.text(), std::wstring(L"éx"));
  CHECK_EQ(s.ed.buffer().mark, size_t{0});  // was 1, inside the joined cluster
  CHECK_EQ(s.point(), size_t{3});
}

// ---- Mode line column ----

TEST(cluster_mode_line_column_counts_cluster_cells) {
  Session s;
  s.load(kFamily + kAcute + L"x");
  s.keys(L"C-f C-f");
  CHECK_EQ(s.ed.point_column(), 3);
}
