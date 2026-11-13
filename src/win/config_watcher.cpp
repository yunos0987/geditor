#include "config_watcher.h"

#include "file_io.h"
#include "path.h"

namespace geditor::win {
namespace {

constexpr DWORD kDelayMs = 200;  // wait after the last notification
constexpr int kLockRetries = 3;  // sharing violations tolerated before reading
constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
                          FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION;

// True while another program holds the file without sharing it for reading.
bool locked(const std::wstring& path) {
  HANDLE h = CreateFileW(extended_length_path(path).c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION;
  CloseHandle(h);
  return false;
}

}  // namespace

ConfigWatcher::~ConfigWatcher() {
  stop();
}

bool ConfigWatcher::start(HWND hwnd, UINT message, const std::wstring& path, std::wstring& error) {
  stop();
  hwnd_ = hwnd;
  message_ = message;
  path_ = path;
  file_name_ = file_name_of(path);
  const std::wstring dir = directory_of(path);
  directory_ = CreateFileW(extended_length_path(dir).c_str(), FILE_LIST_DIRECTORY,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
  if (directory_ == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    const bool missing = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
    std::wstring folder = dir;
    if (folder.size() > 3 && folder.back() == L'\\') folder.pop_back();
    error = folder + L": " + (missing ? std::wstring(L"No such folder") : error_text(code));
    return false;
  }
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  io_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (stop_event_ != nullptr && io_event_ != nullptr) {
    thread_ = CreateThread(nullptr, 0, &ConfigWatcher::thread_main, this, 0, nullptr);
  }
  if (thread_ == nullptr) {
    error = error_text(GetLastError());
    stop();
    return false;
  }
  return true;
}

void ConfigWatcher::stop() {
  if (thread_ != nullptr) {
    SetEvent(stop_event_);
    WaitForSingleObject(thread_, INFINITE);
    CloseHandle(thread_);
    thread_ = nullptr;
  }
  if (stop_event_ != nullptr) CloseHandle(stop_event_);
  if (io_event_ != nullptr) CloseHandle(io_event_);
  if (directory_ != INVALID_HANDLE_VALUE) CloseHandle(directory_);
  stop_event_ = nullptr;
  io_event_ = nullptr;
  directory_ = INVALID_HANDLE_VALUE;
}

DWORD WINAPI ConfigWatcher::thread_main(void* self) {
  static_cast<ConfigWatcher*>(self)->run();
  return 0;
}

bool ConfigWatcher::names_file(DWORD bytes) const {
  DWORD offset = 0;
  while (offset + sizeof(FILE_NOTIFY_INFORMATION) <= bytes) {
    const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer_ + offset);
    const int length = static_cast<int>(info->FileNameLength / sizeof(wchar_t));
    if (CompareStringOrdinal(info->FileName, length, file_name_.c_str(),
                             static_cast<int>(file_name_.size()), TRUE) == CSTR_EQUAL) {
      return true;
    }
    if (info->NextEntryOffset == 0) break;
    offset += info->NextEntryOffset;
  }
  return false;
}

void ConfigWatcher::run() {
  OVERLAPPED overlapped{};
  auto issue = [&] {
    ResetEvent(io_event_);
    overlapped = OVERLAPPED{};
    overlapped.hEvent = io_event_;
    return ReadDirectoryChangesW(directory_, buffer_, sizeof(buffer_), FALSE, kFilter, nullptr,
                                 &overlapped, nullptr) != 0;
  };
  // Tells the window that watching stopped (lParam: a new std::wstring with
  // the reason, deleted by the window).
  auto report_stop = [&](DWORD error) {
    auto* text = new std::wstring(error_text(error));
    if (!PostMessageW(hwnd_, message_, 1, reinterpret_cast<LPARAM>(text))) delete text;
  };
  if (!issue()) {
    report_stop(GetLastError());
    return;
  }
  bool changed = false;
  ULONGLONG deadline = 0;  // when `changed`: 200 ms after the last change of the file
  for (;;) {
    HANDLE handles[] = {stop_event_, io_event_};
    DWORD timeout = INFINITE;
    if (changed) {
      const ULONGLONG now = GetTickCount64();
      timeout = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
    }
    const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, timeout);
    if (wait == WAIT_OBJECT_0 + 1) {
      DWORD bytes = 0;
      if (!GetOverlappedResult(directory_, &overlapped, &bytes, FALSE)) {
        report_stop(GetLastError());
        return;
      }
      // 0 bytes: too many changes for the buffer; the file may be one. Only a
      // change of the file restarts the 200 ms wait: other files in the
      // folder (logs written all the time) must not delay the reload.
      if (bytes == 0 || names_file(bytes)) {
        changed = true;
        deadline = GetTickCount64() + kDelayMs;
      }
      if (!issue()) {
        report_stop(GetLastError());
        return;
      }
      continue;
    }
    if (wait == WAIT_TIMEOUT && changed) {
      changed = false;
      for (int i = 0; i < kLockRetries && locked(path_); ++i) {
        if (WaitForSingleObject(stop_event_, kDelayMs) == WAIT_OBJECT_0) break;
      }
      PostMessageW(hwnd_, message_, 0, 0);
      continue;
    }
    break;  // stop event or a failed wait
  }
  CancelIoEx(directory_, &overlapped);
  DWORD bytes = 0;
  GetOverlappedResult(directory_, &overlapped, &bytes, TRUE);
}

}  // namespace geditor::win
