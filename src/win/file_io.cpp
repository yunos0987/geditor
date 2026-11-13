#include "file_io.h"

#include <windows.h>

#include <algorithm>

namespace geditor::win {
namespace {

// Files above this size are refused before reading (the piece tree holds
// fewer than 4G UTF-16 units, and reading needs the bytes in memory).
constexpr unsigned long long kMaxFileBytes = 0x7FFFFFFFull;
constexpr DWORD kChunkBytes = 64u * 1024u * 1024u;

class Handle {
 public:
  explicit Handle(HANDLE h) : h_(h) {}
  ~Handle() {
    if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  HANDLE get() const { return h_; }
  bool valid() const { return h_ != INVALID_HANDLE_VALUE; }

 private:
  HANDLE h_;
};

}  // namespace

FileKind file_kind_from_error(unsigned long error) {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return FileKind::kNone;
    default:
      return FileKind::kError;
  }
}

std::wstring extended_length_path(const std::wstring& path) {
  // 248 = MAX_PATH - 12, the limit of CreateDirectoryW; below it the classic
  // form works everywhere.
  constexpr size_t kClassicLimit = MAX_PATH - 12;
  if (path.size() < kClassicLimit || path.rfind(L"\\\\?\\", 0) == 0) return path;
  std::wstring p = path;
  for (wchar_t& c : p) {
    if (c == L'/') c = L'\\';  // "\\?\" paths are not normalized by Windows
  }
  if (p.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + p.substr(2);
  const bool drive = p.size() >= 3 && ((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')) &&
                     p[1] == L':' && p[2] == L'\\';
  return drive ? L"\\\\?\\" + p : path;
}

std::wstring error_text(unsigned long error) {
  switch (error) {
    case ERROR_FILE_NOT_FOUND: return L"No such file";
    case ERROR_PATH_NOT_FOUND: return L"No such directory";
    case ERROR_ACCESS_DENIED: return L"Access is denied";
    case ERROR_SHARING_VIOLATION: return L"The file is used by another process";
    case ERROR_LOCK_VIOLATION: return L"Part of the file is locked by another process";
    case ERROR_WRITE_PROTECT: return L"The media is write protected";
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL: return L"The disk is full";
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY: return L"Not enough memory";
    case ERROR_INVALID_NAME: return L"The file name is not valid";
    case ERROR_FILENAME_EXCED_RANGE: return L"The file name is too long";
    case ERROR_BAD_NETPATH: return L"The network path was not found";
    case ERROR_BAD_NET_NAME: return L"The network name was not found";
    case ERROR_NETNAME_DELETED: return L"The network name is no longer available";
    case ERROR_UNEXP_NET_ERR: return L"Unexpected network error";
    case ERROR_NETWORK_UNREACHABLE: return L"The network is unreachable";
    case ERROR_NOT_READY: return L"The device is not ready";
    default: return L"Windows error " + std::to_wstring(error);
  }
}

FileState file_state(const std::wstring& path) {
  FileState state;
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(extended_length_path(path).c_str(), GetFileExInfoStandard, &data)) {
    return state;
  }
  state.exists = true;
  state.size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
  state.write_time = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                     data.ftLastWriteTime.dwLowDateTime;
  return state;
}

bool read_file(const std::wstring& path, std::string& bytes, std::wstring& error) {
  bytes.clear();
  Handle file(CreateFileW(extended_length_path(path).c_str(), GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                          OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
  if (!file.valid()) {
    error = L"Cannot read " + path + L": " + error_text(GetLastError());
    return false;
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file.get(), &size)) {
    error = L"Cannot read " + path + L": " + error_text(GetLastError());
    return false;
  }
  const auto total = static_cast<unsigned long long>(size.QuadPart);
  if (total > kMaxFileBytes) {
    error = L"Cannot read " + path + L": the file is too large (" + std::to_wstring(total) +
            L" bytes)";
    return false;
  }
  bytes.resize(static_cast<size_t>(total));
  size_t done = 0;
  while (done < bytes.size()) {
    const DWORD want = static_cast<DWORD>(std::min<size_t>(bytes.size() - done, kChunkBytes));
    DWORD got = 0;
    if (!ReadFile(file.get(), bytes.data() + done, want, &got, nullptr)) {
      error = L"Cannot read " + path + L": " + error_text(GetLastError());
      bytes.clear();
      return false;
    }
    if (got == 0) break;  // the file shrank while reading
    done += got;
  }
  bytes.resize(done);
  return true;
}

bool write_file(const std::wstring& path, std::string_view bytes, std::wstring& error) {
  // OPEN_ALWAYS (not CREATE_ALWAYS) also works for hidden and system files.
  Handle file(CreateFileW(extended_length_path(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                          nullptr, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file.valid()) {
    error = error_text(GetLastError());
    return false;
  }
  size_t done = 0;
  while (done < bytes.size()) {
    const DWORD want = static_cast<DWORD>(std::min<size_t>(bytes.size() - done, kChunkBytes));
    DWORD put = 0;
    if (!WriteFile(file.get(), bytes.data() + done, want, &put, nullptr) || put == 0) {
      error = error_text(GetLastError());
      return false;
    }
    done += put;
  }
  if (!SetEndOfFile(file.get())) {
    error = error_text(GetLastError());
    return false;
  }
  return true;
}

FileKind file_kind(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(extended_length_path(path).c_str(), GetFileExInfoStandard, &data)) {
    return file_kind_from_error(GetLastError());
  }
  return (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? FileKind::kDirectory
                                                                 : FileKind::kFile;
}

bool list_directory(const std::wstring& dir, std::vector<DirEntry>& entries) {
  entries.clear();
  std::wstring pattern = dir;
  if (pattern.empty() || (pattern.back() != L'\\' && pattern.back() != L'/')) pattern += L'\\';
  pattern += L'*';
  pattern = extended_length_path(pattern);
  WIN32_FIND_DATAW data{};
  HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch,
                                 nullptr, FIND_FIRST_EX_LARGE_FETCH);
  if (find == INVALID_HANDLE_VALUE) return false;
  do {
    const std::wstring_view name = data.cFileName;
    if (name == L"." || name == L"..") continue;
    entries.push_back(DirEntry{std::wstring(name),
                               (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
  } while (FindNextFileW(find, &data));
  FindClose(find);
  return true;
}

std::wstring current_directory() {
  const DWORD n = GetCurrentDirectoryW(0, nullptr);
  if (n == 0) return std::wstring();
  std::wstring dir(n, L'\0');
  const DWORD written = GetCurrentDirectoryW(n, dir.data());
  dir.resize(written < n ? written : 0);
  return dir;
}

}  // namespace geditor::win
