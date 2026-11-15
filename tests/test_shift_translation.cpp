// Emacs shift translation in key lookup: an unbound S- key runs the command of
// the key without S-; a binding of the S- key itself wins ([C6] of M1b).

#include "fake_host.h"
#include "keymap.h"

using geditor::Keymap;
using geditor::KeySequence;

TEST(shift_translation_s_left) {
  test::Session s;
  s.load(L"abc");
  s.set_point(2);
  s.keys(L"S-<left>");
  CHECK_EQ(s.point(), size_t{1});
  CHECK_EQ(s.message(), std::wstring());
}

TEST(shift_translation_s_down) {
  test::Session s;
  s.load(L"ab\ncd\n");
  s.set_point(1);
  s.keys(L"S-<down>");
  CHECK_EQ(s.point(), size_t{4});
}

TEST(shift_translation_c_s_right) {
  test::Session s;
  s.load(L"one two three");
  s.keys(L"C-S-<right>");
  CHECK_EQ(s.point(), size_t{3});
}

TEST(shift_translation_s_end) {
  test::Session s;
  s.load(L"hello\nworld");
  s.keys(L"S-<end>");
  CHECK_EQ(s.point(), size_t{5});
}

TEST(shift_translation_c_s_letter) {
  // C-S-b is not one of the 12 Windows keys: it runs C-b (backward-char).
  test::Session s;
  s.load(L"abc");
  s.set_point(2);
  s.keys(L"C-S-b");
  CHECK_EQ(s.point(), size_t{1});
  CHECK_EQ(s.message(), std::wstring());
}

TEST(shift_translation_not_for_windows_keys) {
  // C-S-a is the Windows key select-all, not C-a (beginning-of-line).
  test::Session s;
  s.load(L"abc");
  s.set_point(2);
  s.keys(L"C-S-a");
  CHECK(s.ed.buffer().mark_active);
  CHECK_EQ(s.ed.buffer().mark, size_t{0});
  CHECK_EQ(s.point(), size_t{3});
}

TEST(shift_translation_settings_win_over_windows_keys) {
  test::Session s;
  s.load(L"abc");
  s.set_point(2);
  s.settings("{\"keys\": {\"C-S-a\": \"beginning-of-line\"}}");
  CHECK(s.ed.settings_errors().empty());
  s.keys(L"C-S-a");
  CHECK_EQ(s.point(), size_t{0});
  CHECK(!s.ed.buffer().mark_active);
  s.set_point(2);
  s.settings("{\"keys\": {\"C-S-a\": null}}");
  s.keys(L"C-S-a");
  CHECK_EQ(s.message(), std::wstring(L"C-S-a is undefined"));
  CHECK_EQ(s.point(), size_t{2});
  CHECK(!s.ed.buffer().mark_active);
}

TEST(shift_translation_unbound_both_ways) {
  test::Session s;
  s.load(L"abc");
  s.keys(L"C-S-i");
  CHECK_EQ(s.message(), std::wstring(L"C-S-i is undefined"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
}

TEST(shift_translation_in_prefix_sequence) {
  // C-x C-S-s: the second key of a prefix sequence is translated too.
  test::Session s;
  s.load(L"abc");
  s.type(L"x");
  s.keys(L"C-x C-S-s");
  CHECK(!s.modified());
  CHECK(s.host.file(L"C:\\work\\t.txt") == "xabc");
}

TEST(shift_translation_explicit_binding_wins) {
  Keymap map;
  CHECK(map.bind(L"<left>", "backward-char"));
  CHECK(map.bind(L"S-<left>", "shift-left-command"));
  auto lookup = [&](const KeySequence& keys) { return map.lookup(keys); };

  KeySequence keys = *geditor::parse_key_sequence(L"S-<left>");
  const Keymap::Lookup r = geditor::lookup_shift_translated(keys, lookup);
  CHECK_EQ(r.result, Keymap::Result::kCommand);
  CHECK(r.command == "shift-left-command");
  CHECK_EQ(geditor::key_sequence_to_string(keys), std::wstring(L"S-<left>"));

  KeySequence other = *geditor::parse_key_sequence(L"S-<right>");
  CHECK(map.bind(L"<right>", "forward-char"));
  const Keymap::Lookup r2 = geditor::lookup_shift_translated(other, lookup);
  CHECK(r2.command == "forward-char");
  CHECK_EQ(geditor::key_sequence_to_string(other), std::wstring(L"<right>"));
}

TEST(shift_translation_key_is_bound) {
  test::Session s;
  CHECK(!s.ed.key_is_bound(*geditor::parse_key(L"M-<f4>")));
  CHECK(!s.ed.key_is_bound(*geditor::parse_key(L"M-SPC")));
  CHECK(s.ed.key_is_bound(*geditor::parse_key(L"C-x")));
  CHECK(s.ed.key_is_bound(*geditor::parse_key(L"S-<left>")));
  s.keys(L"C-x");
  CHECK(s.ed.key_is_bound(*geditor::parse_key(L"C-s")));
  CHECK(!s.ed.key_is_bound(*geditor::parse_key(L"M-<f4>")));
}
