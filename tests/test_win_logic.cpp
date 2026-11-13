// Pure logic of the Win32 layer: command line, clipboard text conversion,
// Win32 error -> FileKind, and the failure paths of saving and starting a new
// window ([C5], [C10] of M1b).

#include <windows.h>

#include <string>
#include <vector>

#include "clipboard.h"
#include "command_line.h"
#include "file_io.h"
#include "process.h"
#include "test_harness.h"

using geditor::FileKind;
using geditor::win::CommandLine;
using geditor::win::parse_command_line;

namespace {

// A new empty directory under %TEMP% for one test.
std::wstring make_temp_dir(const wchar_t* tag) {
  wchar_t base[MAX_PATH + 1] = {};
  GetTempPathW(MAX_PATH + 1, base);
  std::wstring dir = std::wstring(base) + L"geditor_test_" + tag + L"_" +
                     std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

void remove_file(const std::wstring& path) {
  SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
  DeleteFileW(path.c_str());
}

}  // namespace

// ---- Command line ----

TEST(win_cmdline_empty) {
  const CommandLine c = parse_command_line(L"geditor.exe");
  CHECK(c.file.empty());
  CHECK_EQ(c.line, size_t{0});
  CHECK(!c.measure_startup);
  CHECK(c.errors.empty());
  CHECK(parse_command_line(L"").file.empty());
  CHECK(parse_command_line(L"\"C:\\Program Files\\geditor\\geditor.exe\"   ").file.empty());
}

TEST(win_cmdline_quoted_path_with_spaces) {
  const CommandLine c =
      parse_command_line(L"\"C:\\Program Files\\geditor.exe\" \"C:\\My Documents\\a b.txt\"");
  CHECK_EQ(c.file, std::wstring(L"C:\\My Documents\\a b.txt"));
  CHECK(c.errors.empty());
}

TEST(win_cmdline_plain_path) {
  CHECK_EQ(parse_command_line(L"geditor.exe notes.txt").file, std::wstring(L"notes.txt"));
}

TEST(win_cmdline_line_number) {
  const CommandLine c = parse_command_line(L"geditor.exe +42 \"a b.txt\"");
  CHECK_EQ(c.line, size_t{42});
  CHECK_EQ(c.file, std::wstring(L"a b.txt"));
  CHECK_EQ(parse_command_line(L"geditor.exe +0 x").line, size_t{1});
  const CommandLine bad = parse_command_line(L"geditor.exe +4x2 y");
  CHECK_EQ(bad.line, size_t{0});
  CHECK_EQ(bad.errors.size(), size_t{1});
  CHECK_EQ(bad.file, std::wstring(L"y"));
}

TEST(win_cmdline_measure_startup) {
  const CommandLine c = parse_command_line(L"geditor.exe --measure-startup");
  CHECK(c.measure_startup);
  CHECK(!c.measure_input);
  CHECK(c.file.empty());
  CHECK(parse_command_line(L"geditor.exe --measure-input big.txt").measure_input);
}

TEST(win_cmdline_unknown_option) {
  const CommandLine c = parse_command_line(L"geditor.exe --frobnicate a.txt");
  CHECK_EQ(c.file, std::wstring(L"a.txt"));
  CHECK_EQ(c.errors.size(), size_t{1});
  if (!c.errors.empty()) CHECK_EQ(c.errors[0], std::wstring(L"Unknown option: --frobnicate"));
  // After "--", a leading '-' is a file name.
  CHECK_EQ(parse_command_line(L"geditor.exe -- -x.txt").file, std::wstring(L"-x.txt"));
}

TEST(win_cmdline_extra_file) {
  const CommandLine c = parse_command_line(L"geditor.exe a.txt b.txt");
  CHECK_EQ(c.file, std::wstring(L"a.txt"));
  CHECK_EQ(c.errors.size(), size_t{1});
}

TEST(win_cmdline_split_rules) {
  const std::vector<std::wstring> a =
      geditor::win::split_command_line(L"\"C:\\a b\\x.exe\" a\\\\\\\"b \"c\\\\\" \"d\"\"e\" f\\g");
  CHECK_EQ(a.size(), size_t{5});
  if (a.size() == 5) {
    CHECK_EQ(a[0], std::wstring(L"C:\\a b\\x.exe"));
    CHECK_EQ(a[1], std::wstring(L"a\\\"b"));
    CHECK_EQ(a[2], std::wstring(L"c\\"));
    CHECK_EQ(a[3], std::wstring(L"d\"e"));
    CHECK_EQ(a[4], std::wstring(L"f\\g"));
  }
}

TEST(win_cmdline_quote_argument_round_trip) {
  for (const wchar_t* arg : {L"C:\\My Documents\\a b.txt", L"C:\\dir\\", L"plain", L"",
                             L"q\"uote", L"tail\\\\"}) {
    const std::wstring line = L"geditor.exe " + geditor::win::quote_argument(arg);
    const std::vector<std::wstring> a = geditor::win::split_command_line(line);
    CHECK_EQ(a.size(), size_t{2});
    if (a.size() == 2) CHECK_EQ(a[1], std::wstring(arg));
  }
}

// ---- Clipboard text ----

TEST(win_clipboard_lf_to_crlf) {
  CHECK_EQ(geditor::win::to_clipboard_text(L"a\nb\n"), std::wstring(L"a\r\nb\r\n"));
  CHECK_EQ(geditor::win::to_clipboard_text(L"\n\n"), std::wstring(L"\r\n\r\n"));
}

TEST(win_clipboard_crlf_to_lf) {
  const std::wstring data = L"a\r\nb\r\n";
  CHECK_EQ(geditor::win::from_clipboard_text(data.c_str(), data.size() + 1),
           std::wstring(L"a\nb\n"));
}

TEST(win_clipboard_lone_cr) {
  const std::wstring data = L"a\rb\r\r\nc\r";
  CHECK_EQ(geditor::win::from_clipboard_text(data.c_str(), data.size()),
           std::wstring(L"a\nb\n\nc\n"));
}

TEST(win_clipboard_empty) {
  CHECK_EQ(geditor::win::to_clipboard_text(L""), std::wstring());
  CHECK_EQ(geditor::win::from_clipboard_text(L"", 1), std::wstring());
  CHECK_EQ(geditor::win::from_clipboard_text(nullptr, 0), std::wstring());
}

TEST(win_clipboard_stops_at_nul_and_size) {
  const wchar_t data[] = {L'a', L'b', L'\0', L'c'};
  CHECK_EQ(geditor::win::from_clipboard_text(data, 4), std::wstring(L"ab"));
  const wchar_t unterminated[] = {L'x', L'y', L'z'};
  CHECK_EQ(geditor::win::from_clipboard_text(unterminated, 2), std::wstring(L"xy"));
}

TEST(win_clipboard_surrogate_pairs) {
  const std::wstring emoji = L"a\U0001F600\nb";
  const std::wstring out = geditor::win::to_clipboard_text(emoji);
  CHECK_EQ(out, std::wstring(L"a\U0001F600\r\nb"));
  CHECK_EQ(geditor::win::from_clipboard_text(out.c_str(), out.size()), emoji);
}

// ---- Win32 error -> FileKind ----

TEST(win_file_kind_not_found_is_none) {
  CHECK_EQ(geditor::win::file_kind_from_error(ERROR_FILE_NOT_FOUND), FileKind::kNone);
  CHECK_EQ(geditor::win::file_kind_from_error(ERROR_PATH_NOT_FOUND), FileKind::kNone);
}

TEST(win_file_kind_access_denied_is_error) {
  CHECK_EQ(geditor::win::file_kind_from_error(ERROR_ACCESS_DENIED), FileKind::kError);
}

TEST(win_file_kind_sharing_violation_is_error) {
  CHECK_EQ(geditor::win::file_kind_from_error(ERROR_SHARING_VIOLATION), FileKind::kError);
}

TEST(win_file_kind_network_errors_are_error) {
  for (const unsigned long e : {ERROR_BAD_NETPATH, ERROR_BAD_NET_NAME, ERROR_NETNAME_DELETED,
                                ERROR_UNEXP_NET_ERR, ERROR_NETWORK_UNREACHABLE}) {
    CHECK_EQ(geditor::win::file_kind_from_error(e), FileKind::kError);
  }
}

TEST(win_file_kind_other_errors_are_error) {
  CHECK_EQ(geditor::win::file_kind_from_error(ERROR_INVALID_NAME), FileKind::kError);
  CHECK_EQ(geditor::win::file_kind_from_error(ERROR_NOT_READY), FileKind::kError);
}

TEST(win_file_kind_real_paths) {
  const std::wstring dir = make_temp_dir(L"kind");
  const std::wstring file = dir + L"\\a.txt";
  std::wstring error;
  CHECK(geditor::win::write_file(file, "x", error));
  CHECK_EQ(geditor::win::file_kind(file), FileKind::kFile);
  CHECK_EQ(geditor::win::file_kind(dir), FileKind::kDirectory);
  CHECK_EQ(geditor::win::file_kind(dir + L"\\missing.txt"), FileKind::kNone);
  CHECK_EQ(geditor::win::file_kind(dir + L"\\no\\such\\x.txt"), FileKind::kNone);
  std::vector<geditor::DirEntry> entries;
  CHECK(geditor::win::list_directory(dir, entries));
  CHECK_EQ(entries.size(), size_t{1});
  remove_file(file);
  RemoveDirectoryW(dir.c_str());
}

// ---- Failure paths: never silent ----

TEST(win_write_read_round_trip) {
  const std::wstring dir = make_temp_dir(L"rw");
  const std::wstring file = dir + L"\\b.txt";
  std::wstring error;
  CHECK(geditor::win::write_file(file, "long content here", error));
  CHECK(geditor::win::write_file(file, "short", error));  // truncates
  std::string bytes;
  CHECK(geditor::win::read_file(file, bytes, error));
  CHECK_EQ(bytes, std::string("short"));
  remove_file(file);
  RemoveDirectoryW(dir.c_str());
}

TEST(win_write_read_only_file_fails_with_reason) {
  const std::wstring dir = make_temp_dir(L"ro");
  const std::wstring file = dir + L"\\ro.txt";
  std::wstring error;
  CHECK(geditor::win::write_file(file, "original", error));
  SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY);
  error.clear();
  CHECK(!geditor::win::write_file(file, "new", error));
  CHECK_EQ(error, std::wstring(L"Access is denied"));
  std::string bytes;
  CHECK(geditor::win::read_file(file, bytes, error));
  CHECK_EQ(bytes, std::string("original"));
  remove_file(file);
  RemoveDirectoryW(dir.c_str());
}

TEST(win_read_missing_file_fails_with_message) {
  std::string bytes;
  std::wstring error;
  CHECK(!geditor::win::read_file(L"C:\\no\\such\\dir\\x.txt", bytes, error));
  CHECK(error.find(L"Cannot read C:\\no\\such\\dir\\x.txt: ") == 0);
}

TEST(win_launch_failure_reports_error) {
  std::wstring error;
  CHECK(!geditor::win::launch_editor(L"C:\\no\\such\\geditor.exe", L"C:\\a b.txt", L"", error));
  CHECK(error.find(L"Cannot start a new window: ") == 0);
  error.clear();
  CHECK(!geditor::win::launch_editor(L"", L"C:\\a.txt", L"", error));
  CHECK(!error.empty());
}

// ---- Long paths ([S1] of M1b retry 1) ----

namespace {

// Creates nested directories under `base` until the path of a file inside is
// at least `min_length` characters; returns that directory (no "\\?\").
std::wstring make_deep_dir(const std::wstring& base, size_t min_length) {
  std::wstring dir = base;
  int depth = 0;
  while (dir.size() + 20 < min_length) {
    dir += L"\\deep_directory_level_" + std::to_wstring(depth++);
    CreateDirectoryW((L"\\\\?\\" + dir).c_str(), nullptr);
  }
  return dir;
}

void remove_deep_dir(const std::wstring& base, const std::wstring& deep) {
  std::wstring dir = deep;
  while (dir.size() > base.size()) {
    RemoveDirectoryW((L"\\\\?\\" + dir).c_str());
    dir.resize(dir.rfind(L'\\'));
  }
  RemoveDirectoryW(base.c_str());
}

}  // namespace

TEST(win_long_path_read_write_kind_list) {
  const std::wstring base = make_temp_dir(L"long");
  const std::wstring deep = make_deep_dir(base, 437);
  const std::wstring file = deep + L"\\deep_file_name.txt";
  CHECK(file.size() >= 260);
  std::wstring error;
  CHECK(geditor::win::write_file(file, "deep file", error));
  CHECK_EQ(geditor::win::file_kind(file), FileKind::kFile);
  CHECK_EQ(geditor::win::file_kind(deep), FileKind::kDirectory);
  CHECK_EQ(geditor::win::file_kind(deep + L"\\missing.txt"), FileKind::kNone);
  std::string bytes;
  CHECK(geditor::win::read_file(file, bytes, error));
  CHECK_EQ(bytes, std::string("deep file"));
  std::vector<geditor::DirEntry> entries;
  CHECK(geditor::win::list_directory(deep + L"\\", entries));
  CHECK_EQ(entries.size(), size_t{1});
  if (!entries.empty()) CHECK_EQ(entries[0].name, std::wstring(L"deep_file_name.txt"));
  DeleteFileW((L"\\\\?\\" + file).c_str());
  remove_deep_dir(base, deep);
}

TEST(win_extended_length_path) {
  using geditor::win::extended_length_path;
  CHECK_EQ(extended_length_path(L"C:\\short\\a.txt"), std::wstring(L"C:\\short\\a.txt"));
  const std::wstring long_dir = L"C:\\" + std::wstring(300, L'a');
  CHECK_EQ(extended_length_path(long_dir), L"\\\\?\\" + long_dir);
  const std::wstring unc = L"\\\\server\\share\\" + std::wstring(300, L'b');
  CHECK_EQ(extended_length_path(unc), L"\\\\?\\UNC\\server\\share\\" + std::wstring(300, L'b'));
  CHECK_EQ(extended_length_path(L"\\\\?\\" + long_dir), L"\\\\?\\" + long_dir);
  // Relative paths cannot take the prefix: unchanged.
  const std::wstring relative = std::wstring(300, L'c');
  CHECK_EQ(extended_length_path(relative), relative);
}

TEST(win_error_text) {
  CHECK_EQ(geditor::win::error_text(ERROR_ACCESS_DENIED), std::wstring(L"Access is denied"));
  CHECK_EQ(geditor::win::error_text(123456), std::wstring(L"Windows error 123456"));
}
