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
  // Gives up ownership (the caller closes it).
  HANDLE release() {
    const HANDLE h = h_;
    h_ = INVALID_HANDLE_VALUE;
    return h;
  }

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
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS: return L"The file exists";
    case ERROR_CANT_RESOLVE_FILENAME: return L"The name of the file cannot be resolved";
    case ERROR_UNABLE_TO_REMOVE_REPLACED: return L"The file to be replaced could not be removed";
    case ERROR_UNABLE_TO_MOVE_REPLACEMENT:
    case ERROR_UNABLE_TO_MOVE_REPLACEMENT_2: return L"The replacement file could not be moved";
    case ERROR_WRITE_FAULT: return L"The device cannot be written to";
    case ERROR_PRIVILEGE_NOT_HELD: return L"A required privilege is not held";
    default: return L"Windows error " + std::to_wstring(error);
  }
}

namespace {

unsigned long long join64(DWORD high, DWORD low) {
  return (static_cast<unsigned long long>(high) << 32) | low;
}

}  // namespace

FileState file_state(const std::wstring& path) {
  FileState state;
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(extended_length_path(path).c_str(), GetFileExInfoStandard, &data)) {
    return state;
  }
  state.exists = true;
  state.size = join64(data.nFileSizeHigh, data.nFileSizeLow);
  state.write_time = join64(data.ftLastWriteTime.dwHighDateTime, data.ftLastWriteTime.dwLowDateTime);
  state.read_only = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
  return state;
}

bool read_file_state(const std::wstring& path, FileState& state, std::wstring& error) {
  state = FileState();
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(extended_length_path(path).c_str(), GetFileExInfoStandard, &data)) {
    const DWORD e = GetLastError();
    if (file_kind_from_error(e) == FileKind::kNone) return true;
    error = error_text(e);
    return false;
  }
  if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
    state.exists = true;
    state.size = join64(data.nFileSizeHigh, data.nFileSizeLow);
    state.write_time = join64(data.ftLastWriteTime.dwHighDateTime, data.ftLastWriteTime.dwLowDateTime);
    state.read_only = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    return true;
  }
  // A link: the attributes of the file it points to.
  Handle file(CreateFileW(extended_length_path(path).c_str(), FILE_READ_ATTRIBUTES,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                          FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (!file.valid() || !GetFileInformationByHandle(file.get(), &info)) {
    error = error_text(GetLastError());
    return false;
  }
  state.exists = true;
  state.size = join64(info.nFileSizeHigh, info.nFileSizeLow);
  state.write_time = join64(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime);
  state.read_only = (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
  return true;
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

// ---- Safe save (docs/PLAN.md 4.8) ----

namespace {

struct FaultName {
  const wchar_t* name;
  SaveFault fault;
};

constexpr FaultName kFaultNames[] = {
    {L"save-inspect", SaveFault::kInspect},       {L"save-link", SaveFault::kLink},
    {L"save-temp-create", SaveFault::kTempCreate}, {L"save-temp-write", SaveFault::kTempWrite},
    {L"save-flush", SaveFault::kFlush},           {L"save-replace", SaveFault::kReplace},
    {L"save-move", SaveFault::kMove},             {L"save-backup", SaveFault::kBackup},
    {L"save-in-place", SaveFault::kInPlace},
};

// "<step>: <reason>"; always false.
bool step_failed(std::wstring& error, const wchar_t* step, DWORD code) {
  error = std::wstring(step) + L": " + error_text(code);
  return false;
}

bool path_exists(const std::wstring& path) {
  return GetFileAttributesW(extended_length_path(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

void delete_file(const std::wstring& path) {
  DeleteFileW(extended_length_path(path).c_str());
}

// "C:\dir\" and "a.txt" of "C:\dir\a.txt".
void split_path(const std::wstring& path, std::wstring* dir, std::wstring* name) {
  const size_t slash = path.find_last_of(L"\\/");
  *dir = slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
  *name = slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// A name in `dir` that no other save uses: ".<name>.<pid>.<n>.<suffix>".
std::wstring temporary_name(const std::wstring& dir, const std::wstring& name, const wchar_t* suffix) {
  static unsigned long counter = 0;  // constant-initialized: no static initializer
  ++counter;
  return dir + L"." + name + L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(counter) +
         L"." + suffix;
}

// The final path of a link, without the "\\?\" prefix.
std::wstring final_path(HANDLE file, DWORD* error) {
  DWORD n = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (n == 0) {
    *error = GetLastError();
    return std::wstring();
  }
  std::wstring path(n, L'\0');
  n = GetFinalPathNameByHandleW(file, path.data(), n, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (n == 0 || n >= path.size()) {
    *error = n == 0 ? GetLastError() : ERROR_CANT_RESOLVE_FILENAME;
    return std::wstring();
  }
  path.resize(n);
  if (path.rfind(L"\\\\?\\UNC\\", 0) == 0) return L"\\\\" + path.substr(8);
  if (path.rfind(L"\\\\?\\", 0) == 0) return path.substr(4);
  return path;
}

bool write_all(HANDLE file, std::string_view bytes, DWORD* error) {
  size_t done = 0;
  while (done < bytes.size()) {
    const DWORD want = static_cast<DWORD>(std::min<size_t>(bytes.size() - done, kChunkBytes));
    DWORD put = 0;
    if (!WriteFile(file, bytes.data() + done, want, &put, nullptr) || put == 0) {
      *error = GetLastError();
      if (*error == ERROR_SUCCESS) *error = ERROR_WRITE_FAULT;
      return false;
    }
    done += put;
  }
  return true;
}

// Writes and flushes a new temporary file next to `target`; on failure it is
// removed.
bool write_temporary(const std::wstring& target, std::string_view bytes, SaveFault fault, std::wstring* temp,
                     std::wstring& error) {
  std::wstring dir;
  std::wstring name;
  split_path(target, &dir, &name);
  HANDLE h = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 16 && h == INVALID_HANDLE_VALUE; ++attempt) {
    *temp = temporary_name(dir, name, L"geditor-tmp");
    if (fault == SaveFault::kTempCreate) return step_failed(error, L"creating the temporary file", ERROR_ACCESS_DENIED);
    h = CreateFileW(extended_length_path(*temp).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      const DWORD e = GetLastError();
      if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS) {
        return step_failed(error, L"creating the temporary file", e);
      }
    }
  }
  if (h == INVALID_HANDLE_VALUE) return step_failed(error, L"creating the temporary file", ERROR_FILE_EXISTS);
  Handle file(h);
  DWORD e = 0;
  bool ok = true;
  const wchar_t* step = L"writing the temporary file";
  if (fault == SaveFault::kTempWrite) {
    e = ERROR_DISK_FULL;
    ok = false;
  } else if (!write_all(file.get(), bytes, &e)) {
    ok = false;
  } else if (fault == SaveFault::kFlush) {
    step = L"flushing the temporary file";
    e = ERROR_DISK_FULL;
    ok = false;
  } else if (!FlushFileBuffers(file.get())) {
    step = L"flushing the temporary file";
    e = GetLastError();
    ok = false;
  }
  if (ok) return true;
  CloseHandle(file.release());
  delete_file(*temp);
  return step_failed(error, step, e);
}

bool save_new(const std::wstring& path, std::string_view bytes, SaveFault fault, std::wstring& error) {
  std::wstring temp;
  if (!write_temporary(path, bytes, fault, &temp, error)) return false;
  // No MOVEFILE_REPLACE_EXISTING: a file that appeared meanwhile is not
  // overwritten.
  if (fault == SaveFault::kMove ||
      !MoveFileExW(extended_length_path(temp).c_str(), extended_length_path(path).c_str(), MOVEFILE_WRITE_THROUGH)) {
    const DWORD e = fault == SaveFault::kMove ? ERROR_ACCESS_DENIED : GetLastError();
    delete_file(temp);
    return step_failed(error, L"moving the temporary file", e);
  }
  return true;
}

bool save_replace(const std::wstring& target, std::string_view bytes, bool backup, SaveFault fault,
                  std::wstring& error) {
  std::wstring temp;
  if (!write_temporary(target, bytes, fault, &temp, error)) return false;
  std::wstring dir;
  std::wstring name;
  split_path(target, &dir, &name);
  // The old file goes to a private name first: "<file>~" changes only when
  // everything else worked.
  const std::wstring old = temporary_name(dir, name, L"geditor-old");
  if (fault == SaveFault::kReplace ||
      !ReplaceFileW(extended_length_path(target).c_str(), extended_length_path(temp).c_str(),
                    extended_length_path(old).c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
    const DWORD e = fault == SaveFault::kReplace ? ERROR_ACCESS_DENIED : GetLastError();
    std::wstring extra;
    if (!path_exists(target) && path_exists(old)) {
      // ERROR_UNABLE_TO_MOVE_REPLACEMENT_2: the old file has the private
      // name; give it its name back.
      if (!MoveFileExW(extended_length_path(old).c_str(), extended_length_path(target).c_str(),
                       MOVEFILE_WRITE_THROUGH)) {
        extra = L"; the old file is now " + old + L" (" + error_text(GetLastError()) + L")";
      }
    }
    if (path_exists(temp)) delete_file(temp);
    step_failed(error, L"replacing the file", e);
    error += extra;
    return false;
  }
  if (backup) {
    const std::wstring backup_path = target + L"~";
    if (fault == SaveFault::kBackup ||
        !MoveFileExW(extended_length_path(old).c_str(), extended_length_path(backup_path).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      const DWORD e = fault == SaveFault::kBackup ? ERROR_ACCESS_DENIED : GetLastError();
      // Put the old file back: the save fails as a whole.
      if (!MoveFileExW(extended_length_path(old).c_str(), extended_length_path(target).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD back = GetLastError();
        step_failed(error, L"making the backup", e);
        error += L"; the file has the new content and the old one is " + old + L" (" + error_text(back) + L")";
        return false;
      }
      return step_failed(error, L"making the backup", e);
    }
  } else if (!DeleteFileW(extended_length_path(old).c_str())) {
    error = L"cannot remove " + old + L": " + error_text(GetLastError());  // a note: the save worked
  }
  return true;
}

// A file with several hard links is written in place (decision F5).
bool save_in_place(const std::wstring& target, std::string_view bytes, bool backup, SaveFault fault,
                   std::wstring& error) {
  const std::wstring backup_path = target + L"~";
  if (backup) {
    std::wstring dir;
    std::wstring name;
    split_path(target, &dir, &name);
    const std::wstring copy = temporary_name(dir, name, L"geditor-tmp");
    DWORD e = ERROR_ACCESS_DENIED;
    bool ok = fault != SaveFault::kBackup &&
              CopyFileW(extended_length_path(target).c_str(), extended_length_path(copy).c_str(), TRUE);
    if (!ok && fault != SaveFault::kBackup) e = GetLastError();
    if (ok && !MoveFileExW(extended_length_path(copy).c_str(), extended_length_path(backup_path).c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      e = GetLastError();
      ok = false;
    }
    if (!ok) {
      if (path_exists(copy)) {
        SetFileAttributesW(extended_length_path(copy).c_str(), FILE_ATTRIBUTE_NORMAL);
        delete_file(copy);
      }
      return step_failed(error, L"making the backup", e);
    }
  }
  Handle file(CreateFileW(extended_length_path(target).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file.valid()) return step_failed(error, L"opening the file for writing", GetLastError());
  DWORD e = 0;
  bool ok = true;
  if (fault == SaveFault::kInPlace) {
    e = ERROR_DISK_FULL;
    ok = false;
  } else if (!write_all(file.get(), bytes, &e) || !SetEndOfFile(file.get()) || !FlushFileBuffers(file.get())) {
    if (e == 0) e = GetLastError();
    ok = false;
  }
  if (ok) return true;
  step_failed(error, L"writing the file in place", e);
  error += backup ? L"; the file may be partly written, " + backup_path + L" has the old content"
                  : std::wstring(L"; the file may be partly written");
  return false;
}

}  // namespace

SaveFault save_fault_from_name(std::wstring_view name, bool* known) {
  for (const FaultName& f : kFaultNames) {
    if (name == f.name) {
      *known = true;
      return f.fault;
    }
  }
  *known = false;
  return SaveFault::kNone;
}

bool save_file(const std::wstring& path, std::string_view bytes, bool backup, std::wstring& error,
               SaveFault fault) {
  error.clear();
  WIN32_FILE_ATTRIBUTE_DATA own{};
  if (fault == SaveFault::kInspect) return step_failed(error, L"reading the file attributes", ERROR_ACCESS_DENIED);
  if (!GetFileAttributesExW(extended_length_path(path).c_str(), GetFileExInfoStandard, &own)) {
    const DWORD e = GetLastError();
    if (file_kind_from_error(e) != FileKind::kNone) return step_failed(error, L"reading the file attributes", e);
    return save_new(path, bytes, fault, error);
  }
  if ((own.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    error = L"reading the file attributes: the path is a folder";
    return false;
  }
  const bool link = (own.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 || fault == SaveFault::kLink;
  const wchar_t* inspect_step = link ? L"resolving the link" : L"reading the file attributes";
  // Opening follows a symbolic link to the file it points to.
  Handle file(CreateFileW(extended_length_path(path).c_str(), FILE_READ_ATTRIBUTES,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                          FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  if (!file.valid()) return step_failed(error, inspect_step, GetLastError());
  std::wstring target = path;
  if (link) {
    if (fault == SaveFault::kLink) return step_failed(error, inspect_step, ERROR_CANT_RESOLVE_FILENAME);
    DWORD e = 0;
    target = final_path(file.get(), &e);
    if (target.empty()) return step_failed(error, inspect_step, e);
  }
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(file.get(), &info)) return step_failed(error, inspect_step, GetLastError());
  CloseHandle(file.release());
  if ((info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
    // Decision F6: the read-only attribute is kept; the save fails.
    error = error_text(ERROR_ACCESS_DENIED) + L" (the file is read-only)";
    return false;
  }
  if (info.nNumberOfLinks >= 2) return save_in_place(target, bytes, backup, fault, error);
  return save_replace(target, bytes, backup, fault, error);
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
