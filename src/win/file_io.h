// File system access for the Host: read, write, inspect, list. M1 writes the
// file in place; the safe save through a temporary file is M4.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "host.h"

namespace geditor::win {

// FileKind for a GetFileAttributesExW failure: only "not found" errors mean
// kNone (a new file); every other error (access denied, sharing violation,
// network failure, bad name) is kError.
FileKind file_kind_from_error(unsigned long error);

// The path to pass to the file APIs: an absolute path that is too long for
// the classic MAX_PATH limit gets the "\\?\" (or "\\?\UNC\") prefix, so that
// files deeper than 260 characters open without longPathAware. Shorter,
// relative and already prefixed paths are returned unchanged.
std::wstring extended_length_path(const std::wstring& path);

// Short English text for a Win32 error code ("Access is denied").
std::wstring error_text(unsigned long error);

// What tells two versions of a file apart without reading it: existence,
// size and last-write time (all zero when the file is missing or cannot be
// inspected).
struct FileState {
  bool exists = false;
  unsigned long long size = 0;
  unsigned long long write_time = 0;

  bool operator==(const FileState&) const = default;
};
FileState file_state(const std::wstring& path);

bool read_file(const std::wstring& path, std::string& bytes, std::wstring& error);
// `error` receives the reason only ("Access is denied").
bool write_file(const std::wstring& path, std::string_view bytes, std::wstring& error);
FileKind file_kind(const std::wstring& path);
bool list_directory(const std::wstring& dir, std::vector<DirEntry>& entries);
std::wstring current_directory();

}  // namespace geditor::win
