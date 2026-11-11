// Undo/redo rules of docs/PLAN.md 2 ([M1-3]), driven by keys.

#include "fake_host.h"

using test::Session;

TEST(undo_typing_groups_20_chars_and_21st_starts_new_group) {
  Session s;
  s.load(L"");
  s.type(L"abcdefghijklmnopqrstu");  // 21 characters
  CHECK_EQ(s.text(), std::wstring(L"abcdefghijklmnopqrstu"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"abcdefghijklmnopqrst"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring());
  s.keys(L"C-/");
  CHECK_EQ(s.message(), std::wstring(L"No further undo information"));
}

TEST(undo_typing_group_ends_at_other_command) {
  Session s;
  s.load(L"");
  s.type(L"ab");
  s.keys(L"C-b");
  s.type(L"c");
  CHECK_EQ(s.text(), std::wstring(L"acb"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring());
}

TEST(undo_one_group_per_command) {
  Session s;
  s.load(L"one two three\nnext");
  s.keys(L"M-d");  // kill-word: one command, one group
  CHECK_EQ(s.text(), std::wstring(L" two three\nnext"));
  s.keys(L"C-k");  // kill-line
  CHECK_EQ(s.text(), std::wstring(L"\nnext"));
  s.keys(L"C-y");  // yank
  CHECK_EQ(s.text(), std::wstring(L" two three\nnext"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"\nnext"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L" two three\nnext"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"one two three\nnext"));
}

TEST(undo_transpose_is_one_group) {
  Session s;
  s.load(L"ab");
  s.set_point(1);
  s.keys(L"C-t");  // erase + insert inside one command
  CHECK_EQ(s.text(), std::wstring(L"ba"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  CHECK_EQ(s.point(), size_t{1});
}

TEST(undo_ime_string_is_one_group) {
  Session s;
  s.load(L"");
  s.type(L"ab");
  s.ed.insert_text(L"\x65E5\x672C\x8A9E");  // committed IME string
  s.type(L"c");
  CHECK_EQ(s.text(), std::wstring(L"ab\x65E5\x672C\x8A9E" L"c"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"ab\x65E5\x672C\x8A9E"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring());
}

TEST(undo_restores_cursor) {
  Session s;
  s.load(L"hello world");
  s.set_point(6);
  s.keys(L"M-d");
  CHECK_EQ(s.text(), std::wstring(L"hello "));
  s.keys(L"C-a");
  CHECK_EQ(s.point(), size_t{0});
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"hello world"));
  CHECK_EQ(s.point(), size_t{6});
  s.keys(L"C-a C-?");  // redo puts the cursor where the command left it
  CHECK_EQ(s.text(), std::wstring(L"hello "));
  CHECK_EQ(s.point(), size_t{6});
}

TEST(undo_to_save_point_clears_modified_and_redo_sets_it) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-e");
  s.type(L"x");
  CHECK(s.modified());
  s.keys(L"C-x C-s");
  CHECK(!s.modified());
  s.type(L"y");  // must not merge into the saved group
  CHECK(s.modified());
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"abcx"));
  CHECK(!s.modified());
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  CHECK(s.modified());
  s.keys(L"C-?");
  CHECK(!s.modified());
  s.keys(L"C-?");
  CHECK_EQ(s.text(), std::wstring(L"abcxy"));
  CHECK(s.modified());
  CHECK(s.ed.mode_line().find(L"-** ") == 0);
}

TEST(undo_new_edit_discards_redo_history) {
  Session s;
  s.load(L"");
  s.type(L"ab");
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring());
  s.type(L"c");
  s.keys(L"C-?");
  CHECK_EQ(s.message(), std::wstring(L"No further redo information"));
  CHECK_EQ(s.text(), std::wstring(L"c"));
}

TEST(undo_save_point_lost_when_redo_discarded) {
  Session s;
  s.load(L"");
  s.type(L"ab");
  s.keys(L"C-x C-s");
  s.keys(L"C-/");
  s.type(L"c");  // the saved state can no longer be reached
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring());
  CHECK(s.modified());
}

TEST(undo_refused_read_only_edit_leaves_no_entry) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-x C-q");
  s.type(L"x");
  CHECK_EQ(s.message(), std::wstring(L"Buffer is read-only"));
  s.keys(L"C-d");
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  CHECK(!s.modified());
  s.keys(L"C-x C-q");
  CHECK_EQ(s.ed.buffer().undo.undo_count(), size_t{0});
  s.keys(L"C-/");
  CHECK_EQ(s.message(), std::wstring(L"No further undo information"));
}

TEST(undo_history_unit_merges_typing_pieces) {
  geditor::PieceTree t;
  geditor::UndoHistory h;
  for (int i = 0; i < 5; ++i) {
    h.begin_command(static_cast<size_t>(i), true);
    h.record_insert(static_cast<size_t>(i), t.insert(static_cast<size_t>(i), L"z"));
    h.end_command(static_cast<size_t>(i) + 1);
  }
  CHECK_EQ(h.undo_count(), size_t{1});
  size_t point = 0;
  CHECK(h.undo(t, nullptr, &point));
  CHECK_EQ(t.text(), std::wstring());
  CHECK_EQ(point, size_t{0});
  CHECK(h.redo(t, nullptr, &point));
  CHECK_EQ(t.text(), std::wstring(L"zzzzz"));
  CHECK_EQ(point, size_t{5});
}
