// The version (2026-11-20): VERSION at the top of the source tree holds
// "MAJOR.MINOR.PATCH" and a newline; CMake reads it for the project version
// and the version resource of geditor.exe. The first "## " heading of
// CHANGELOG.md names the same version.

#include <windows.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "test_harness.h"

namespace {

std::string read_source_file(const char* name) {
  std::ifstream in(std::string(GEDITOR_SOURCE_DIR) + "/" + name, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// "1.2.3" with each part one or more ASCII digits.
bool is_version(const std::string& s, int parts[3]) {
  int part = 0;
  size_t digits = 0;
  int value = 0;
  for (char c : s) {
    if (c >= '0' && c <= '9') {
      ++digits;
      value = value * 10 + (c - '0');
    } else if (c == '.' && digits > 0 && part < 2) {
      parts[part++] = value;
      digits = 0;
      value = 0;
    } else {
      return false;
    }
  }
  if (part != 2 || digits == 0) return false;
  parts[2] = value;
  return true;
}

}  // namespace

TEST(version_file_is_one_line_of_major_minor_patch) {
  const std::string text = read_source_file("VERSION");
  CHECK(!text.empty());
  CHECK(text.back() == '\n');
  const std::string version = text.substr(0, text.size() - 1);
  CHECK(version.find('\n') == std::string::npos);
  CHECK(version.find('\r') == std::string::npos);
  int parts[3] = {};
  CHECK(is_version(version, parts));
  // CMake read the same file for the project version.
  CHECK_EQ(version, std::string(GEDITOR_VERSION));
}

TEST(version_changelog_first_heading_names_the_version) {
  const std::string text = read_source_file("CHANGELOG.md");
  const size_t at = text.find("\n## ");
  CHECK(at != std::string::npos);
  if (at == std::string::npos) return;
  const size_t start = at + 4;
  size_t end = start;
  while (end < text.size() && ((text[end] >= '0' && text[end] <= '9') || text[end] == '.')) ++end;
  CHECK_EQ(text.substr(start, end - start), std::string(GEDITOR_VERSION));
}

TEST(version_resource_of_the_exe_names_the_version) {
  const wchar_t* exe = L"" GEDITOR_EXE_PATH;
  DWORD handle = 0;
  const DWORD size = GetFileVersionInfoSizeW(exe, &handle);
  CHECK(size > 0);
  if (size == 0) return;
  std::vector<unsigned char> data(size);
  CHECK(GetFileVersionInfoW(exe, 0, size, data.data()));
  VS_FIXEDFILEINFO* fixed = nullptr;
  UINT fixed_size = 0;
  CHECK(VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixed_size));
  if (fixed == nullptr) return;
  int parts[3] = {};
  CHECK(is_version(GEDITOR_VERSION, parts));
  CHECK_EQ(static_cast<int>(HIWORD(fixed->dwFileVersionMS)), parts[0]);
  CHECK_EQ(static_cast<int>(LOWORD(fixed->dwFileVersionMS)), parts[1]);
  CHECK_EQ(static_cast<int>(HIWORD(fixed->dwFileVersionLS)), parts[2]);
  CHECK_EQ(static_cast<int>(LOWORD(fixed->dwFileVersionLS)), 0);
  CHECK_EQ(fixed->dwProductVersionMS, fixed->dwFileVersionMS);
  CHECK_EQ(fixed->dwProductVersionLS, fixed->dwFileVersionLS);
  for (const wchar_t* key : {L"\\StringFileInfo\\040904B0\\FileVersion", L"\\StringFileInfo\\040904B0\\ProductVersion"}) {
    wchar_t* value = nullptr;
    UINT length = 0;
    CHECK(VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&value), &length));
    if (value == nullptr) continue;
    const std::wstring got(value);
    const std::string expected = GEDITOR_VERSION;
    CHECK(got == std::wstring(expected.begin(), expected.end()));
  }
}

// The icon (tools/gen_icon.py): src/win/geditor.ico holds the 8 sizes, each
// a PNG, and the exe has it as icon group 1 with the same sizes.
TEST(version_icon_file_and_exe_icon_group_hold_every_size) {
  const int expected[] = {16, 20, 24, 32, 40, 48, 64, 256};
  const std::string ico = read_source_file("src/win/geditor.ico");
  CHECK(ico.size() > 6);
  if (ico.size() <= 6) return;
  const auto u16 = [](const char* p) {
    return static_cast<int>(static_cast<unsigned char>(p[0]) | (static_cast<unsigned char>(p[1]) << 8));
  };
  CHECK_EQ(u16(ico.data()), 0);
  CHECK_EQ(u16(ico.data() + 2), 1);
  CHECK_EQ(u16(ico.data() + 4), 8);
  for (int i = 0; i < 8 && 6 + 16 * (i + 1) <= static_cast<int>(ico.size()); ++i) {
    const char* e = ico.data() + 6 + 16 * i;
    const int width = static_cast<unsigned char>(e[0]) == 0 ? 256 : static_cast<unsigned char>(e[0]);
    CHECK_EQ(width, expected[i]);
    const unsigned offset = static_cast<unsigned char>(e[12]) | (static_cast<unsigned char>(e[13]) << 8) |
                            (static_cast<unsigned char>(e[14]) << 16) |
                            (static_cast<unsigned>(static_cast<unsigned char>(e[15])) << 24);
    CHECK(offset + 8 <= ico.size());
    if (offset + 8 <= ico.size()) CHECK(ico.compare(offset + 1, 3, "PNG") == 0);
  }

  const HMODULE exe = LoadLibraryExW(L"" GEDITOR_EXE_PATH, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  CHECK(exe != nullptr);
  if (exe == nullptr) return;
  const HRSRC group = FindResourceW(exe, MAKEINTRESOURCEW(1), RT_GROUP_ICON);
  CHECK(group != nullptr);
  if (group != nullptr) {
    const char* g = static_cast<const char*>(LockResource(LoadResource(exe, group)));
    CHECK(g != nullptr);
    if (g != nullptr) {
      CHECK_EQ(u16(g + 4), 8);
      // GRPICONDIRENTRY is 14 bytes: width, height, ...
      for (int i = 0; i < 8; ++i) {
        const int width = static_cast<unsigned char>(g[6 + 14 * i]) == 0 ? 256 : static_cast<unsigned char>(g[6 + 14 * i]);
        CHECK_EQ(width, expected[i]);
      }
    }
  }
  // Explorer and the taskbar take the icon with the lowest id: no other
  // icon group comes before it.
  int groups = 0;
  EnumResourceNamesW(
      exe, RT_GROUP_ICON,
      [](HMODULE, LPCWSTR, LPWSTR, LONG_PTR count) -> BOOL {
        ++*reinterpret_cast<int*>(count);
        return TRUE;
      },
      reinterpret_cast<LONG_PTR>(&groups));
  CHECK_EQ(groups, 1);
  FreeLibrary(exe);
}
