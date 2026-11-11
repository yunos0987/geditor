// Key notation of docs/PLAN.md 4.2: parse and print back.

#include "key.h"
#include "test_harness.h"

using geditor::Key;

namespace {

bool round_trips(const wchar_t* notation) {
  const auto keys = geditor::parse_key_sequence(notation);
  if (!keys) return false;
  return geditor::key_sequence_to_string(*keys) == notation;
}

bool rejected(const wchar_t* notation) {
  return !geditor::parse_key_sequence(notation).has_value();
}

}  // namespace

TEST(key_notation_modifier_prefixes_round_trip) {
  CHECK(round_trips(L"C-a"));
  CHECK(round_trips(L"M-a"));
  CHECK(round_trips(L"S-a"));
  CHECK(round_trips(L"C-M-a"));
  CHECK(round_trips(L"C-S-a"));
  CHECK(round_trips(L"M-S-a"));
  CHECK(round_trips(L"C-M-S-a"));
}

TEST(key_notation_symbols_written_as_shifted_char_round_trip) {
  CHECK(round_trips(L"C-?"));
  CHECK(round_trips(L"M-<"));
  CHECK(round_trips(L"M-%"));
  CHECK(round_trips(L"C-_"));
  CHECK(round_trips(L"C-/"));
  CHECK(round_trips(L"C--"));
  CHECK(round_trips(L"C-M-%"));
}

TEST(key_notation_named_keys_round_trip) {
  CHECK(round_trips(L"RET"));
  CHECK(round_trips(L"TAB"));
  CHECK(round_trips(L"SPC"));
  CHECK(round_trips(L"ESC"));
  CHECK(round_trips(L"DEL"));
  CHECK(round_trips(L"C-SPC"));
  CHECK(round_trips(L"M-DEL"));
}

TEST(key_notation_bracketed_keys_round_trip) {
  CHECK(round_trips(L"<delete>"));
  CHECK(round_trips(L"<insert>"));
  CHECK(round_trips(L"<home>"));
  CHECK(round_trips(L"<end>"));
  CHECK(round_trips(L"<prior>"));
  CHECK(round_trips(L"<next>"));
  CHECK(round_trips(L"<left>"));
  CHECK(round_trips(L"<right>"));
  CHECK(round_trips(L"<up>"));
  CHECK(round_trips(L"<down>"));
  CHECK(round_trips(L"<f1>"));
  CHECK(round_trips(L"<f12>"));
  CHECK(round_trips(L"<f24>"));
  CHECK(round_trips(L"C-<left>"));
  CHECK(round_trips(L"S-<end>"));
  CHECK(round_trips(L"C-S-<right>"));
}

TEST(key_notation_multi_key_sequence_round_trips) {
  CHECK(round_trips(L"C-x C-s"));
  CHECK(round_trips(L"C-x RET f"));
  const auto keys = geditor::parse_key_sequence(L"C-x C-s");
  CHECK(keys.has_value());
  if (keys) {
    CHECK_EQ(keys->size(), size_t{2});
    CHECK((*keys)[0] == (Key{U'x', geditor::kModCtrl}));
    CHECK((*keys)[1] == (Key{U's', geditor::kModCtrl}));
  }
}

TEST(key_notation_plain_characters) {
  CHECK(round_trips(L"a"));
  CHECK(round_trips(L"A"));
  CHECK(round_trips(L"\x3042"));
  CHECK(round_trips(L"\U0001F600"));
  const auto k = geditor::parse_key(L"SPC");
  CHECK(k.has_value() && k->code == U' ' && k->mods == 0);
}

TEST(key_notation_modifier_order_is_normalized) {
  const auto k = geditor::parse_key(L"M-C-a");
  CHECK(k.has_value());
  if (k) CHECK_EQ(geditor::key_to_string(*k), std::wstring(L"C-M-a"));
}

TEST(key_notation_invalid_forms_are_rejected) {
  CHECK(rejected(L"C-"));
  CHECK(rejected(L"X-a"));
  CHECK(rejected(L"<f25>"));
  CHECK(rejected(L"<f0>"));
  CHECK(rejected(L"C-A"));
  CHECK(rejected(L"M-A"));
  CHECK(rejected(L"S-1"));
  CHECK(rejected(L"C-C-a"));
  CHECK(rejected(L"<foo>"));
  CHECK(rejected(L"ab"));
  CHECK(rejected(L""));
  CHECK(rejected(L"   "));
  CHECK(rejected(L"C-x C-"));
}
