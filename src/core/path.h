// Windows path helpers (pure string operations, no file system access).
#pragma once

#include <string>
#include <string_view>

namespace geditor {

// "C:\..." or "\\server\share\...".
bool is_absolute_path(std::wstring_view path);

// Makes `path` absolute against `base_dir`, turns '/' into '\', removes
// repeated separators and "." / ".." segments. A trailing separator is kept
// only for a drive root such as "C:\".
std::wstring normalize_path(std::wstring_view path, std::wstring_view base_dir);

// Directory part with a trailing backslash ("C:\a\b.txt" -> "C:\a\").
std::wstring directory_of(std::wstring_view path);
// File name part ("C:\a\b.txt" -> "b.txt").
std::wstring file_name_of(std::wstring_view path);
// Appends a backslash unless `dir` already ends with one.
std::wstring with_trailing_separator(std::wstring_view dir);

bool same_path(std::wstring_view a, std::wstring_view b);

}  // namespace geditor
