// --settings=PATH (decision D11): parsing, resolving and passing it to new
// windows.

#include <windows.h>

#include <string>

#include "command_line.h"
#include "echo_text.h"
#include "file_io.h"
#include "process.h"
#include "test_harness.h"

using geditor::win::CommandLine;
using geditor::win::parse_command_line;

TEST(win_settings_option_is_parsed) {
  const CommandLine c = parse_command_line(L"geditor.exe --settings=build\\s.json +3 a.txt");
  CHECK_EQ(c.settings, std::wstring(L"build\\s.json"));
  CHECK_EQ(c.file, std::wstring(L"a.txt"));
  CHECK_EQ(c.line, size_t{3});
  CHECK(c.errors.empty());
  const CommandLine q = parse_command_line(L"geditor.exe \"--settings=C:\\a b\\s.json\"");
  CHECK_EQ(q.settings, std::wstring(L"C:\\a b\\s.json"));
  CHECK(q.file.empty());
}

TEST(win_settings_option_errors) {
  const CommandLine empty = parse_command_line(L"geditor.exe --settings= a.txt");
  CHECK(empty.settings.empty());
  CHECK_EQ(empty.errors.size(), size_t{1});
  CHECK_EQ(empty.errors[0], std::wstring(L"Invalid option: --settings= (use --settings=PATH)"));
  const CommandLine bare = parse_command_line(L"geditor.exe --settings a.txt");
  CHECK_EQ(bare.errors.size(), size_t{1});
  CHECK_EQ(bare.file, std::wstring(L"a.txt"));
  // After "--" it is a file name.
  const CommandLine file = parse_command_line(L"geditor.exe -- --settings=x");
  CHECK_EQ(file.file, std::wstring(L"--settings=x"));
  CHECK(file.settings.empty());
}

TEST(win_settings_path_resolution) {
  CommandLine c;
  // No option: the user profile file.
  CHECK_EQ(geditor::win::settings_path_for(c, L"C:\\work", L"C:\\Users\\me"),
           std::wstring(L"C:\\Users\\me\\.geditor.json"));
  CHECK_EQ(geditor::win::settings_path_for(c, L"C:\\work", L""), std::wstring());
  // Relative to the current directory; the profile is not used.
  c.settings = L"build\\s.json";
  CHECK_EQ(geditor::win::settings_path_for(c, L"C:\\work", L"C:\\Users\\me"),
           std::wstring(L"C:\\work\\build\\s.json"));
  c.settings = L"..\\s.json";
  CHECK_EQ(geditor::win::settings_path_for(c, L"C:\\work\\sub", L""), std::wstring(L"C:\\work\\s.json"));
  c.settings = L"D:\\x\\s.json";
  CHECK_EQ(geditor::win::settings_path_for(c, L"C:\\work", L"C:\\Users\\me"), std::wstring(L"D:\\x\\s.json"));
}

TEST(win_new_window_gets_the_same_settings) {
  const std::wstring line =
      geditor::win::editor_command_line(L"C:\\bin\\geditor.exe", L"C:\\a b\\f.txt", L"C:\\s dir\\s.json");
  CHECK_EQ(line, std::wstring(L"\"C:\\bin\\geditor.exe\" \"--settings=C:\\s dir\\s.json\" \"C:\\a b\\f.txt\""));
  const CommandLine parsed = parse_command_line(line);
  CHECK_EQ(parsed.settings, std::wstring(L"C:\\s dir\\s.json"));
  CHECK_EQ(parsed.file, std::wstring(L"C:\\a b\\f.txt"));
  CHECK(parsed.errors.empty());
  // Without --settings nothing is added.
  CHECK_EQ(geditor::win::editor_command_line(L"C:\\g.exe", L"C:\\f.txt", L""),
           std::wstring(L"\"C:\\g.exe\" \"C:\\f.txt\""));
}

// [R4] the echo area keeps the mode line and one text row.
TEST(win_max_echo_rows_keeps_mode_line_and_one_text_row) {
  CHECK_EQ(geditor::win::max_echo_rows(20), 18);
  CHECK_EQ(geditor::win::max_echo_rows(5), 3);
  CHECK_EQ(geditor::win::max_echo_rows(3), 1);
  CHECK_EQ(geditor::win::max_echo_rows(2), 1);
  CHECK_EQ(geditor::win::max_echo_rows(0), 1);
}

// [R5] the state compared after the watcher starts.
TEST(win_file_state_tells_versions_apart) {
  wchar_t base[MAX_PATH + 1] = {};
  GetTempPathW(MAX_PATH + 1, base);
  const std::wstring path = std::wstring(base) + L"geditor_test_state_" + std::to_wstring(GetCurrentProcessId()) +
                            L"_" + std::to_wstring(GetTickCount64()) + L".json";
  const geditor::win::FileState missing = geditor::win::file_state(path);
  CHECK(!missing.exists);
  std::wstring error;
  CHECK(geditor::win::write_file(path, "{}\n", error));
  const geditor::win::FileState first = geditor::win::file_state(path);
  CHECK(first.exists);
  CHECK_EQ(first.size, 3ull);
  CHECK(first != missing);
  CHECK(geditor::win::file_state(path) == first);  // unchanged file: same state
  // Same size, later write time.
  HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                         nullptr);
  CHECK(h != INVALID_HANDLE_VALUE);
  FILETIME later{};
  later.dwLowDateTime = static_cast<DWORD>(first.write_time + 10000000ull);
  later.dwHighDateTime = static_cast<DWORD>((first.write_time + 10000000ull) >> 32);
  SetFileTime(h, nullptr, nullptr, &later);
  CloseHandle(h);
  const geditor::win::FileState touched = geditor::win::file_state(path);
  CHECK(touched.size == first.size && touched != first);
  // Another size.
  CHECK(geditor::win::write_file(path, "{ }\n", error));
  CHECK(geditor::win::file_state(path).size == 4ull);
  DeleteFileW(path.c_str());
  CHECK(!geditor::win::file_state(path).exists);
}
