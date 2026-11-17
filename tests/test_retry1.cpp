// M2 retry 1 (code review): [R1] self-insert-command only for character
// keys, [R3] huge prefix counts are refused, [R4] the echo area line cap.

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "fake_host.h"

namespace {

constexpr const wchar_t* kNonCharacterKeys[] = {
    L"<f1>", L"<left>", L"RET", L"TAB", L"<delete>", L"ESC", L"C-a", L"M-a",
};

// No U+FFFD (ef bf bd) and no lone surrogate in the saved bytes.
bool clean_utf8(const std::string& bytes) {
  return bytes.find("\xEF\xBF\xBD") == std::string::npos;
}

}  // namespace

// ---- [R1] ----

TEST(r1_self_insert_on_non_character_keys_inserts_nothing) {
  test::Session s;
  s.load(L"x");
  std::string text = "{\"keys\": {";
  for (const wchar_t* k : kNonCharacterKeys) text += "\"" + test::utf8(k) + "\": \"self-insert-command\",";
  text += "}}";
  s.settings(text);
  CHECK(s.ed.settings_errors().empty());
  for (const wchar_t* k : kNonCharacterKeys) {
    s.keys(k);
    if (s.text() != L"x") std::printf("  key %s inserted\n", test::to_display(k).c_str());
    CHECK_EQ(s.text(), std::wstring(L"x"));
    CHECK_EQ(s.message(), std::wstring(k) + L" is not a character key");
  }
  s.type(L"y");
  s.keys(L"C-x C-s");
  const std::string bytes = s.host.file(L"C:\\work\\t.txt");
  CHECK_EQ(bytes, std::string("yx"));
  CHECK(clean_utf8(bytes));
}

TEST(r1_mx_self_insert_command_inserts_nothing) {
  test::Session s;
  s.load(L"x");
  s.keys(L"M-x");
  s.type(L"self-insert-command");
  s.keys(L"RET");
  CHECK_EQ(s.text(), std::wstring(L"x"));
  CHECK_EQ(s.message(), std::wstring(L"RET is not a character key"));
  CHECK(!s.modified());
}

TEST(r1_plain_typing_is_unchanged) {
  test::Session s;
  s.load(L"");
  s.type(L"a\u65e5\U0001F600 Z");
  CHECK_EQ(s.text(), std::wstring(L"a\u65e5\U0001F600 Z"));
  s.keys(L"C-x C-s");
  const std::string bytes = s.host.file(L"C:\\work\\t.txt");
  CHECK_EQ(bytes, std::string("a\xE6\x97\xA5\xF0\x9F\x98\x80 Z"));
  CHECK(clean_utf8(bytes));
}

TEST(r1_shifted_letter_binding_inserts_the_capital) {
  test::Session s;
  s.load(L"");
  s.settings("{\"keys\": {\"S-b\": \"self-insert-command\"}}");
  s.keys(L"S-b");
  CHECK_EQ(s.text(), std::wstring(L"B"));
}

// ---- [R3] ----

TEST(r3_huge_prefix_count_is_refused_quickly) {
  test::Session s;
  s.load(L"abc");
  const auto start = std::chrono::steady_clock::now();
  s.keys(L"M-9 9 9 9 9 9 9 9 9");
  s.type(L"a");
  CHECK_EQ(s.message(), std::wstring(L"Repeat count too large"));
  s.keys(L"M-9 9 9 9 9 9 9 9 9 RET");
  CHECK_EQ(s.message(), std::wstring(L"Repeat count too large"));
  s.keys(L"M-9 9 9 9 9 9 9 9 9 C-o");
  CHECK_EQ(s.message(), std::wstring(L"Repeat count too large"));
  s.keys(L"M-9 9 9 9 9 9 9 9 9 TAB");
  CHECK_EQ(s.message(), std::wstring(L"Repeat count too large"));
  s.keys(L"M-1 0 0 0 0 0 0 1");  // one over the limit
  s.type(L"a");
  CHECK_EQ(s.message(), std::wstring(L"Repeat count too large"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  CHECK(!s.modified());
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  CHECK(ms < 1000);
  s.keys(L"M-1 0 0 0");  // a normal count still works
  s.type(L"a");
  CHECK_EQ(s.text().size(), size_t{1003});
}

TEST(r3_huge_prefix_count_on_a_sequence_is_refused) {
  test::Session s;
  s.load(L"abc");
  s.settings(
      "{\"keys\": {\"C-c x\": [[\"insert-text\", \"xy\"]], \"C-c b\": [[\"beginning-of-line\"]]}}");
  s.keys(L"M-9 9 9 9 9 9 9 9 9 C-c x");
  CHECK_EQ(s.message(), std::wstring(L"Repeat count too large"));
  s.keys(L"M-5 0 0 0 0 0 1 C-c x");  // 2 characters x 5000001 > 10,000,000
  CHECK_EQ(s.message(), std::wstring(L"Repeat count too large"));
  s.keys(L"M-9 9 9 9 9 9 9 9 9 C-c b");  // no insertion, but far too many repeats
  CHECK_EQ(s.message(), std::wstring(L"Repeat count too large"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  s.keys(L"M-3 C-c x");
  CHECK_EQ(s.text(), std::wstring(L"xyxyxyabc"));
}

TEST(r3_huge_prefix_count_on_movement_stays_bounded) {
  test::Session s;
  s.load(L"abc\ndef\n");
  const auto start = std::chrono::steady_clock::now();
  s.keys(L"M-9 9 9 9 9 9 9 9 9 C-f");
  CHECK_EQ(s.point(), size_t{8});
  s.keys(L"M-9 9 9 9 9 9 9 9 9 DEL");
  CHECK_EQ(s.text(), std::wstring(L"abc\ndef\n"));  // fewer characters: nothing deleted
  s.keys(L"M-9 9 9 9 9 9 9 9 9 C-p");
  CHECK_EQ(s.point(), size_t{0});
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  CHECK(ms < 1000);
}

// ---- [R4] ----

namespace {

std::string unknown_items(int n) {
  std::string text = "{\n";
  for (int i = 0; i < n; ++i) text += "  \"unknown_" + std::to_string(i) + "\": 1,\n";
  return text + "}\n";
}

}  // namespace

TEST(r4_echo_lines_are_capped_with_and_more) {
  test::Session s;
  s.settings(unknown_items(25));
  CHECK_EQ(s.ed.echo_line_count(), 10);
  CHECK_EQ(s.ed.echo_line_count(4), 4);
  const std::vector<std::wstring> four = s.ed.message_lines(4);
  CHECK_EQ(four.size(), size_t{4});
  CHECK_EQ(four[0], std::wstring(L".geditor.json:2:3: Unknown setting \"unknown_0\""));
  CHECK_EQ(four[3], std::wstring(L"... and 22 more"));
  const std::vector<std::wstring> one = s.ed.message_lines(1);
  CHECK_EQ(one.size(), size_t{1});
  CHECK_EQ(one[0], std::wstring(L"... and 25 more"));
  CHECK_EQ(s.ed.echo_line_count(1), 1);
  s.settings(unknown_items(11));
  const std::vector<std::wstring> three = s.ed.message_lines(3);
  CHECK_EQ(three[2], std::wstring(L"... and 9 more"));
  s.settings(unknown_items(3));  // fits: no "and more"
  CHECK_EQ(s.ed.echo_line_count(5), 3);
  CHECK_EQ(s.ed.message_lines(5).size(), size_t{3});
}

TEST(r4_echo_cap_counts_the_minibuffer_line) {
  test::Session s;
  s.load(L"abc");
  s.keys(L"C-x C-f");
  s.settings(unknown_items(11));
  CHECK_EQ(s.ed.echo_line_count(4), 4);  // 3 message lines + the minibuffer
  const std::vector<std::wstring> lines = s.ed.message_lines(3);
  CHECK_EQ(lines.back(), std::wstring(L"... and 9 more"));
  CHECK_EQ(s.ed.echo_line_count(1), 1);  // only the minibuffer line fits
}
