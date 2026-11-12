// Echo area / minibuffer line fitting ([S3] of M1b retry 1) and the note for
// several dropped files ([S7]).

#include <string>

#include "echo_text.h"
#include "test_harness.h"

using geditor::win::fit_message;
using geditor::win::fit_minibuffer;
using geditor::win::MinibufferLine;
using geditor::win::text_cells;

namespace {

const std::wstring kLongDir =
    L"C:\\Users\\someone\\Documents\\projects\\very_long_directory_name_one\\"
    L"very_long_directory_name_two\\very_long_directory_name_three";

}  // namespace

TEST(echo_fit_short_message_unchanged) {
  CHECK_EQ(fit_message(L"Wrote C:\\a.txt", 40), std::wstring(L"Wrote C:\\a.txt"));
  CHECK_EQ(fit_message(L"", 10), std::wstring());
}

TEST(echo_fit_path_keeps_reason) {
  const std::wstring message = L"Cannot write " + kLongDir + L"\\file.txt: Access is denied";
  const std::wstring fitted = fit_message(message, 60);
  CHECK(text_cells(fitted) <= 60);
  CHECK(fitted.find(L"Cannot write C:\\") == 0);
  const std::wstring tail = L"\\file.txt: Access is denied";
  CHECK(fitted.size() >= tail.size() && fitted.compare(fitted.size() - tail.size(), tail.size(), tail) == 0);
  CHECK(fitted.find(L"\u2026") != std::wstring::npos);
}

TEST(echo_fit_without_path_keeps_both_ends) {
  const std::wstring message = std::wstring(100, L'a') + L"END";
  const std::wstring fitted = fit_message(message, 30);
  CHECK_EQ(text_cells(fitted), 30);
  CHECK(fitted.find(L"\u2026") != std::wstring::npos);
  CHECK_EQ(fitted.substr(fitted.size() - 3), std::wstring(L"END"));
}

TEST(echo_fit_wide_characters) {
  const std::wstring message = L"\u65E5\u672C\u8A9E" + std::wstring(40, L'\u3042');
  const std::wstring fitted = fit_message(message, 21);
  CHECK(text_cells(fitted) <= 21);
}

TEST(minibuffer_fit_short_input) {
  const MinibufferLine line = fit_minibuffer(L"Find file: ", L"C:\\a\\", 5, L"", 40);
  CHECK_EQ(line.text, std::wstring(L"Find file: C:\\a\\"));
  CHECK_EQ(line.cursor_column, 16);
}

TEST(minibuffer_fit_keeps_prompt_and_cursor) {
  const std::wstring input = kLongDir + L"\\";
  const MinibufferLine line = fit_minibuffer(L"Find file: ", input, input.size(), L"", 50);
  CHECK(line.text.find(L"Find file: \u2026") == 0);
  CHECK(text_cells(line.text) <= 49);
  CHECK(line.cursor_column <= 49);
  CHECK_EQ(line.cursor_column, text_cells(line.text));
  CHECK_EQ(line.text.substr(line.text.size() - 6), std::wstring(L"three\\"));
}

TEST(minibuffer_fit_cursor_in_the_middle) {
  const std::wstring input = std::wstring(30, L'a') + std::wstring(30, L'b') + std::wstring(30, L'c');
  const MinibufferLine line = fit_minibuffer(L"Write file: ", input, 45, L"", 40);
  CHECK(line.text.find(L"Write file: \u2026") == 0);
  CHECK(line.cursor_column < 40);
  // The character under the cursor is a 'b' (offset 45).
  const size_t cursor_index = static_cast<size_t>(line.cursor_column);  // ASCII only
  CHECK(cursor_index < line.text.size());
  if (cursor_index < line.text.size()) CHECK_EQ(line.text[cursor_index], L'b');
}

TEST(minibuffer_fit_message_after_input) {
  const MinibufferLine line = fit_minibuffer(L"Find file: ", L"C:\\x", 4, L"[No match]", 40);
  CHECK_EQ(line.text, std::wstring(L"Find file: C:\\x [No match]"));
  CHECK_EQ(line.cursor_column, 15);
}

TEST(invisible_format_chars) {
  using geditor::win::is_invisible_format_char;
  for (const char32_t c : {U'​', U'‌', U'‍', U'‎', U'‏', U'⁠',
                           U'︀', U'️', U'﻿'}) {
    CHECK(is_invisible_format_char(c));
  }
  for (const char32_t c : {U'゙', U'́', U' ', U'⁡', U'︐', U'a'}) {
    CHECK(!is_invisible_format_char(c));
  }
}

TEST(dropped_files_note_text) {
  CHECK_EQ(geditor::win::dropped_files_note(1), std::wstring());
  CHECK_EQ(geditor::win::dropped_files_note(3),
           std::wstring(L"Opened only the first of 3 dropped files"));
}
