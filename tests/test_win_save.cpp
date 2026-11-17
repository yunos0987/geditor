// The safe save of the Win32 layer on real files (docs/PLAN.md 4.8,
// decisions F3 - F6, [C4], [C10]). Every file is made under the folder of
// this test program (build/<preset>/tests/m4a_save/...), never elsewhere.

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "file_io.h"
#include "test_harness.h"

using geditor::win::SaveFault;

namespace {

std::wstring exe_directory() {
  wchar_t path[MAX_PATH * 4] = {};
  const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH * 4);
  std::wstring p(path, n);
  return p.substr(0, p.find_last_of(L'\\'));
}

// A new empty folder under build/<preset>/tests/m4a_save.
std::wstring make_dir(const wchar_t* tag) {
  const std::wstring base = exe_directory() + L"\\m4a_save";
  CreateDirectoryW(base.c_str(), nullptr);
  static unsigned serial = 0;
  const std::wstring dir = base + L"\\" + tag + L"_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                           std::to_wstring(++serial);
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

void put(const std::wstring& path, const std::string& bytes) {
  std::wstring error;
  CHECK(geditor::win::write_file(path, bytes, error));
}

std::string get(const std::wstring& path) {
  std::string bytes;
  std::wstring error;
  if (!geditor::win::read_file(path, bytes, error)) return "<missing>";
  return bytes;
}

bool exists(const std::wstring& path) {
  return GetFileAttributesW(geditor::win::extended_length_path(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

// The names in `dir`, sorted, for "no temporary file is left" checks.
std::vector<std::wstring> listing(const std::wstring& dir) {
  std::vector<std::wstring> names;
  WIN32_FIND_DATAW data{};
  HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
  if (find == INVALID_HANDLE_VALUE) return names;
  do {
    const std::wstring name = data.cFileName;
    if (name != L"." && name != L"..") names.push_back(name);
  } while (FindNextFileW(find, &data));
  FindClose(find);
  std::sort(names.begin(), names.end());
  return names;
}

bool has_temporary(const std::wstring& dir) {
  for (const std::wstring& n : listing(dir)) {
    if (n.find(L".geditor-tmp") != std::wstring::npos || n.find(L".geditor-old") != std::wstring::npos) {
      std::printf("  left: %s\n", test::to_display(n).c_str());
      return true;
    }
  }
  return false;
}

void remove_tree(const std::wstring& dir) {
  for (const std::wstring& n : listing(dir)) {
    const std::wstring p = geditor::win::extended_length_path(dir + L"\\" + n);
    SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!DeleteFileW(p.c_str())) RemoveDirectoryW(p.c_str());
  }
  RemoveDirectoryW(dir.c_str());
}

DWORD link_count(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, 0, nullptr);
  BY_HANDLE_FILE_INFORMATION info{};
  if (h == INVALID_HANDLE_VALUE) return 0;
  GetFileInformationByHandle(h, &info);
  CloseHandle(h);
  return info.nNumberOfLinks;
}

}  // namespace

TEST(win_save_new_file_moves_the_temporary_file) {
  const std::wstring dir = make_dir(L"new");
  const std::wstring file = dir + L"\\a.txt";
  std::wstring error;
  CHECK(geditor::win::save_file(file, "new\r\n", true, error));
  CHECK(error.empty());
  CHECK_EQ(get(file), std::string("new\r\n"));
  CHECK(!exists(file + L"~"));  // nothing to back up
  CHECK_EQ(listing(dir).size(), size_t{1});
  remove_tree(dir);
}

TEST(win_save_existing_file_with_and_without_backup) {
  const std::wstring dir = make_dir(L"existing");
  const std::wstring file = dir + L"\\a.txt";
  put(file, "v1");
  std::wstring error;
  CHECK(geditor::win::save_file(file, "v2", true, error));
  CHECK(error.empty());
  CHECK_EQ(get(file), std::string("v2"));
  CHECK_EQ(get(file + L"~"), std::string("v1"));
  CHECK(geditor::win::save_file(file, "v3", false, error));
  CHECK_EQ(get(file), std::string("v3"));
  CHECK_EQ(get(file + L"~"), std::string("v1"));  // only when asked
  CHECK(!has_temporary(dir));
  CHECK_EQ(listing(dir).size(), size_t{2});
  remove_tree(dir);
}

TEST(win_save_keeps_hidden_attribute_through_replace) {
  const std::wstring dir = make_dir(L"hidden");
  const std::wstring file = dir + L"\\h.txt";
  put(file, "v1");
  SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_HIDDEN);
  std::wstring error;
  CHECK(geditor::win::save_file(file, "v2", false, error));
  CHECK_EQ(get(file), std::string("v2"));
  CHECK((GetFileAttributesW(file.c_str()) & FILE_ATTRIBUTE_HIDDEN) != 0);
  remove_tree(dir);
}

// [C4] / [C10]: a failure at each step leaves the file and "<file>~" as they
// were, names the step, and leaves no temporary file.
TEST(win_save_each_step_failure_changes_nothing) {
  struct Row {
    SaveFault fault;
    bool new_file;
    const wchar_t* step;
  };
  const Row rows[] = {
      {SaveFault::kInspect, false, L"reading the file attributes: Access is denied"},
      {SaveFault::kLink, false, L"resolving the link: The name of the file cannot be resolved"},
      {SaveFault::kTempCreate, false, L"creating the temporary file: Access is denied"},
      {SaveFault::kTempWrite, false, L"writing the temporary file: The disk is full"},
      {SaveFault::kFlush, false, L"flushing the temporary file: The disk is full"},
      {SaveFault::kReplace, false, L"replacing the file: Access is denied"},
      // "<B>": the backup path (M4a fix F5).
      {SaveFault::kBackup, false, L"making the backup <B>: Access is denied"},
      {SaveFault::kTempWrite, true, L"writing the temporary file: The disk is full"},
      {SaveFault::kFlush, true, L"flushing the temporary file: The disk is full"},
      {SaveFault::kMove, true, L"moving the temporary file: Access is denied"},
  };
  for (const Row& r : rows) {
    const std::wstring dir = make_dir(L"fault");
    const std::wstring file = dir + L"\\a.txt";
    put(file + L"~", "older backup");
    if (!r.new_file) put(file, "original");
    const std::vector<std::wstring> before = listing(dir);
    std::wstring error;
    CHECK(!geditor::win::save_file(file, "new content", true, error, r.fault));
    std::wstring expected = r.step;
    if (const size_t b = expected.find(L"<B>"); b != std::wstring::npos) expected.replace(b, 3, file + L"~");
    if (error != expected) std::printf("  fault %d\n", static_cast<int>(r.fault));
    CHECK_EQ(error, expected);
    CHECK_EQ(get(file), r.new_file ? std::string("<missing>") : std::string("original"));
    CHECK_EQ(get(file + L"~"), std::string("older backup"));
    CHECK(!has_temporary(dir));
    CHECK(listing(dir) == before);
    remove_tree(dir);
  }
}

// F3 without injection: "<file>~" is a folder, so the backup cannot be made;
// the old file is put back and nothing else changes.
TEST(win_save_real_backup_failure_puts_the_old_file_back) {
  const std::wstring dir = make_dir(L"backup_dir");
  const std::wstring file = dir + L"\\a.txt";
  put(file, "original");
  CreateDirectoryW((file + L"~").c_str(), nullptr);
  std::wstring error;
  CHECK(!geditor::win::save_file(file, "new", true, error));
  CHECK(error.find(L"making the backup " + file + L"~: ") == 0);
  CHECK_EQ(get(file), std::string("original"));
  CHECK((GetFileAttributesW((file + L"~").c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0);
  CHECK(!has_temporary(dir));
  remove_tree(dir);
}

// F4 without injection: a temporary file cannot be created in a folder that
// is gone.
TEST(win_save_into_a_missing_folder_fails_at_the_temporary_file) {
  const std::wstring dir = make_dir(L"missing");
  const std::wstring file = dir + L"\\no\\such\\a.txt";
  std::wstring error;
  CHECK(!geditor::win::save_file(file, "x", true, error));
  CHECK_EQ(error, std::wstring(L"creating the temporary file: No such directory"));
  remove_tree(dir);
}

// F6: the read-only attribute stops the save with the reason.
TEST(win_save_read_only_file_fails_and_keeps_it) {
  const std::wstring dir = make_dir(L"ro");
  const std::wstring file = dir + L"\\ro.txt";
  put(file, "locked");
  SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY);
  std::wstring error;
  CHECK(!geditor::win::save_file(file, "new", true, error));
  CHECK_EQ(error, std::wstring(L"Access is denied (the file is read-only)"));
  CHECK_EQ(get(file), std::string("locked"));
  CHECK(!exists(file + L"~"));
  CHECK(!has_temporary(dir));
  remove_tree(dir);
}

// F5: a hard-linked file is written in place, after the backup copy; both
// names see the new content and stay linked.
TEST(win_save_hard_link_writes_in_place) {
  const std::wstring dir = make_dir(L"hard");
  const std::wstring a = dir + L"\\a.txt";
  const std::wstring b = dir + L"\\b.txt";
  put(a, "old content");
  CHECK(CreateHardLinkW(b.c_str(), a.c_str(), nullptr));
  CHECK_EQ(link_count(a), DWORD{2});
  std::wstring error;
  CHECK(geditor::win::save_file(a, "new", true, error));
  CHECK(error.empty());
  CHECK_EQ(get(a), std::string("new"));
  CHECK_EQ(get(b), std::string("new"));
  CHECK_EQ(link_count(a), DWORD{2});
  CHECK_EQ(get(a + L"~"), std::string("old content"));
  CHECK_EQ(link_count(a + L"~"), DWORD{1});
  // A failure of the backup copy stops before writing.
  CHECK(!geditor::win::save_file(a, "newer", true, error, SaveFault::kBackup));
  CHECK_EQ(error, L"making the backup " + a + L"~: Access is denied");
  CHECK_EQ(get(a), std::string("new"));
  CHECK_EQ(get(a + L"~"), std::string("old content"));
  // A failure of the in-place write says so and names the backup.
  CHECK(!geditor::win::save_file(a, "newer", true, error, SaveFault::kInPlace));
  CHECK_EQ(error, L"writing the file in place: The disk is full; the file may be partly written, " + a +
                      L"~ has the old content");
  CHECK_EQ(get(a + L"~"), std::string("new"));  // the backup of this attempt
  CHECK(!has_temporary(dir));
  remove_tree(dir);
}

// M4a fix F5: a real failure of the hard-link backup ("<file>~" is a folder)
// names the backup too, and nothing is written.
TEST(win_save_hard_link_backup_failure_names_the_backup) {
  const std::wstring dir = make_dir(L"hard_backup");
  const std::wstring a = dir + L"\\a.txt";
  const std::wstring b = dir + L"\\b.txt";
  put(a, "old content");
  CHECK(CreateHardLinkW(b.c_str(), a.c_str(), nullptr));
  CreateDirectoryW((a + L"~").c_str(), nullptr);
  std::wstring error;
  CHECK(!geditor::win::save_file(a, "new", true, error));
  CHECK(error.find(L"making the backup " + a + L"~: ") == 0);
  CHECK_EQ(get(a), std::string("old content"));
  CHECK_EQ(get(b), std::string("old content"));
  CHECK(!has_temporary(dir));
  remove_tree(dir);
}

namespace {

// The longest name of the volume of the test folder (255 on NTFS).
size_t volume_name_limit() {
  const std::wstring root = exe_directory().substr(0, 3);  // "C:\"
  DWORD length = 0;
  if (!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, &length, nullptr, nullptr, 0)) return 255;
  return length;
}

}  // namespace

// M4a fix F3: a 240-character name is saved with "<file>~" (the temporary
// names are cut to fit); a name of the volume's limit is saved, without a
// backup when one is asked for, and the note names it. No temporary file is
// left. HEAD edef20c failed the 240 case at "creating the temporary file".
TEST(win_save_long_names_fit_the_volume_limit) {
  const size_t limit = volume_name_limit();
  std::printf("  volume name limit: %zu\n", limit);
  CHECK(limit >= 240);
  const std::wstring dir = make_dir(L"long");
  std::wstring error;

  const std::wstring f240 = dir + L"\\" + std::wstring(236, L'n') + L".txt";
  put(f240, "v1");
  CHECK(geditor::win::save_file(f240, "v2", true, error));
  CHECK(error.empty());
  CHECK_EQ(get(f240), std::string("v2"));
  CHECK_EQ(get(f240 + L"~"), std::string("v1"));
  CHECK(!has_temporary(dir));

  // 240 units with surrogate pairs where the temporary name is cut.
  std::wstring pairs;
  for (int i = 0; i < 18; ++i) pairs += L"\xD83D\xDE00";
  const std::wstring e240 = dir + L"\\" + std::wstring(200, L'e') + pairs + L".txt";
  CHECK_EQ(e240.size() - dir.size() - 1, size_t{240});
  put(e240, "e1");
  CHECK(geditor::win::save_file(e240, "e2", true, error));
  CHECK(error.empty());
  CHECK_EQ(get(e240), std::string("e2"));
  CHECK_EQ(get(e240 + L"~"), std::string("e1"));
  CHECK(!has_temporary(dir));

  const std::wstring full = dir + L"\\" + std::wstring(limit - 4, L'm') + L".txt";
  put(full, "w1");
  CHECK(geditor::win::save_file(full, "w2", false, error));
  CHECK(error.empty());
  CHECK_EQ(get(full), std::string("w2"));
  CHECK(geditor::win::save_file(full, "w3", true, error));
  CHECK_EQ(error, L"no backup: " + full + L"~ would be longer than " + std::to_wstring(limit) + L" characters");
  CHECK_EQ(get(full), std::string("w3"));
  CHECK(!exists(full + L"~"));
  CHECK(!has_temporary(dir));

  // A new file of the limit (no backup to make).
  const std::wstring fresh = dir + L"\\" + std::wstring(limit, L'k');
  CHECK(geditor::win::save_file(fresh, "k", true, error));
  CHECK(error.empty());
  CHECK_EQ(get(fresh), std::string("k"));
  CHECK(!has_temporary(dir));
  CHECK_EQ(listing(dir).size(), size_t{6});  // f240, its ~, e240, its ~, full, fresh
  remove_tree(dir);
}

// M4a fix F4: an injected ReplaceFileW failure on a long name keeps PLAN 4.8
// (the old file and "<file>~" unchanged, no temporary file).
TEST(win_save_replace_failure_on_a_long_name_changes_nothing) {
  const std::wstring dir = make_dir(L"long_fault");
  const std::wstring file = dir + L"\\" + std::wstring(236, L'r') + L".txt";
  put(file, "original");
  put(file + L"~", "older backup");
  std::wstring error;
  CHECK(!geditor::win::save_file(file, "new", true, error, SaveFault::kReplace));
  CHECK_EQ(error, std::wstring(L"replacing the file: Access is denied"));
  CHECK_EQ(get(file), std::string("original"));
  CHECK_EQ(get(file + L"~"), std::string("older backup"));
  CHECK(!has_temporary(dir));
  CHECK_EQ(listing(dir).size(), size_t{2});
  remove_tree(dir);
}

// F5: a symbolic link stays a link and its target gets the content. Creating
// links needs Developer Mode or the privilege: without it the test says SKIP.
TEST(win_save_symbolic_link_keeps_the_link) {
  const std::wstring dir = make_dir(L"symlink");
  CreateDirectoryW((dir + L"\\real").c_str(), nullptr);
  const std::wstring target = dir + L"\\real\\t.txt";
  const std::wstring link = dir + L"\\l.txt";
  put(target, "old");
  if (!CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
    std::printf("  SKIP win_save_symbolic_link_keeps_the_link: CreateSymbolicLinkW failed (error %lu: %s)\n",
                GetLastError(), test::to_display(geditor::win::error_text(GetLastError())).c_str());
    remove_tree(dir + L"\\real");
    remove_tree(dir);
    return;
  }
  std::wstring error;
  CHECK(geditor::win::save_file(link, "new", true, error));
  CHECK((GetFileAttributesW(link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
  CHECK_EQ(get(target), std::string("new"));
  CHECK_EQ(get(link), std::string("new"));
  CHECK_EQ(get(target + L"~"), std::string("old"));
  CHECK(!exists(link + L"~"));
  geditor::win::FileState state;
  CHECK(geditor::win::read_file_state(link, state, error));
  CHECK_EQ(state.size, 3ull);
  DeleteFileW(link.c_str());
  remove_tree(dir + L"\\real");
  remove_tree(dir);
}

TEST(win_read_file_state_reports_time_size_and_read_only) {
  const std::wstring dir = make_dir(L"state");
  const std::wstring file = dir + L"\\s.txt";
  geditor::win::FileState state;
  std::wstring error;
  CHECK(geditor::win::read_file_state(file, state, error));
  CHECK(!state.exists);
  put(file, "12345");
  CHECK(geditor::win::read_file_state(file, state, error));
  CHECK(state.exists);
  CHECK_EQ(state.size, 5ull);
  CHECK(state.write_time != 0);
  CHECK(!state.read_only);
  SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY);
  CHECK(geditor::win::read_file_state(file, state, error));
  CHECK(state.read_only);
  // A path below a file cannot be inspected: an error, not "missing".
  CHECK(!geditor::win::read_file_state(file + L"\\x", state, error) || !state.exists);
  remove_tree(dir);
}

TEST(win_save_fault_names) {
  bool known = false;
  CHECK(geditor::win::save_fault_from_name(L"save-flush", &known) == SaveFault::kFlush);
  CHECK(known);
  CHECK(geditor::win::save_fault_from_name(L"save-in-place", &known) == SaveFault::kInPlace);
  CHECK(geditor::win::save_fault_from_name(L"save-nothing", &known) == SaveFault::kNone);
  CHECK(!known);
}

TEST(win_save_replace_errors_1175_to_1177_name_the_cause) {
  // Deferred fix F2: without REPLACEFILE_IGNORE_MERGE_ERRORS, a file whose
  // attributes or permissions cannot be carried over fails with one of these
  // errors. HEAD 6064002 showed the generic texts of error_text ("The file
  // to be replaced could not be removed", "The replacement file could not be
  // moved"); the reason now says what happened, why it can happen and what
  // to do.
  for (const unsigned long code : {1175ul, 1176ul, 1177ul}) {
    const std::wstring text = geditor::win::replace_error_text(code);
    std::printf("  %lu: %s\n", code, test::to_display(text).c_str());
    CHECK(text.find(L"Windows error " + std::to_wstring(code)) != std::wstring::npos);
    CHECK(text.find(L"attributes or the permissions of the file") != std::wstring::npos);
    CHECK(text.find(L"Save it under another name with write-file (C-x C-w)") != std::wstring::npos);
    CHECK(text != geditor::win::error_text(code));
  }
  CHECK(geditor::win::replace_error_text(1175).find(L"could not remove the old file") != std::wstring::npos);
  CHECK(geditor::win::replace_error_text(1177).find(L"could not move the new file") != std::wstring::npos);
  // Other errors keep their short text.
  CHECK_EQ(geditor::win::replace_error_text(ERROR_ACCESS_DENIED), std::wstring(L"Access is denied"));
  CHECK_EQ(geditor::win::replace_error_text(ERROR_SHARING_VIOLATION), geditor::win::error_text(ERROR_SHARING_VIOLATION));
}
