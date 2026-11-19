// A prefix argument before text committed by the IME (Editor::insert_text,
// user decision of 2026-11-20): the whole committed text is inserted that
// many times, as a prefix argument repeats a typed character. A count of 0
// or less, or one too large, is refused with the messages of
// self-insert-command.

#include <string>

#include "fake_host.h"

using test::Session;

namespace {

std::wstring times(const std::wstring& s, int n) {
  std::wstring out;
  for (int i = 0; i < n; ++i) out += s;
  return out;
}

}  // namespace

TEST(ime_prefix_c_digit_repeats_the_committed_text) {
  Session s;
  s.load(L"");
  s.keys(L"C-3");
  s.ed.insert_text(L"\xFF58");  // fullwidth x, as the IME gives it
  CHECK_EQ(s.text(), times(L"\xFF58", 3));
  CHECK_EQ(s.point(), size_t{3});
}

TEST(ime_prefix_repeats_a_text_of_several_characters_whole) {
  Session s;
  s.load(L"");
  s.keys(L"C-3");
  s.ed.insert_text(L"\x304B\x3093\x3058");  // kanji in kana
  CHECK_EQ(s.text(), times(L"\x304B\x3093\x3058", 3));
}

TEST(ime_prefix_every_prefix_key_form) {
  struct Case {
    const wchar_t* keys;
    int count;
  };
  const Case cases[] = {{L"M-3", 3}, {L"C-M-3", 3}, {L"C-3", 3}, {L"M-1 2", 12}, {L"C-1 C-2", 12}, {L"M-0 7", 7}};
  for (const Case& c : cases) {
    Session s;
    s.load(L"ab");
    s.set_point(1);
    s.keys(c.keys);
    s.ed.insert_text(L"\x3042");
    CHECK_EQ(s.text(), L"a" + times(L"\x3042", c.count) + L"b");
    CHECK_EQ(s.point(), size_t(1 + c.count));
  }
}

TEST(ime_prefix_universal_argument_bound_in_settings) {
  Session s;
  s.settings(R"({"keys": {"C-u": "universal-argument"}})");
  s.load(L"");
  s.keys(L"C-u");
  s.ed.insert_text(L"\x3042");
  CHECK_EQ(s.text(), times(L"\x3042", 4));
  s.keys(L"C-u C-u");
  s.ed.insert_text(L"\x3044");
  CHECK_EQ(s.text(), times(L"\x3042", 4) + times(L"\x3044", 16));
}

TEST(ime_prefix_is_one_undo_group) {
  Session s;
  s.load(L"");
  s.type(L"ab");
  s.keys(L"C-3");
  s.ed.insert_text(L"\x3042\x3044");
  s.type(L"cd");
  CHECK_EQ(s.text(), L"ab" + times(L"\x3042\x3044", 3) + L"cd");
  s.keys(L"C-/");
  CHECK_EQ(s.text(), L"ab" + times(L"\x3042\x3044", 3));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  CHECK_EQ(s.point(), size_t{2});
}

TEST(ime_prefix_is_used_once) {
  Session s;
  s.load(L"");
  s.keys(L"C-3");
  s.ed.insert_text(L"\x3042");
  s.ed.insert_text(L"\x3044");
  CHECK_EQ(s.text(), times(L"\x3042", 3) + L"\x3044");
}

TEST(ime_without_prefix_inserts_once) {
  Session s;
  s.load(L"");
  s.ed.insert_text(L"\x3042\x3044");
  CHECK_EQ(s.text(), std::wstring(L"\x3042\x3044"));
}

TEST(ime_prefix_matches_a_typed_character) {
  // C-3 then the committed "x" gives the same as C-3 then the key x.
  for (const wchar_t* prefix : {L"C-3", L"M-2", L"C-M-5"}) {
    Session typed;
    typed.load(L"ab");
    typed.set_point(1);
    typed.keys(prefix);
    typed.keys(L"x");
    Session ime;
    ime.load(L"ab");
    ime.set_point(1);
    ime.keys(prefix);
    ime.ed.insert_text(L"x");
    CHECK_EQ(ime.text(), typed.text());
    CHECK_EQ(ime.point(), typed.point());
    CHECK_EQ(ime.message(), typed.message());
    typed.keys(L"C-/");
    ime.keys(L"C-/");
    CHECK_EQ(ime.text(), typed.text());
    CHECK_EQ(ime.point(), typed.point());
  }
}

TEST(ime_prefix_zero_and_negative_are_refused_as_for_a_typed_character) {
  for (const wchar_t* prefix : {L"C-0", L"M-0", L"C--", L"M--", L"C-M--", L"M-- 3"}) {
    Session typed;
    typed.load(L"ab");
    typed.keys(prefix);
    typed.keys(L"x");
    Session ime;
    ime.load(L"ab");
    ime.keys(prefix);
    ime.ed.insert_text(L"\x3042");
    CHECK_EQ(ime.text(), std::wstring(L"ab"));
    CHECK_EQ(ime.message(), std::wstring(L"Repeat count must be positive"));
    CHECK_EQ(ime.message(), typed.message());
    CHECK(!ime.modified());
    // The prefix is used up: the next text goes in once.
    ime.ed.insert_text(L"\x3044");
    CHECK_EQ(ime.text(), std::wstring(L"\x3044" L"ab"));
  }
}

TEST(ime_prefix_too_large_is_refused) {
  Session s;
  s.load(L"ab");
  // 5,000,001 times 2 units is more than kMaxRepeatedInsert (10,000,000).
  s.keys(L"M-5 0 0 0 0 0 1");
  s.ed.insert_text(L"\x3042\x3044");
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  CHECK_EQ(s.message(), std::wstring(L"Repeat count too large"));
  // 5,000,000 times 2 units is allowed (checked with a short text: the
  // limit is on the inserted length).
  Session t;
  t.load(L"");
  t.keys(L"M-5 0 0 0");
  t.ed.insert_text(L"\x3042\x3044");
  CHECK_EQ(t.text().size(), size_t{10000});
}

TEST(ime_prefix_line_ends_are_normalized_before_repeating) {
  Session s;
  s.load(L"");
  s.keys(L"C-2");
  s.ed.insert_text(L"a\r\nb\r");
  CHECK_EQ(s.text(), std::wstring(L"a\nb\na\nb\n"));
}

TEST(ime_prefix_with_an_unfinished_key_sequence_is_dropped) {
  Session s;
  s.load(L"");
  s.keys(L"C-3 C-x");
  s.ed.insert_text(L"\x3042");
  CHECK_EQ(s.text(), std::wstring(L"\x3042"));
  // C-x was dropped too: the next key is a key of its own.
  s.keys(L"C-f");
  s.ed.insert_text(L"\x3044");
  CHECK_EQ(s.text(), std::wstring(L"\x3042\x3044"));
}

TEST(ime_prefix_in_the_minibuffer) {
  Session s;
  s.load(L"");
  s.keys(L"M-x");
  s.keys(L"C-3");
  s.ed.insert_text(L"\x3042");
  CHECK(s.ed.minibuffer_active());
  const std::wstring echo = s.ed.echo_area_text();
  CHECK(echo.ends_with(times(L"\x3042", 3)));
  s.keys(L"C-g");
  CHECK_EQ(s.text(), std::wstring());
}

TEST(ime_prefix_in_a_keyboard_macro) {
  Session s;
  s.load(L"");
  s.keys(L"C-x (");
  s.keys(L"C-3");
  s.ed.insert_text(L"\x3042");
  s.keys(L"C-x )");
  CHECK_EQ(s.text(), times(L"\x3042", 3));
  s.keys(L"C-x e");
  CHECK_EQ(s.text(), times(L"\x3042", 6));
  s.keys(L"M-2 C-x e");
  CHECK_EQ(s.text(), times(L"\x3042", 12));
  s.keys(L"C-/");
  CHECK_EQ(s.text(), times(L"\x3042", 6));
}

TEST(ime_prefix_refusal_stops_a_keyboard_macro) {
  Session s;
  s.load(L"");
  s.keys(L"C-x (");
  s.keys(L"C-0");
  s.ed.insert_text(L"\x3042");
  s.type(L"z");
  s.keys(L"C-x )");
  CHECK_EQ(s.text(), std::wstring(L"z"));
  s.keys(L"C-x e");
  // The refusal stops the run before "z".
  CHECK_EQ(s.text(), std::wstring(L"z"));
  CHECK_EQ(s.message(), std::wstring(L"Repeat count must be positive"));
}

TEST(ime_prefix_on_a_read_only_buffer_inserts_nothing) {
  Session s;
  s.load(L"ab");
  s.keys(L"C-x C-q");
  s.keys(L"C-3");
  s.ed.insert_text(L"\x3042");
  CHECK_EQ(s.text(), std::wstring(L"ab"));
}
