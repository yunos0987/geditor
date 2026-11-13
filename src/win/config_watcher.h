// Watches the settings file for changes (docs/PLAN.md 2 "watching the
// settings file"): a thread waits on ReadDirectoryChangesW for the file's
// folder, and 200 ms after a change of the file posts a message to the
// window, which then reloads the settings on its own thread. While another
// program holds the file without sharing, the thread waits and tries again
// (up to 3 times) before posting.
#pragma once

#include <string>

#include <windows.h>

namespace geditor::win {

class ConfigWatcher {
 public:
  ConfigWatcher() = default;
  ~ConfigWatcher();
  ConfigWatcher(const ConfigWatcher&) = delete;
  ConfigWatcher& operator=(const ConfigWatcher&) = delete;

  // Starts watching `path` and posting `message` to `hwnd`. Returns false
  // with the reason in `error` when the watch cannot start (for example the
  // folder does not exist).
  bool start(HWND hwnd, UINT message, const std::wstring& path, std::wstring& error);
  // Stops the thread (also done by the destructor).
  void stop();

 private:
  static DWORD WINAPI thread_main(void* self);
  void run();
  // True when the notifications in buffer_ (`bytes` long) name the file.
  bool names_file(DWORD bytes) const;

  HWND hwnd_ = nullptr;
  UINT message_ = 0;
  std::wstring path_;
  std::wstring file_name_;
  HANDLE directory_ = INVALID_HANDLE_VALUE;
  HANDLE stop_event_ = nullptr;
  HANDLE io_event_ = nullptr;
  HANDLE thread_ = nullptr;
  alignas(DWORD) BYTE buffer_[16 * 1024] = {};
};

}  // namespace geditor::win
